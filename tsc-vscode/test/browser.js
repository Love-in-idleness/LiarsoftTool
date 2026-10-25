// Local UI harness: the real provider/compiler, with an isolated temporary SevenBridge project.
const http = require('node:http');
const fs = require('node:fs/promises');
const path = require('node:path');
const os = require('node:os');
const crypto = require('node:crypto');
const { createProject, saveProject, importAsset } = require('../src/project');
const { bmp, wav } = require('./media-fixture');
const { register } = require('../src/authoring');
(async () => {
    const temp = await fs.mkdtemp(path.join(os.tmpdir(), 'tsc-browser-')), resources = path.join(temp, 'resources');
    await fs.mkdir(resources);
    const project = await createProject(path.join(temp, 'story'), 'sevenbridge', resources, 2001);
    project.config.variables = [{ name: 'Score', register: 20 }]; project.config.characters = [{ name: 'Narrator', speaker: 999 }]; await saveProject(project);
    const image = path.join(temp, 'background.bmp'), sound = path.join(temp, 'tone.wav');
    await fs.writeFile(image, bmp(800, 600, [35, 60, 90])); await fs.writeFile(sound, wav());
    await importAsset(project, image, 'background', 'Test background', 1);
    await importAsset(project, sound, 'sound', 'Test tone', 1);
    const source = path.join(project.root, 'scr', '2001.tsc'), subscribers = new Set(), mapped = new Map(), handlers = {};
    const uri = file => ({ fsPath: file, scheme: 'file', toString: () => file });
    const doc = { uri: uri(source), languageId: 'codex-tsc', isDirty: false, version: 1, text: await fs.readFile(source, 'utf8'), getText() { return this.text; },
        positionAt: offset => offset, async save() { await fs.writeFile(source, this.text); this.isDirty = false; return true; } };
    const disposable = { dispose() {} }, prefix = '/' + crypto.randomUUID();
    const notify = message => { for (const stream of subscribers) stream.write('data: ' + JSON.stringify(message) + '\n\n'); };
    const vscode = {
        l10n: { t: (text, value) => text.replace('{0}', value) }, env: { language: process.env.TSC_UI_LANGUAGE || 'en' },
        Uri: { file: uri, joinPath: (base, ...parts) => uri(path.join(base.fsPath, ...parts)) }, Range: class { constructor(start, end) { Object.assign(this, { start, end }); } },
        WorkspaceEdit: class { edits = []; replace(file, range, text) { this.edits.push({ range, text }); } }, ProgressLocation: { Notification: 1 },
        commands: { registerCommand: () => disposable, executeCommand: async () => {} },
        window: { registerCustomEditorProvider: () => disposable, withProgress: (_, callback) => callback(), showWarningMessage: async () => 'Save',
            showInformationMessage: message => notify({ type: 'error', message }), showErrorMessage: message => notify({ type: 'error', message }) },
        workspace: { isTrusted: true, textDocuments: [doc], getConfiguration: () => ({ get: () => process.env.LIARSOFTTOOL_PATH || path.resolve(__dirname, '../../build/liarsofttool') }),
            onDidChangeTextDocument: callback => { handlers.change = callback; return disposable; }, applyEdit: async edit => {
                for (const { range, text } of edit.edits.sort((a, b) => b.range.start - a.range.start)) doc.text = doc.text.slice(0, range.start) + text + doc.text.slice(range.end);
                doc.version++; doc.isDirty = true; handlers.change({ document: doc }); return true;
            } }
    };
    const panel = { active: true, onDidChangeViewState: () => disposable, onDidDispose: callback => { handlers.dispose = callback; }, webview: {
        cspSource: 'http://127.0.0.1:*', postMessage: async message => notify(message), onDidReceiveMessage: callback => { handlers.receive = callback; return disposable; },
        asWebviewUri: value => { const id = crypto.randomUUID(); mapped.set(id, value.fsPath); return { toString: () => prefix + '/file/' + id }; }
    } };
    await register({ subscriptions: [], extensionUri: uri(path.resolve(__dirname, '..')), globalStorageUri: uri(path.join(temp, 'cache')) }, vscode).provider.resolveCustomTextEditor(doc, panel);
    const nonce = /script nonce="([^"]+)"/.exec(panel.webview.html)[1];
    const bridge = `<script nonce="${nonce}">window.acquireVsCodeApi=()=>({postMessage:m=>fetch('${prefix}/message',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(m)})});new EventSource('${prefix}/events').onmessage=e=>window.dispatchEvent(new MessageEvent('message',{data:JSON.parse(e.data)}));</script>`;
    const html = panel.webview.html.replace("default-src 'none';", "default-src 'none'; connect-src http://127.0.0.1:*;").replace('<div id="root">', bridge + '<div id="root">');
    const server = http.createServer(async (req, res) => {
        try {
            if (req.headers.origin && req.headers.origin !== `http://127.0.0.1:${server.address().port}`) { res.writeHead(403); res.end(); return; }
            if (req.url === '/favicon.ico') { res.writeHead(204); res.end(); }
            else if (req.url === prefix + '/') { res.setHeader('Content-Type', 'text/html'); res.end(html); }
            else if (req.url === prefix + '/events') {
                res.writeHead(200, { 'Content-Type': 'text/event-stream', 'Cache-Control': 'no-cache' }); res.write(': connected\n\n');
                subscribers.add(res); req.on('close', () => subscribers.delete(res));
            } else if (req.url === prefix + '/message' && req.method === 'POST') {
                let body = ''; for await (const chunk of req) { body += chunk; if (body.length > 1024 * 1024) throw new Error('Request too large'); }
                await handlers.receive(JSON.parse(body)); res.end('ok');
            } else if (req.url === prefix + '/source') { res.setHeader('Content-Type', 'text/plain'); res.end(doc.text); }
            else if (req.url.startsWith(prefix + '/file/')) {
                const file = mapped.get(req.url.slice((prefix + '/file/').length)); if (!file) throw new Error('Unknown resource');
                const type = { '.js': 'application/javascript', '.css': 'text/css', '.png': 'image/png', '.bmp': 'image/bmp', '.ogg': 'audio/ogg', '.wav': 'audio/wav' }[path.extname(file)];
                res.setHeader('Content-Type', type || 'application/octet-stream'); res.end(await fs.readFile(file));
            } else { res.writeHead(404); res.end(); }
        } catch (error) { res.writeHead(500); res.end(error.message); }
    });
    server.listen(0, '127.0.0.1', () => console.log(`http://127.0.0.1:${server.address().port}${prefix}/`));
    const close = async () => { handlers.dispose(); for (const stream of subscribers) stream.end(); server.close(); await fs.rm(temp, { recursive: true, force: true }); process.exit(); };
    process.on('SIGINT', close); process.on('SIGTERM', close);
})().catch(error => { console.error(error); process.exit(1); });
