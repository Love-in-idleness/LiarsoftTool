import React, { useState, useEffect, useCallback, useRef } from 'react';
import { createRoot } from 'react-dom/client';
import { ReactFlow, Background, Controls, MiniMap, Handle, Position, applyNodeChanges } from '@xyflow/react';
import '@xyflow/react/dist/style.css';
import './style.css';
import { translator } from './messages';
const vscode = acquireVsCodeApi();
const t = translator(document.documentElement.lang);
const send = value => vscode.postMessage(value);
const ariaLabelConfig = {
    'controls.ariaLabel': t('Control Panel'), 'controls.zoomIn.ariaLabel': t('Zoom In'), 'controls.zoomOut.ariaLabel': t('Zoom Out'),
    'controls.fitView.ariaLabel': t('Fit View'), 'controls.interactive.ariaLabel': t('Toggle Interactivity'), 'minimap.ariaLabel': t('Mini Map'),
    'node.a11yDescription.default': t('Select a node with Enter or Space; use the sidebar to edit.'),
    'edge.a11yDescription.default': t('Select an edge with Enter or Space; use Disconnect to remove it.')
};
function StoryNode({ data, selected }) {
    return <div className={'story-node ' + (selected ? 'selected' : '')} style={{ minHeight: Math.max(115, data.ports.length * 26 + 36) }}>
        <Handle type="target" position={Position.Left} aria-label={t('Input')} />
        <small>{t(data.kind)}</small><strong>{data.title}</strong><code>{data.id}</code>
        {data.summary && <p>{data.summary}</p>}
        {data.ports.map((port, index) => <React.Fragment key={port.id}><span className="port-label" style={{ top: 30 + index * 26 }}>{port.label}</span>
            <Handle type="source" id={port.id} position={Position.Right} style={{ top: 38 + index * 26 }} aria-label={port.label} /></React.Fragment>)}
    </div>;
}
const nodeTypes = { story: StoryNode };
const fieldsByEvent = { dialogue: ['text', 'speaker', 'voice'], background: ['image', 'layer', 'x', 'y'], sprite: ['image', 'layer', 'x', 'y'],
    clear: ['layer'], music: ['track'], stopMusic: [], voice: ['voice'], sound: ['sound'], wait: ['duration'], variable: ['register', 'operation', 'value'], call: ['scene'], return: [] };
const optionCount = command => Math.max(0, Math.min(5, Number(command.tokens[1]?.value) || 0));
const defaults = { dialogue: { speaker: 999, voice: 0 }, background: { image: 1, layer: 10, x: 0, y: 0 }, sprite: { image: 1, layer: 30, x: 0, y: 0 },
    clear: { layer: 30 }, music: { track: 1 }, voice: { voice: 1 }, sound: { sound: 1 }, wait: { duration: 1000 }, variable: { value: 0 }, call: { scene: 2001 } };
