const { test } = require('node:test');
const assert = require('node:assert/strict');
const path = require('node:path');
const g = require('../src/graph');
const { inspect } = require('../src/project');
const { staticState, textRuns } = require('../src/preview');
const tool = process.env.LIARSOFTTOOL_PATH || path.resolve(__dirname, '../../build/liarsofttool');
async function edit(text, operation, engine = 'forest') {
    const doc = await inspect(text, tool);
    assert.deepEqual(doc.diagnostics, []);
    const changed = g.applyChanges(text, g.mutate(text, doc, operation, engine));
    assert.deepEqual((await inspect(changed, tool)).diagnostics, [], changed);
    return changed;
}
test('both profiles generate compiler-valid high-level events, comparisons and five choices', async () => {
    for (const engine of Object.keys(g.profiles)) {
        let source = g.newScene(engine);
        for (const type of ['dialogue', 'background', 'sprite', 'clear', 'music', 'stopMusic', 'voice', 'sound', 'wait', 'call', 'return'])
            source = await edit(source, { type: 'appendEvent', node: 'start', event: type, fields: { text: '日本語; "quoted" \\ ^cytext\nnext', register: 20 } }, engine);
        for (const operation of ['set', 'add', 'subtract']) source = await edit(source, { type: 'appendEvent', node: 'start', event: 'variable', fields: { register: 20, value: -1, operation } }, engine);
        for (const operator of ['==', '>=', '>', '<=', '<', '!=']) source = await edit(source, { type: 'addNode', kind: 'condition', fields: { register: 20, value: -2, operator } }, engine);
        source = await edit(source, { type: 'addNode', kind: 'choice', fields: { choices: Array.from({ length: 5 }, (_, i) => ({ text: 'Option ' + i, target: g.stop })) } }, engine);
        assert.match(source, engine === 'forest' ? /\*gosub 1000\r?\n/ : /\*gosub 2001 "" 0/);
        assert.throws(() => g.flow('choice', { choices: Array(6).fill({ target: g.stop }) }, engine));
        assert.throws(() => g.event('variable', { register: 0 }, engine));
        assert.throws(() => g.event('sprite', { folder: '../escape' }, engine));
    }
});
test('granular edits preserve Unicode, comments, unknown opcodes, CRLF and metadata byte-for-byte', async () => {
    let text = g.newScene('forest').replace('*goto _editor_stop', '*opcode 84 0 0\n*TXT 0 0 0 0 "" "^g999中😀;hello" 1\n*goto _editor_stop')
        .replaceAll('\n', '\r\n') + '; tail: untouched 😀\r\n';
    const before = text, doc = await inspect(text, tool), command = doc.lines.find(l => l.opcode === 81);
    assert.deepEqual(doc.diagnostics, []);
    text = await edit(text, { type: 'setToken', node: 'start', command: command.start, index: 6, value: '^g999replaced"\\\ntext' });
    assert.equal(text, before.replace('"^g999中😀;hello"', g.quote('^g999replaced"\\\ntext')));
    text = await edit(text, { type: 'move', node: 'start', x: 42, y: 24 });
    assert.match(text, /"x":42,"y":24}\r\n:start/);
    assert(text.endsWith('; tail: untouched 😀\r\n'));
    assert.equal(g.applyChanges('abc', [{ start: 0, end: 1, text: 'A' }, { start: 1, end: 2, text: 'B' }]), 'ABc');
    assert.throws(() => g.applyChanges('abc', [{ start: 0, end: 2, text: 'A' }, { start: 1, end: 3, text: 'B' }]));
    assert.throws(() => g.byteIndex('😀', 1), /offset/);
});
test('graph connections, rename, deletion guards and implicit fallthrough', async () => {
    let text = await edit(g.newScene('forest'), { type: 'addNode', kind: 'paragraph', title: 'Second' });
    let doc = await inspect(text, tool), node = g.graph(text, doc).nodes.at(-1).id;
    text = await edit(text, { type: 'connect', node: 'start', target: node });
    text = await edit(text, { type: 'rename', node, name: 'second' });
    assert.match(text, /\*goto second/); assert.match(text, /:second/);
    doc = await inspect(text, tool);
    assert.throws(() => g.mutate(text, doc, { type: 'deleteNode', node: 'second' }), /incoming/);
    text = await edit(text, { type: 'disconnect', node: 'start' });
    text = await edit(text, { type: 'deleteNode', node: 'second' });
    assert(!text.includes(':second'));
    const implicit = g.newScene('forest').replace('*goto _editor_stop\n', '');
    assert(g.graph(implicit, await inspect(implicit, tool)).edges[0].implicit);
    assert.throws(() => g.mutate(text, doc, { type: 'rename', node: 'start', name: 'bad label' }));
});
test('condition/choice output ports are independent; event groups move and delete as units', async () => {
    let text = g.newScene('sevenbridge');
    text = await edit(text, { type: 'addNode', kind: 'condition', fields: { register: 21 } }, 'sevenbridge');
    const condition = g.graph(text, await inspect(text, tool)).nodes.at(-1).id;
    text = await edit(text, { type: 'connect', node: condition, handle: 'true', target: 'start' }, 'sevenbridge');
    const edges = g.graph(text, await inspect(text, tool)).edges.filter(e => e.source === condition);
    assert.equal(edges.find(e => e.sourceHandle === 'true').target, 'start');
    assert.equal(edges.find(e => e.sourceHandle === 'false').target, g.stop);
    text = await edit(text, { type: 'appendEvent', node: 'start', event: 'background', fields: {} }, 'sevenbridge');
    text = await edit(text, { type: 'appendEvent', node: 'start', event: 'dialogue', fields: { text: 'second' } }, 'sevenbridge');
    let node = g.graph(text, await inspect(text, tool)).nodes[0];
    text = await edit(text, { type: 'moveEvent', node: 'start', command: node.events[1].start, direction: 'up' }, 'sevenbridge');
    node = g.graph(text, await inspect(text, tool)).nodes[0];
    assert.equal(node.events[0].type, 'dialogue');
    assert.equal(node.events[1].commands.length, 5);
    text = await edit(text, { type: 'deleteEvent', node: 'start', command: node.events[1].start }, 'sevenbridge');
    assert(!text.includes('*load ')); assert(text.includes('second'));
});
test('invalid source disables mutations and malformed editor comments remain ordinary comments', async () => {
    const text = g.newScene('forest').replace('*goto _editor_stop', '*missing');
    const doc = await inspect(text, tool);
    assert(!g.graph(text, doc).editable);
    assert.throws(() => g.mutate(text, doc, { type: 'move', node: 'start', x: 0, y: 0 }), /source errors/);
    for (const value of ['null', '[]', 'false', '{invalid']) {
        const raw = g.newScene('forest').replace(/;@editor-node .+/, ';@editor-node ' + value);
        const node = g.graph(raw, await inspect(raw, tool)).nodes[0];
        assert(!node.metadata); assert.equal(node.id, 'start');
    }
});
test('static preview follows known context, honors clears, colors, and warns on merges/cycles', async () => {
    const body = g.event('background', { image: 12, x: 4, y: 5 }, 'forest') + g.event('sprite', { image: 13 }, 'forest') + g.event('dialogue', { speaker: 2, text: '^cyYellow^cgGreen^cwWhite\nline' }, 'forest');
    let text = g.newScene('forest').replace('*goto _editor_stop', body + '*goto _editor_stop');
    let state = staticState(text, await inspect(text, tool), 'start');
    assert.deepEqual(state.layers.map(l => [l.layer, l.folder, l.id]), [[10, 'grpe', 12], [30, 'grpo', 13]]);
    assert.equal(state.dialogue.speaker, 2);
    assert.deepEqual(state.dialogue.runs.slice(0, 3).map(r => r.color), ['#FFDE00', '#D7FFB3', '#FFFFFF']);
    text = text.replace('*goto _editor_stop', '*cls 30 0\n*txcls 0\n*goto _editor_stop');
    state = staticState(text, await inspect(text, tool), 'start');
    assert.equal(state.layers.length, 1); assert.equal(state.dialogue, null);
    const merge = text + ':extra\n*goto _editor_stop\n';
    assert.match(staticState(merge, await inspect(merge, tool), g.stop).warnings.join(), /Multiple predecessors/);
    assert(!staticState(merge, await inspect(merge, tool), g.stop, 'start').warnings.length);
    const cycle = text.replace('*goto _editor_stop', '*goto start');
    assert.match(staticState(cycle, await inspect(cycle, tool), 'start').warnings.join(), /Cycle/);
    assert.equal(textRuns('plain').speaker, 999);
});
