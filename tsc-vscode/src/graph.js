const { randomUUID } = require('node:crypto');
const profiles = { forest: require('../profiles/forest.json'), sevenbridge: require('../profiles/sevenbridge.json') };
const marker = ';@editor-node ';
const stop = '_editor_stop';
function profile(id) { if (!profiles[id]) throw new Error('Unknown engine profile'); return profiles[id]; }
function quote(value) {
    return '"' + String(value).replace(/\\/g, '\\\\').replace(/"/g, '\\"').replace(/\n/g, '\\n').replace(/\r/g, '\\r').replace(/\t/g, '\\t') + '"';
}
function integer(value, min = -32768, max = 32767) {
    const number = Number(value);
    if (!Number.isInteger(number) || number < min || number > max) throw new Error('Integer is out of range');
    return number;
}
function label(value) { if (!/^[A-Za-z_][A-Za-z0-9_]*$/.test(value)) throw new Error('Invalid label'); return value; }
function byteIndex(text, offset) {
    const bytes = Buffer.from(text);
    if (!Number.isInteger(offset) || offset < 0 || offset > bytes.length || (offset < bytes.length && (bytes[offset] & 0xc0) === 0x80))
        throw new Error('Invalid UTF-8 source offset');
    return bytes.subarray(0, offset).toString('utf8').length;
}
function slice(text, start, end) { return text.slice(byteIndex(text, start), byteIndex(text, end)); }
function applyChanges(text, changes) {
    let last = Buffer.byteLength(text) + 1;
    for (const change of [...changes].sort((a, b) => b.start - a.start)) {
        if (change.start > change.end || change.end > last) throw new Error('Overlapping source edits');
        text = text.slice(0, byteIndex(text, change.start)) + change.text + text.slice(byteIndex(text, change.end));
        last = change.start;
    }
    return text;
}
function graph(text, document) {
    if (document.version !== 1 || !Array.isArray(document.lines) || !Array.isArray(document.diagnostics))
        throw new Error('Upgrade LiarsoftTool: --inspect-tsc protocol 1 is required');
    const nodes = [], edges = [];
    for (let i = 0; i < document.lines.length; ++i) {
        const line = document.lines[i];
        if (line.kind !== 'label') continue;
        const id = line.tokens[0].value.slice(1), previous = document.lines[i - 1];
        let meta = {}, metadata;
        if (previous && slice(text, previous.start, previous.end).startsWith(marker)) {
            try {
                const parsed = JSON.parse(slice(text, previous.start, previous.end).slice(marker.length));
                if (parsed && typeof parsed === 'object' && !Array.isArray(parsed)) { meta = parsed; metadata = previous; }
            }
            catch { /* Invalid editor comments remain ordinary source comments. */ }
        }
        nodes.push({ id, label: line, metadata, meta,
            start: metadata ? metadata.start : line.start, end: Buffer.byteLength(text),
            position: { x: Number.isFinite(meta.x) ? meta.x : (nodes.length % 4) * 280,
                        y: Number.isFinite(meta.y) ? meta.y : Math.floor(nodes.length / 4) * 220 }, commands: [] });
    }
    if (!nodes.length && document.lines.some(l => l.kind === 'command')) {
        const first = document.lines.find(l => l.kind === 'command');
        nodes.push({ id: '__entry', start: first.start, end: Buffer.byteLength(text), meta: {}, position: { x: 0, y: 0 }, commands: [] });
    }
    for (let i = 0; i < nodes.length; ++i) {
        const node = nodes[i]; node.end = nodes[i + 1]?.start ?? Buffer.byteLength(text);
        node.commands = document.lines.filter(l => l.kind === 'command' && l.start >= node.start && l.start < node.end);
        node.events = [];
        let group;
        for (const line of document.lines.filter(l => l.start >= node.start && l.start < node.end)) {
            const raw = slice(text, line.start, line.end).trim();
            if (line.kind === 'comment' && /^;@editor-event [A-Za-z]+$/.test(raw)) {
                if (group) node.events.push(group);
                group = { start: line.start, end: line.end, type: raw.slice(15), commands: [] };
            }
            else if (line.kind === 'comment' && raw === ';@editor-event-end' && group) { group.end = line.end; node.events.push(group); group = undefined; }
            else if (line.kind === 'command') {
                if (group) { group.commands.push(line); group.end = line.end; }
                else node.events.push({ start: line.start, end: line.end, type: line.opcode === 81 ? 'dialogue' : 'raw', commands: [line] });
            }
        }
        if (group) node.events.push(group);
        const terminalIndex = node.commands.findIndex(c => [5, 8, 12, 14, 16].includes(c.opcode));
        const reachable = terminalIndex < 0 ? node.commands : node.commands.slice(0, terminalIndex + 1);
        node.flowEnd = [8, 12, 16].includes(reachable.at(-1)?.opcode);
        const targets = [];
        for (const command of reachable) {
            const refs = command.tokens.filter(t => t.role === 'target');
            if ([3, 4, 5, 14].includes(command.opcode))
                refs.slice(0, command.opcode === 14 ? Number(command.tokens[1].value) : 1).forEach((token, index) =>
                    targets.push({ token, handle: command.opcode === 14 ? String(index) : command.opcode === 3 ? 'false' : command.opcode === 4 ? 'true' : 'next' }));
        }
        if (targets.some(t => t.handle === 'false')) {
            const next = targets.find(t => t.handle === 'next'); if (next) next.handle = 'true';
        }
        for (const target of targets) edges.push({ id: `${node.id}:${target.token.start}`, source: node.id, target: target.token.value, sourceHandle: target.handle, token: target.token });
        const last = reachable.at(-1);
        if (!targets.length && nodes[i + 1] && (!last || ![8, 12, 16].includes(last.opcode)))
            edges.push({ id: `${node.id}:fallthrough`, source: node.id, target: nodes[i + 1].id, sourceHandle: 'next', implicit: true });
    }
    return { nodes, edges, editable: !document.diagnostics.length, diagnostics: document.diagnostics };
}
function metadata(node, values, eol) {
    const meta = { ...node.meta, ...values };
    return { start: node.metadata?.start ?? node.label.start, end: node.metadata?.end ?? node.label.start, text: marker + JSON.stringify(meta) + eol };
}
function newScene(engine, titles = { start: 'Start', end: 'End' }) {
    const p = profile(engine);
    return `;@gsc-byte-format ${p.byteFormat}\n;@gsc-schema ${p.schema}\n` +
        `${marker}${JSON.stringify({ title: titles.start, kind: 'paragraph', x: 0, y: 0 })}\n:start\n*goto ${stop}\n` +
        `${marker}${JSON.stringify({ title: titles.end, kind: 'end', x: 280, y: 0 })}\n:${stop}\n*end\n`;
}
function flow(kind, fields, engine) {
    const p = profile(engine), target = label(fields.target || stop);
    if (kind === 'end') return '*end\n';
    if (kind === 'paragraph') return `*goto ${target}\n`;
    if (kind === 'condition') {
        const family = { '==': 4, '>=': 5, '>': 6, '<=': 7, '<': 8, '!=': 9 }[fields.operator || '=='];
        if (!family) throw new Error('Unsupported comparison');
        const value = integer(fields.value ?? 0);
        return `*vm 0x${family.toString(16)}800 0 ${integer(fields.register, 16, 6999)} ${p.byteFormat === 'legacy-28' ? value & 65535 : value}\n` +
            `*jz ${label(fields.false || stop)}\n*goto ${label(fields.true || stop)}\n`;
    }
    if (kind === 'choice') {
        const choices = fields.choices || [{ text: 'Yes', target: stop }, { text: 'No', target: stop }];
        if (choices.length < 1 || choices.length > 5) throw new Error('Choose one to five options');
        const padded = Array.from({ length: 5 }, (_, i) => choices[i] || { text: '', target: stop });
        return `*select ${choices.length} ${quote(fields.prompt || '')} ${padded.map(c => label(c.target)).join(' ')} ${padded.map(c => quote(c.text)).join(' ')} ${p.selectOperands.join(' ')}\n`;
    }
    throw new Error('Unknown node type');
}
function event(type, fields, engine) {
    const p = profile(engine), n = (key, fallback = 0, min = -32768, max = 32767) => integer(fields[key] ?? fallback, min, max);
    switch (type) {
    case 'dialogue': return `*TXT 0 ${n('voice', 0, 0, 0xffffffff)} 0 0 "" ${quote('^g' + String(n('speaker', 999, 0, 999)).padStart(3, '0') + String(fields.text || '').replace(/\r?\n/g, '^n'))} 1\n`;
    case 'background': case 'sprite': {
        const layer = n('layer', type === 'background' ? 10 : 30, 1, 49);
        const folder = fields.folder || (type === 'background' ? 'grpe' : 'grpo');
        if (!/^[A-Za-z0-9_]+$/.test(folder)) throw new Error('Invalid image folder');
        return `*queue\n*folder ${layer} ${quote(folder)}\n*locmode ${layer} 0 0\n*load ${layer} ${n('image', 1, 0)} ${n('x')} ${n('y')} 0 0\n*update 0 0 0\n`;
    }
    case 'clear': return `*queue\n*cls ${n('layer', 30, 1, 49)} 0\n*update 0 0 0\n`;
    case 'music': return `*bgm_on ${n('track', 1, 1)} ${p.bgmOperands.join(' ')}\n`;
    case 'stopMusic': return '*bgm_off ' + (engine === 'forest' ? '0' : '0 0') + '\n';
    case 'voice': return `*voice ${n('voice', 1, 0, 0xffffffff)} 0 0 0\n`;
    case 'sound': return `*se ${n('sound', 1, 0)}\n*se_on 0 0 0\n`;
    case 'wait': return `*wait ${n('duration', 1000, 0)}\n`;
    case 'call': return engine === 'forest' ? `*gosub ${n('scene', 1000, 0)}\n` : `*gosub ${n('scene', 2001, 0)} "" ${Array(10).fill(0).join(' ')}\n`;
    case 'return': return '*return 0\n';
    case 'variable': {
        const reg = n('register', undefined, 16, 6999), value = n('value');
        const raw = p.byteFormat === 'legacy-28' ? value & 65535 : value;
        if (fields.operation === 'add' || fields.operation === 'subtract')
            return `*vm 0x${fields.operation === 'add' ? 'a' : 'b'}800 1 ${reg} ${raw}\n*vm 0x1900 2 ${reg} 1\n`;
        return `*vm 0x1800 1 ${reg} ${raw}\n`;
    }
    default: throw new Error('Unknown event');
    }
}
function mutate(text, doc, operation, engine) {
    const g = graph(text, doc), eol = text.includes('\r\n') ? '\r\n' : '\n';
    if (!g.editable) throw new Error('Fix source errors before graphical editing');
    const node = g.nodes.find(n => n.id === operation.node);
    const nl = value => value.replace(/\r?\n/g, eol);
    if (operation.type === 'addNode') {
        const id = 'N_' + randomUUID().replaceAll('-', ''), kind = operation.kind || 'paragraph';
        return [{ start: Buffer.byteLength(text), end: Buffer.byteLength(text), text: nl('\n' + marker + JSON.stringify({ title: operation.title || kind, kind, x: (g.nodes.length % 4) * 280, y: Math.floor(g.nodes.length / 4) * 220 }) + '\n:' + id + '\n' + flow(kind, operation.fields || {}, engine)) }];
    }
    if (!node) throw new Error('Node no longer exists');
    if (operation.type === 'move' || operation.type === 'title') {
        if (!node.label) throw new Error('Add a source label before editing node layout');
        const values = operation.type === 'title' ? { title: String(operation.title) } : { x: Number(operation.x), y: Number(operation.y) };
        if (operation.type === 'move' && (!Number.isFinite(values.x) || !Number.isFinite(values.y))) throw new Error('Invalid node position');
        return [metadata(node, values, eol)];
    }
    if (operation.type === 'rename') {
        if (!node.label || node.id === stop || node.id === '__entry') throw new Error('This label is protected');
        const name = label(operation.name);
        if (g.nodes.some(n => n.id === name && n !== node)) throw new Error('Duplicate label');
        return [{ start: node.label.tokens[0].start, end: node.label.tokens[0].end, text: ':' + name },
            ...doc.lines.flatMap(l => l.tokens.filter(t => t.role === 'target' && t.value === node.id).map(t => ({ start: t.start, end: t.end, text: name })))];
    }
    if (operation.type === 'deleteNode') {
        if (node === g.nodes[0] || node.id === stop || g.edges.some(e => e.target === node.id)) throw new Error('Reconnect incoming edges before deleting this node');
        return [{ start: node.start, end: node.end, text: '' }];
    }
    if (operation.type === 'connect' || operation.type === 'disconnect') {
        const target = operation.type === 'disconnect' ? stop : label(operation.target);
        if (!g.nodes.some(n => n.id === target)) throw new Error('Target does not exist');
        const edge = g.edges.find(e => e.source === node.id && e.sourceHandle === (operation.handle || 'next'));
        if (g.edges.filter(e => e.source === node.id && e.sourceHandle === (operation.handle || 'next')).length > 1)
            throw new Error('Edit complex control flow in source');
        if (edge?.token) return [{ start: edge.token.start, end: edge.token.end, text: target }];
        if (node.meta.kind === 'end' || [3, 4, 12, 14, 16].some(op => node.commands.some(c => c.opcode === op))) throw new Error('This output port cannot be connected');
        const terminal = node.commands.at(-1);
        if (terminal?.opcode === 8) return [{ start: terminal.start, end: terminal.end, text: '*goto ' + target + eol }];
        return [{ start: node.end, end: node.end, text: '*goto ' + target + eol }];
    }
    if (operation.type === 'appendEvent') {
        if (node.id === stop) throw new Error('The disconnected-edge destination must stay empty');
        const terminal = node.commands.find(c => [3, 4, 5, 8, 12, 14, 16].includes(c.opcode));
        const terminalEvent = node.events.find(e => e.commands.includes(terminal));
        if (operation.event === 'return' && node.commands.some(c => [3, 4, 12, 14, 16].includes(c.opcode))) throw new Error('Use return in a paragraph without branching');
        return [{ start: terminalEvent?.start ?? terminal?.start ?? node.end, end: terminalEvent?.start ?? terminal?.start ?? node.end,
            ...(operation.event === 'return' && terminal ? { end: terminal.end } : {}),
            text: nl(';@editor-event ' + operation.event + '\n' + event(operation.event, operation.fields || {}, engine) + ';@editor-event-end\n') }];
    }
    if (operation.type === 'setToken') {
        const command = node.commands.find(c => c.start === operation.command), token = command?.tokens[operation.index];
        if (!token || operation.index === 0 || token.role === 'target') throw new Error('Use graph connections for targets');
        const value = String(operation.value);
        if (!token.quoted && !/^@{0,16}-?(?:\d+|0x[0-9a-f]+)$/i.test(value)) throw new Error('Invalid numeric operand');
        return [{ start: token.start, end: token.end, text: token.quoted ? quote(value) : value }];
    }
    if (operation.type === 'deleteEvent') {
        const event = node.events.find(e => e.start === operation.command);
        if (event && !event.commands.some(c => [3, 4, 5, 8, 14, 200].includes(c.opcode)))
            return [{ start: event.start, end: event.end, text: '' }];
        const command = node.commands.find(c => c.start === operation.command);
        if (!command || [3, 4, 5, 8, 14, 200].includes(command.opcode)) throw new Error('Cannot delete a flow command as an event');
        return [{ start: command.start, end: command.end, text: '' }];
    }
    if (operation.type === 'moveEvent') {
        const index = node.events.findIndex(e => e.start === operation.command), target = index + (operation.direction === 'up' ? -1 : 1);
        if (index < 0 || target < 0 || target >= node.events.length) throw new Error('Event cannot move further');
        const events = [node.events[index], node.events[target]].sort((a, b) => a.start - b.start);
        if (events.some(e => e.commands.some(c => [3, 4, 5, 8, 12, 14, 16, 200].includes(c.opcode)))) throw new Error('Events cannot cross control flow');
        return [{ start: events[0].start, end: events[1].end,
            text: slice(text, events[1].start, events[1].end) + slice(text, events[0].end, events[1].start) + slice(text, events[0].start, events[0].end) }];
    }
    throw new Error('Unknown edit');
}
module.exports = { profiles, profile, quote, integer, byteIndex, slice, applyChanges, graph, metadata, newScene, event, flow, mutate, stop };