function App() {
    const previousIds = useRef(new Set());
    const flowView = useRef(null);
    const [state, setState] = useState(null), [nodes, setNodes] = useState([]), [selected, select] = useState(null), [error, setError] = useState(''),
        [pending, setPending] = useState(true), [visual, setVisual] = useState(null), [predecessor, setPredecessor] = useState(''),
        [event, setEvent] = useState('dialogue'), [fields, setFields] = useState({}), [audio, setAudio] = useState(''), [audioUri, setAudioUri] = useState(''),
        [edge, setEdge] = useState(null), [dark, setDark] = useState(document.body.classList.contains('vscode-dark'));
    useEffect(() => {
        const observer = new MutationObserver(() => setDark(document.body.classList.contains('vscode-dark'))); observer.observe(document.body, { attributes: true, attributeFilter: ['class'] });
        const receive = ({ data }) => {
            if (data.type === 'document') {
                setState(data); setPending(false); setError(data.graph.diagnostics.map(d => d.message).join('\n'));
                const added = previousIds.current.size ? data.graph.nodes.find(n => !previousIds.current.has(n.id))?.id : undefined;
                previousIds.current = new Set(data.graph.nodes.map(n => n.id));
                select(old => added || (data.graph.nodes.some(n => n.id === old) ? old : data.graph.nodes[0]?.id));
            } else if (data.type === 'pending') setPending(true);
            else if (data.type === 'error' || data.type === 'failure') { setError(data.message); setPending(false); if (data.type === 'failure') setState(null); }
            else if (data.type === 'preview') setVisual({ node: data.node, ...data.visual });
            else if (data.type === 'audio') setAudioUri(data.uri);
        };
        window.addEventListener('message', receive); send({ type: 'ready' });
        return () => { observer.disconnect(); window.removeEventListener('message', receive); };
    }, []);
    useEffect(() => {
        if (!state) return;
        setNodes(state.graph.nodes.map(node => {
            const choice = node.commands.find(c => c.opcode === 14), branch = node.commands.some(c => [3, 4].includes(c.opcode));
            const kind = node.flowEnd ? 'End' : choice ? 'Choice' : branch ? 'Condition' : node.meta.kind === 'end' ? 'End' : 'Paragraph';
            const ports = choice ? Array.from({ length: optionCount(choice) }, (_, i) => ({ id: String(i), label: choice.tokens[8 + i]?.value || t(String(i)) })) :
                branch ? [{ id: 'true', label: t('true') }, { id: 'false', label: t('false') }] : kind === 'End' ? [] : [{ id: 'next', label: t('next') }];
            const say = node.commands.find(c => c.opcode === 81);
            return { id: node.id, type: 'story', position: node.position, selected: node.id === selected,
                data: { id: node.id, kind, title: node.meta.title || node.id, ports, summary: say?.tokens[6]?.value.replace(/^\^g\d{3}/, '').slice(0, 60) } };
        }));
    }, [state, selected]);
    useEffect(() => {
        setVisual(null);
        if (state?.config && selected && !pending && !state.readOnly) send({ type: 'preview', version: state.version, node: selected, predecessor: predecessor || undefined });
    }, [state, selected, predecessor, pending]);
    const edit = operation => { if (pending || !state || state.readOnly) return; setPending(true); setError(''); send({ type: 'edit', version: state.version, operation }); };
    const node = state?.graph.nodes.find(n => n.id === selected), disabled = pending || !state || state.readOnly;
    const command = id => send({ type: 'command', command: id });
    const changeToken = (line, index, value) => edit({ type: 'setToken', node: selected, command: line.start, index, value });
    const variableOptions = state?.config?.variables || [];
    const addNode = kind => {
        if (kind === 'condition' && !variableOptions.length) { setError(t('Add a named variable first')); return; }
        edit({ type: 'addNode', kind, title: t(kind[0].toUpperCase() + kind.slice(1)), fields: kind === 'condition' ? { register: variableOptions[0].register } : {} });
    };
    const onNodesChange = useCallback(changes => setNodes(old => applyNodeChanges(changes, old)), []);
    useEffect(() => {
        const frame = requestAnimationFrame(() => flowView.current?.fitView({ padding: 0.2, duration: 150 }));
        return () => cancelAnimationFrame(frame);
    }, [nodes.length]);
    return <main>
        <header><h1>{t('TSC Creator')}</h1>{[['New scene', 'newTscScene'], ['Import', 'importTscAsset'], ['Variable', 'addTscVariable'], ['Character', 'addTscCharacter'], ['Settings', 'tscProjectSettings'], ['Build', 'buildTscProject']].map(([title, id]) =>
            <button key={id} onClick={() => command(id)} disabled={!state?.config || pending}>{t(title)}</button>)}<button onClick={() => send({ type: 'source' })}>{t('Source')}</button></header>
        {error && <div role="alert" className="error">{error}</div>}
        {!state?.config && <div className="notice">{t('No project: create a TSC project from the command palette')}</div>}
        {state?.readOnly && <div className="notice">{t('Fix source errors before graphical editing')}</div>}
        <div className="toolbar">{['paragraph', 'condition', 'choice', 'end'].map(kind => <button key={kind} onClick={() => addNode(kind)} disabled={disabled || !state.config}>{t(kind[0].toUpperCase() + kind.slice(1))} +</button>)}
            <button disabled={disabled || !edge} onClick={() => { const current = state.graph.edges.find(e => e.id === edge); if (current) edit({ type: 'disconnect', node: current.source, handle: current.sourceHandle }); setEdge(null); }}>{t('Disconnect selected edge')}</button>
            <span role="status">{pending ? t('Refreshing…') : state?.config?.profile}</span></div>
        <div className="workspace"><section className="canvas" aria-label={t('Story graph')} onKeyUp={event => {
            if (event.key.startsWith('Arrow') && event.target.closest('.react-flow__node')) {
                const current = flowView.current?.getNode(selected);
                if (current) edit({ type: 'move', node: current.id, x: current.position.x, y: current.position.y });
            }
        }}><ReactFlow nodes={nodes} edges={(state?.graph.edges || []).map(e => ({ ...e, label: t(e.sourceHandle), selected: e.id === edge }))}
            ariaLabelConfig={ariaLabelConfig}
            onInit={instance => { flowView.current = instance; }}
            onSelectionChange={({ nodes, edges }) => { if (nodes.length === 1) select(nodes[0].id); if (edges.length === 1) setEdge(edges[0].id); }}
            nodeTypes={nodeTypes} onNodesChange={onNodesChange} onNodeClick={(_, n) => { select(n.id); setPredecessor(''); setEdge(null); }}
            onEdgeClick={(_, e) => setEdge(e.id)} onNodeDragStop={(_, n) => edit({ type: 'move', node: n.id, x: n.position.x, y: n.position.y })}
            onConnect={e => edit({ type: 'connect', node: e.source, target: e.target, handle: e.sourceHandle })}
            nodesDraggable={!disabled} nodesConnectable={!disabled} deleteKeyCode={null} fitView onlyRenderVisibleElements colorMode={dark ? 'dark' : 'light'}>
            <Background /><Controls /><MiniMap pannable zoomable /></ReactFlow></section>
        <aside>{!node || state.readOnly ? <p>{t(state?.readOnly ? 'Fix source errors before graphical editing' : 'Select a node')}</p> : <>
            <h2>{node.meta.title || node.id}</h2>
            <label>{t('Title')}<input key={node.id + ':title:' + state.version} defaultValue={node.meta.title || node.id} disabled={disabled || !node.label}
                onBlur={e => { if (e.target.value !== (node.meta.title || node.id)) edit({ type: 'title', node: node.id, title: e.target.value }); }} /></label>
            <label>{t('Label')}<input key={node.id} defaultValue={node.id} disabled={disabled || node.id === '_editor_stop'} onKeyDown={e => {
                if (e.key === 'Enter' && e.target.value !== node.id) edit({ type: 'rename', node: node.id, name: e.target.value });
            }} /><small>{t('Rename label')}: Enter</small></label>
            <button disabled={disabled} onClick={() => edit({ type: 'deleteNode', node: node.id })}>{t('Delete node')}</button>
            {node.commands.filter(c => c.opcode >= 0x4000 && c.opcode < 0xa000 && /^0x[4-9]800$/i.test(c.tokens[1]?.value)).map(c => <section key={c.start}>
                <h3>{t('Condition')}</h3><label>{t('register')}<select value={c.tokens[3].value} disabled={disabled} onChange={e => changeToken(c, 3, e.target.value)}>
                    <option value={c.tokens[3].value}>{variableOptions.find(v => v.register === Number(c.tokens[3].value))?.name || c.tokens[3].value}</option>{variableOptions.filter(v => v.register !== Number(c.tokens[3].value)).map(v => <option key={v.register} value={v.register}>{v.name}</option>)}</select></label>
                <select aria-label={t('Condition')} value={c.opcode >> 12} disabled={disabled} onChange={e => changeToken(c, 1, '0x' + Number(e.target.value).toString(16) + '800')}>
                    {['==', '>=', '>', '<=', '<', '!='].map((op, i) => <option key={op} value={i + 4}>{op}</option>)}</select>
                <label>{t('value')}<input key={state.version} type="number" defaultValue={Number(c.tokens[4].value) > 32767 ? Number(c.tokens[4].value) - 65536 : c.tokens[4].value} disabled={disabled}
                    onBlur={e => { const value = state.config?.profile === 'forest' ? Number(e.target.value) & 65535 : Number(e.target.value); if (value !== Number(c.tokens[4].value)) changeToken(c, 4, String(value)); }} /></label></section>)}
            {node.commands.some(c => c.opcode === 14) && <section><h3>{t('Choice')}</h3>{node.commands.filter(c => c.opcode === 14).map(c => <div key={c.start}>
                <label>{t('Option count')}<select value={c.tokens[1].value} disabled={disabled} onChange={e => changeToken(c, 1, e.target.value)}>{[1, 2, 3, 4, 5].map(n => <option key={n}>{n}</option>)}</select></label>
                <label>{t('text')}<input key={state.version + ':prompt'} defaultValue={c.tokens[2].value} disabled={disabled} onBlur={e => { if (e.target.value !== c.tokens[2].value) changeToken(c, 2, e.target.value); }} /></label>
                {Array.from({ length: optionCount(c) }, (_, i) => <label key={state.version + ':' + i}>{t(String(i))}<input defaultValue={c.tokens[8 + i]?.value} disabled={disabled}
                    onBlur={e => { if (e.target.value !== c.tokens[8 + i].value) changeToken(c, 8 + i, e.target.value); }} /></label>)}</div>)}</section>}
            <h3>{t('Add event')}</h3><select aria-label={t('Add event')} value={event} onChange={e => { setEvent(e.target.value); setFields({ ...defaults[e.target.value], ...(e.target.value === 'call' ? { scene: state.config?.profile === 'forest' ? 1000 : 2001 } : {}) }); }} disabled={disabled || !state.config}>
                {Object.keys(fieldsByEvent).map(id => <option key={id} value={id}>{t(id)}</option>)}</select>
            {fieldsByEvent[event].map(field => <label key={field}>{t(field)}{
                field === 'text' ? <textarea value={fields.text || ''} onChange={e => setFields({ ...fields, text: e.target.value })} /> :
                field === 'register' ? <select value={fields.register ?? ''} onChange={e => setFields({ ...fields, register: Number(e.target.value) })}><option value="">—</option>{variableOptions.map(v => <option key={v.register} value={v.register}>{v.name}</option>)}</select> :
                field === 'speaker' ? <select value={fields.speaker ?? 999} onChange={e => setFields({ ...fields, speaker: Number(e.target.value) })}><option value={999}>{t('Narration')}</option>{state.config?.characters.filter(c => c.speaker !== 999).map(c => <option key={c.speaker} value={c.speaker}>{c.name}</option>)}</select> :
                field === 'operation' ? <select value={fields.operation || 'set'} onChange={e => setFields({ ...fields, operation: e.target.value })}>{['set', 'add', 'subtract'].map(v => <option key={v} value={v}>{t(v)}</option>)}</select> :
                <><input type="number" list={'resources-' + field} value={fields[field] ?? ''} onChange={e => setFields({ ...fields, [field]: e.target.value === '' ? undefined : Number(e.target.value) })} />
                    <datalist id={'resources-' + field}>{state.config?.assets.filter(a => a.kind === ({ image: event, track: 'music', voice: 'voice', sound: 'sound' }[field])).map(a => <option key={a.source} value={a.id}>{a.name}</option>)}</datalist></>}</label>)}
            <button disabled={disabled || !state.config} onClick={() => edit({ type: 'appendEvent', node: node.id, event, fields })}>{t('Add')}</button>
            <h3>{t('Paragraph')}</h3>{node.events?.filter(e => !e.commands.some(c => [3, 4, 5, 8, 14, 200].includes(c.opcode))).map(item => <article key={state.version + ':' + item.start}>
                <strong>{t(item.type)}</strong>{item.commands.filter(c => c.opcode === 81).map(c => <textarea key={c.start} aria-label={t('text')} defaultValue={c.tokens[6].value.replace(/^\^g\d{3}/, '').replaceAll('^n', '\n')} disabled={disabled}
                    onBlur={e => { const prefix = /^\^g\d{3}/.exec(c.tokens[6].value)?.[0] || ''; const value = prefix + e.target.value.replace(/\r?\n/g, '^n'); if (value !== c.tokens[6].value) changeToken(c, 6, value); }} />)}
                <button disabled={disabled} onClick={() => edit({ type: 'moveEvent', node: node.id, command: item.start, direction: 'up' })}>{t('Move up')}</button>{' '}
                <button disabled={disabled} onClick={() => edit({ type: 'moveEvent', node: node.id, command: item.start, direction: 'down' })}>{t('Move down')}</button>{' '}
                <button disabled={disabled} onClick={() => edit({ type: 'deleteEvent', node: node.id, command: item.start })}>{t('Delete')}</button></article>)}
            <details><summary>{t('Raw parameters')}</summary>{node.commands.map(c => <article key={c.start}><code>{c.tokens[0]?.value}</code>
                {c.tokens.slice(1).map((token, i) => <label key={state.version + ':' + i}>#{i + 1} {token.role}
                    {token.quoted ? <textarea defaultValue={token.value} disabled={disabled} onBlur={e => { if (e.target.value !== token.value) changeToken(c, i + 1, e.target.value); }} /> :
                        <input defaultValue={token.value} disabled={disabled || token.role === 'target'} onBlur={e => { if (e.target.value !== token.value) changeToken(c, i + 1, e.target.value); }} />}</label>)}
                <button disabled={disabled} onClick={() => edit({ type: 'deleteEvent', node: node.id, command: c.start })}>{t('Delete')}</button></article>)}</details>
        </>}</aside></div>
        <section className="preview"><div><h2>{t('Static preview')}</h2><p>{t('Preview is not the original engine')}</p>
            <label>{t('Predecessor')}<select value={predecessor} onChange={e => setPredecessor(e.target.value)}><option value="">{t('Automatic')}</option>{[...new Set((state?.graph.edges || []).filter(e => e.target === selected).map(e => e.source))].map(id => <option key={id}>{id}</option>)}</select></label>
            <label>{t('Imported audio')}<select value={audio} onChange={e => setAudio(e.target.value)}><option value="">—</option>{state?.config?.assets.filter(a => ['music', 'voice', 'sound'].includes(a.kind)).map(a => <option key={a.source} value={a.source}>{a.name}</option>)}</select></label>
            <button disabled={!audio || pending} onClick={() => send({ type: 'audio', version: state.version, asset: audio })}>{t('Listen')}</button>{audioUri && <audio controls src={audioUri} />}
            {visual?.warnings.map((warning, i) => <p className="warning" key={i}>{t(warning)}</p>)}</div>
            <div className="stage-wrap"><div className="stage" style={{ width: 800, height: 600 }}>{visual?.node === selected ? <>
                {visual.layers.filter(l => l.uri).map(l => <img key={l.layer} src={l.uri} alt={t('layer') + ' ' + l.layer} style={{ left: l.x, top: l.y, zIndex: l.depth }} />)}
                {visual.dialogue && <div className="dialogue" style={{ zIndex: 10000 }}>{visual.nameplate && <img src={visual.nameplate} alt={t('speaker')} />}
                    <p>{visual.dialogue.runs.map((run, i) => <span key={i} style={{ color: run.color }}>{run.text}</span>)}</p></div>}</> : <p>{t('No preview yet')}</p>}</div></div>
        </section>
    </main>;
}
createRoot(document.getElementById('root')).render(<App />);
