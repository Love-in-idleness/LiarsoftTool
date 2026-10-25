const { test, after } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const { register } = require('../src/authoring');
const { createProject, inspect } = require('../src/project');
const { translator, languageId } = require('../src/messages');
const temp = fs.mkdtemp(path.join(os.tmpdir(), 'tsc-webview-'));
const tool = process.env.LIARSOFTTOOL_PATH || path.resolve(__dirname, '../../build/liarsofttool');
after(async () => fs.rm(await temp, { recursive: true, force: true }));
for (const language of ['en-US', 'zh-CN', 'ja-JP', 'ru-RU']) test('SevenBridge custom editor and localized errors: ' + language, async () => {
    const root = await fs.mkdtemp(path.join(await temp, 'language-')), resources = path.join(root, 'resources'); await fs.mkdir(resources);
    const t = translator(language);
    const project = await createProject(path.join(root, 'story'), 'sevenbridge', resources, 2001);
    const source = path.join(project.root, 'scr', '2001.tsc'), messages = [], handlers = {}, commands = {}, executed = [];
    const disposable = { dispose() {} };
    const uri = file => ({ fsPath: file, scheme: 'file', toString: () => 'file://' + file });
    const doc = { uri: uri(source), version: 1, text: await fs.readFile(source, 'utf8'), getText() { return this.text; }, positionAt(offset) { return offset; } };
    const vscode = {
        l10n: { t }, env: { language },
        Uri: { file: uri, joinPath: (base, ...parts) => uri(path.join(base.fsPath, ...parts)) },
        Range: class { constructor(start, end) { Object.assign(this, { start, end }); } },
        WorkspaceEdit: class { edits = []; replace(file, range, text) { this.edits.push({ range, text }); } },
        window: { registerCustomEditorProvider: (name, provider) => { handlers.provider = provider; return disposable; }, showErrorMessage: text => { throw new Error(text); } },
        commands: { registerCommand: (name, callback) => { commands[name] = callback; return disposable; }, executeCommand: async (...args) => executed.push(args) },
        workspace: {
            isTrusted: true, getConfiguration: () => ({ get: () => tool }),
            onDidChangeTextDocument: callback => { handlers.change = callback; return disposable; },
            applyEdit: async edit => {
                for (const { range, text } of edit.edits.sort((a, b) => b.range.start - a.range.start)) doc.text = doc.text.slice(0, range.start) + text + doc.text.slice(range.end);
                doc.version++; handlers.change({ document: doc }); return true;
            }
        }
    };
    const panel = { active: true, webview: {
        cspSource: 'vscode-webview:', asWebviewUri: value => value, postMessage: async value => messages.push(value),
        onDidReceiveMessage: callback => { handlers.receive = callback; return disposable; }
    }, onDidChangeViewState: () => disposable, onDidDispose: callback => { handlers.dispose = callback; return disposable; } };
    const context = { subscriptions: [], globalStorageUri: uri(path.join(root, 'cache')), extensionUri: uri(path.resolve(__dirname, '..')) };
    const api = register(context, vscode);
    await api.provider.resolveCustomTextEditor(doc, panel);
    assert(panel.webview.html.includes('lang="' + languageId(language) + '"')); assert.match(panel.webview.html, /script-src 'nonce-/); assert(!panel.webview.html.includes('https://'));
    await handlers.receive({ type: 'ready' });
    assert.equal(messages.at(-1).graph.nodes.length, 2); assert.equal(api.activeDocument(), doc);
    const edit = operation => handlers.receive({ type: 'edit', version: doc.version, operation });
    await edit({ type: 'appendEvent', node: 'start', event: 'dialogue', fields: { text: '日本語 "test"', speaker: 3 } });
    assert.match(doc.text, /\^g003日本語/); assert.equal(doc.version, 2);
    await handlers.receive({ type: 'ready' });
    const checked = await inspect(doc.text, tool), txt = checked.lines.find(l => l.opcode === 81);
    const before = doc.text;
    await edit({ type: 'setToken', node: 'start', command: txt.start, index: 1, value: 'not an expression' });
    assert.equal(doc.text, before); assert.equal(messages.at(-1).type, 'error');
    assert.equal(messages.at(-1).message, t('Invalid numeric operand'));
    await handlers.receive({ type: 'edit', version: 1, operation: { type: 'rename', node: 'start', name: 'stale' } });
    assert.equal(doc.text, before); assert.equal(messages.at(-1).message, t('Source changed; wait for the graph to refresh'));
    await handlers.receive({ type: 'preview', version: doc.version, node: 'start' });
    assert.equal(messages.at(-1).type, 'preview'); assert.equal(messages.at(-1).visual.dialogue.speaker, 3);
    await handlers.receive({ type: 'audio', version: doc.version, asset: '/etc/passwd' });
    assert.equal(messages.at(-1).type, 'error');
    assert.equal(messages.at(-1).message, t('Choose an imported audio resource'));
    await handlers.receive({ type: 'command', command: 'arbitraryCommand' });
    assert.equal(messages.at(-1).type, 'error');
    assert.equal(messages.at(-1).message, t('Unknown command'));
    await handlers.receive({ type: 'source' }); assert.equal(executed.at(-1)[2], 'default');
    vscode.workspace.isTrusted = false;
    await handlers.receive({ type: 'ready' }); assert.equal(messages.at(-1).type, 'failure');
    handlers.dispose(); assert.equal(api.activeDocument(), undefined);
    for (const item of context.subscriptions) item.dispose();
});
