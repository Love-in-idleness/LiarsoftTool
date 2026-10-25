const { graph } = require('./graph');
function textRuns(text) {
    const speaker = /^\^g(\d{3})/.exec(text); if (speaker) text = text.slice(5);
    const runs = []; let color = '#FFFFFF', at = 0;
    const controls = /\^c([ygwk])|\^n/g; let match;
    while ((match = controls.exec(text))) {
        if (match.index > at) runs.push({ text: text.slice(at, match.index), color });
        if (match[0] === '^n') runs.push({ text: '\n', color });
        else color = { y: '#FFDE00', g: '#D7FFB3', w: '#FFFFFF', k: '#000000' }[match[1]];
        at = controls.lastIndex;
    }
    runs.push({ text: text.slice(at), color }); return { runs, speaker: speaker ? Number(speaker[1]) : 999 };
}
function staticState(text, document, nodeId, predecessor) {
    const g = graph(text, document), layers = new Map(), folders = new Map(), depth = new Map(), anchors = new Map(), warnings = new Set(), path = [], seen = new Set();
    let current = g.nodes.find(n => n.id === nodeId), dialogue = null;
    while (current) {
        if (seen.has(current.id)) { warnings.add('Cycle: inherited visual state is unknown'); break; }
        seen.add(current.id); path.unshift(current);
        const parents = [...new Set(g.edges.filter(e => e.target === current.id).map(e => e.source))];
        const selected = current.id === nodeId ? predecessor : undefined;
        if (selected && !parents.includes(selected)) throw new Error('Invalid preview predecessor');
        if (parents.length > 1 && !parents.includes(selected)) { warnings.add('Multiple predecessors: select a preview context'); break; }
        current = g.nodes.find(n => n.id === (selected || parents[0]));
    }
    for (const node of path) for (const command of node.commands) {
        const args = command.tokens.slice(1).map(t => t.value), nums = args.map(Number);
        const numeric = (...indices) => indices.every(i => Number.isFinite(nums[i]));
        switch (command.opcode) {
        case 121:
            if (numeric(0)) folders.set(nums[0], args[1]); break;
        case 40:
            if (numeric(0, 1)) depth.set(nums[0], nums[1]); else warnings.add('Dynamic depth is not simulated'); break;
        case 38:
            if (!numeric(0, 1, 2)) warnings.add('Non-default anchoring is not simulated');
            else for (const layer of nums[0] ? [nums[0]] : Array.from({ length: 49 }, (_, i) => i + 1)) anchors.set(layer, !nums[1] && !nums[2]);
            break;
        case 30:
            if (!anchors.get(nums[0])) { warnings.add('Unknown or non-default anchoring is not simulated'); layers.delete(nums[0]); }
            else if (numeric(0, 1, 2, 3) && (folders.has(nums[0]) || folders.has(0))) {
                layers.set(nums[0], { layer: nums[0], folder: folders.get(nums[0]) || folders.get(0), id: nums[1], x: nums[2], y: nums[3], depth: depth.get(nums[0]) ?? nums[0] });
                if (nums[4] || nums[5]) warnings.add('Effects and color modes are not simulated');
            } else warnings.add('Image with unknown folder or expression is not simulated');
            break;
        case 36:
            if (numeric(0)) { if (!nums[0]) layers.clear(); else layers.delete(nums[0]); } break;
        case 81:
            dialogue = textRuns(args[5]); break;
        case 82:
            if (dialogue) dialogue.runs.push(...textRuns(args[4]).runs); break;
        case 83: dialogue = null; break;
        case 15: case 200:
            layers.clear(); folders.clear(); depth.clear(); anchors.clear(); dialogue = null;
            warnings.add('Scene calls are not simulated; inherited visual state is unknown'); break;
        default:
            if (![3, 4, 5, 8, 12, 13, 14, 15, 16, 26, 28, 60, 61, 62, 63, 64, 66, 67, 68, 69].includes(command.opcode) && command.opcode < 0x1000)
                warnings.add('Some commands are not simulated');
        }
        if ([3, 4].includes(command.opcode)) warnings.add('Local conditional flow is not simulated');
        if ([3, 4, 5, 8, 12, 14, 16].includes(command.opcode)) break;
    }
    return { layers: [...layers.values()].sort((a, b) => a.depth - b.depth), dialogue, warnings: [...warnings] };
}
module.exports = { textRuns, staticState };
