const fs = require('node:fs/promises');
const path = require('node:path');
const crypto = require('node:crypto');
const model = require('./graph');
const projects = require('./project');
const { staticState } = require('./preview');
const { languageId } = require('./messages');
function register(context, vscode) {
    const t = vscode.l10n.t, states = new Set();
    const executable = doc => vscode.workspace.getConfiguration('liarsofttool', doc.uri).get('path');
    const errorText = error => t(error.messageKey || error.message, ...(error.messageArgs || []));
    const fail = error => vscode.window.showErrorMessage(t('TSC editor: {0}', errorText(error)));
    const titles = () => ({ start: t('Start'), end: t('End') });
    const trusted = () => { if (!vscode.workspace.isTrusted) throw new Error(t('Trust this workspace before running LiarsoftTool.')); };
    const active = () => [...states].find(s => s.panel.active);
    const project = async () => {
        const source = active()?.doc.uri.fsPath || vscode.window.activeTextEditor?.document.uri.fsPath;
        const value = source && await projects.findProject(source);
        if (!value) throw new Error(t('Open a scene in a TSC project first.')); return value;
    };
    const input = async (prompt, value, validateInput) => vscode.window.showInputBox({ prompt: t(prompt), value: value === undefined ? undefined : String(value), validateInput });
    async function refreshAll() { for (const state of states) await state.refresh(); }
    async function choose(label, entries) { return vscode.window.showQuickPick(entries, { title: t(label), placeHolder: t(label) }); }
    const commands = {
        async newTscProject() {
            trusted();
            const engine = await choose('Target engine', ['sevenbridge', 'forest'].map(id => ({ label: model.profiles[id].name, id })));
            if (!engine) return;
            const resources = await vscode.window.showOpenDialog({ canSelectFolders: true, canSelectFiles: false, openLabel: t('Unpacked game resource working copy') });
            if (!resources) return;
            const parent = await vscode.window.showOpenDialog({ canSelectFolders: true, canSelectFiles: false, openLabel: t('Parent folder for the new project') });
            if (!parent) return;
            const name = await input('New project folder name', 'my-story', value => /^[A-Za-z0-9_\-\u0080-\uffff]+$/.test(value) && !['.', '..'].includes(value) ? null : t('Use a simple folder name'));
            if (!name) return;
            const scene = await input('First scene number', engine.id === 'forest' ? 1000 : 2001, value => /^\d{4,5}$/.test(value) && Number(value) <= 32767 ? null : t('Choose a number from 0000 to 32767'));
            if (!scene) return;
            const created = await projects.createProject(path.join(parent[0].fsPath, name), engine.id, resources[0].fsPath, scene, titles());
            const uri = vscode.Uri.file(path.join(created.root, 'scr', String(Number(scene)).padStart(4, '0') + '.tsc'));
            await vscode.commands.executeCommand('vscode.openWith', uri, 'liarsofttool.graph');
        },
        async newTscScene() {
            trusted(); const p = await project();
            const number = await input('New scene number', 1001, value => /^\d{4,5}$/.test(value) && Number(value) <= 32767 ? null : t('Choose a number from 0000 to 32767'));
            if (!number) return;
            const file = await projects.safePath(p.root, 'scr/' + String(Number(number)).padStart(4, '0') + '.tsc');
            await projects.install(file, model.newScene(p.config.profile, titles()));
            await vscode.commands.executeCommand('vscode.openWith', vscode.Uri.file(file), 'liarsofttool.graph');
        },
        async editTscGraph() {
            const doc = vscode.window.activeTextEditor?.document || active()?.doc;
            if (!doc || doc.uri.scheme !== 'file') throw new Error(t('Open a local TSC file first.'));
            await vscode.commands.executeCommand('vscode.openWith', doc.uri, 'liarsofttool.graph');
        },
        async buildTscProject() {
            trusted(); const p = await project();
            const dirty = vscode.workspace.textDocuments.filter(d => d.isDirty && projects.within(p.root, d.uri.fsPath));
            if (dirty.length) {
                if (await vscode.window.showWarningMessage(t('Save project files before building?'), { modal: true }, t('Save')) !== t('Save')) return;
                for (const doc of dirty) if (!await doc.save()) return;
            }
            await vscode.window.withProgress({ location: vscode.ProgressLocation.Notification, title: t('Building TSC project') }, async () => {
                const result = await projects.buildProject(p, executable(active()?.doc || vscode.window.activeTextEditor.document));
                vscode.window.showInformationMessage(t('Build written: {0}', result.output));
            });
        },
        async importTscAsset() {
            trusted(); const p = await project();
            const kind = await choose('Resource kind', ['background', 'sprite', 'music', 'sound', 'voice', 'nameplate'].map(id => ({ label: t(id), id })));
            if (!kind) return;
            const selected = await vscode.window.showOpenDialog({ canSelectFiles: true, canSelectFolders: false, openLabel: t('Import resource') });
            if (!selected) return;
            const name = await input('Resource name', path.basename(selected[0].fsPath)); if (!name) return;
            const id = await input('Resource number (existing numbers replace resources only in exported output)', undefined,
                value => /^\d+$/.test(value) && Number(value) <= (kind.id === 'nameplate' ? 998 : 32767) ? null : t('Invalid resource number'));
            if (id === undefined) return;
            await projects.importAsset(p, selected[0].fsPath, kind.id, name, Number(id)); await refreshAll();
        },
        async addTscVariable() {
            trusted(); const p = await project();
            const name = await input('Variable name'); if (!name) return;
            const number = await input('Register number: choose a free register; original game scripts may already use it', undefined,
                value => /^\d+$/.test(value) && Number(value) >= 16 && Number(value) <= 6999 ? null : t('Choose a register from 16 to 6999'));
            if (number === undefined) return;
            p.config.variables.push({ name, register: Number(number) }); await projects.saveProject(p); await refreshAll();
        },
        async addTscCharacter() {
            trusted(); const p = await project();
            const name = await input('Character name'); if (!name) return;
            const number = await input('Nameplate number (gfNNN); 999 means narration', 999,
                value => /^\d+$/.test(value) && Number(value) <= 999 ? null : t('Choose a number from 0 to 999'));
            if (number === undefined) return;
            if (p.config.characters.some(c => c.speaker === Number(number))) throw new Error(t('This nameplate is already registered'));
            p.config.characters.push({ name, speaker: Number(number) }); await projects.saveProject(p); await refreshAll();
        },
        async tscProjectSettings() {
            const p = await project(); await vscode.commands.executeCommand('vscode.open', vscode.Uri.file(path.join(p.root, projects.manifest)));
        }
    };
    for (const [name, callback] of Object.entries(commands)) context.subscriptions.push(vscode.commands.registerCommand('liarsofttool.' + name, () => Promise.resolve().then(callback).catch(fail)));
    const provider = {
        async resolveCustomTextEditor(doc, panel) {
            if (doc.uri.scheme !== 'file') throw new Error(t('Open a local TSC file first.'));
            const cache = path.join(context.globalStorageUri.fsPath, 'preview', crypto.randomUUID());
            const state = { doc, panel, snapshot: null, project: null, controller: null, timer: null, disposed: false, previewGeneration: 0, resources: new Set() };
            states.add(state);
            const post = message => { if (!state.disposed) return panel.webview.postMessage(message); };
            async function convert(file) {
                if (state.disposed) throw new Error('Editor closed');
                const task = projects.previewFile(file, cache, executable(doc)); state.resources.add(task);
                try { return await task; } finally { state.resources.delete(task); }
            }
            async function refresh() {
                clearTimeout(state.timer); state.controller?.abort();
                const controller = new AbortController(); state.controller = controller;
                const version = doc.version, text = doc.getText();
                try {
                    trusted();
                    const config = await projects.findProject(doc.uri.fsPath);
                    const root = config?.root || path.dirname(doc.uri.fsPath), roots = [context.extensionUri, vscode.Uri.file(root), vscode.Uri.file(cache)];
                    if (config) roots.push(vscode.Uri.file(await fs.realpath(path.resolve(config.root, config.config.resources))));
                    panel.webview.options = { enableScripts: true, localResourceRoots: roots };
                    const inspected = await projects.inspect(text, executable(doc), { signal: controller.signal, cwd: root });
                    if (controller.signal.aborted || doc.version !== version || state.disposed) return;
                    const graph = model.graph(text, inspected);
                    state.snapshot = { text, inspected, graph, version }; state.project = config;
                    await post({ type: 'document', version, graph, config: config?.config, profiles: model.profiles, readOnly: !graph.editable });
                } catch (error) { if (!controller.signal.aborted) { state.snapshot = null; await post({ type: 'failure', message: errorText(error) }); } }
            }
            state.refresh = refresh;
            const subscriptions = [vscode.workspace.onDidChangeTextDocument(event => {
                if (event.document === doc) {
                    state.controller?.abort(); state.snapshot = null;
                    post({ type: 'pending' }); clearTimeout(state.timer); state.timer = setTimeout(refresh, 180);
                }
            }), panel.onDidChangeViewState(event => { if (event.webviewPanel.active) refresh(); }),
            panel.webview.onDidReceiveMessage(async message => {
                try {
                    if (state.disposed) return;
                    if (message.type === 'ready') { await refresh(); return; }
                    if (message.type === 'command') {
                        const allowed = ['newTscScene', 'buildTscProject', 'importTscAsset', 'addTscVariable', 'addTscCharacter', 'tscProjectSettings'];
                        if (!allowed.includes(message.command)) throw new Error('Unknown command');
                        await commands[message.command](); return;
                    }
                    if (message.type === 'source') { await vscode.commands.executeCommand('vscode.openWith', doc.uri, 'default'); return; }
                    trusted();
                    const snapshot = state.snapshot;
                    if (!snapshot || snapshot.version !== doc.version || message.version !== doc.version) throw new Error(t('Source changed; wait for the graph to refresh'));
                    if (message.type === 'edit') {
                        if (state.project && snapshot.inspected.byteFormat !== model.profile(state.project.config.profile).byteFormat) throw new Error('Scene container does not match its project');
                        if (!state.project && ['addNode', 'appendEvent'].includes(message.operation.type)) throw new Error(t('Open a scene in a TSC project first.'));
                        const changes = model.mutate(snapshot.text, snapshot.inspected, message.operation, state.project?.config.profile);
                        const candidate = model.applyChanges(snapshot.text, changes), checked = await projects.inspect(candidate, executable(doc));
                        if (checked.diagnostics.length) throw new Error(checked.diagnostics[0].message);
                        if (doc.version !== snapshot.version || state.disposed) throw new Error(t('Source changed; wait for the graph to refresh'));
                        const edit = new vscode.WorkspaceEdit();
                        for (const change of changes) edit.replace(doc.uri, new vscode.Range(doc.positionAt(model.byteIndex(snapshot.text, change.start)), doc.positionAt(model.byteIndex(snapshot.text, change.end))), change.text);
                        if (!await vscode.workspace.applyEdit(edit)) throw new Error('Source edit was rejected');
                        return;
                    }
                    if (message.type === 'preview') {
                        const generation = ++state.previewGeneration;
                        if (!state.project) throw new Error(t('Open a scene in a TSC project first.'));
                        const visual = staticState(snapshot.text, snapshot.inspected, message.node, message.predecessor);
                        const sourceAsset = async (folder, id) => {
                            const custom = state.project.config.assets.find(a => a.id === id && (folder === 'grps' ? a.kind === 'nameplate' : folder === 'grpe' ? a.kind === 'background' : folder === 'grpo' ? a.kind === 'sprite' : a.kind === folder));
                            return custom ? { file: await projects.safePath(state.project.root, custom.source) } : projects.baseResource(state.project, folder, id);
                        };
                        for (const layer of visual.layers) {
                            try {
                                if (!/^[A-Za-z0-9_]+$/.test(layer.folder)) throw new Error('Unsupported image folder');
                                const asset = await sourceAsset(layer.folder, layer.id);
                                layer.uri = panel.webview.asWebviewUri(vscode.Uri.file(await convert(asset.file))).toString();
                                layer.x += asset.x || 0; layer.y += asset.y || 0;
                                if (asset.firstFrame) visual.warnings.push(t('Only the first LWG frame is previewed'));
                            } catch (error) { visual.warnings.push(errorText(error)); }
                        }
                        if (visual.dialogue && visual.dialogue.speaker !== 999) {
                            try {
                                const asset = await sourceAsset('grps', visual.dialogue.speaker);
                                visual.nameplate = panel.webview.asWebviewUri(vscode.Uri.file(await convert(asset.file))).toString();
                            } catch (error) { visual.warnings.push(errorText(error)); }
                        }
                        if (generation === state.previewGeneration && doc.version === snapshot.version) await post({ type: 'preview', node: message.node, visual });
                    }
                    if (message.type === 'audio') {
                        const asset = state.project?.config.assets.find(a => a.source === message.asset && ['music', 'voice', 'sound'].includes(a.kind));
                        if (!asset) throw new Error('Choose an imported audio resource');
                        const file = await projects.safePath(state.project.root, asset.source);
                        const uri = panel.webview.asWebviewUri(vscode.Uri.file(await convert(file))).toString();
                        if (doc.version === snapshot.version) await post({ type: 'audio', uri });
                    }
                } catch (error) { await post({ type: 'error', message: errorText(error) }); }
            })];
            panel.onDidDispose(() => {
                state.disposed = true; state.controller?.abort(); clearTimeout(state.timer); states.delete(state);
                for (const item of subscriptions) item.dispose();
                // Only this panel's exclusively created preview directory is removed.
                Promise.allSettled([...state.resources]).then(() => fs.rm(cache, { recursive: true, force: true })).catch(() => {});
            });
            const nonce = crypto.randomBytes(18).toString('base64'), webview = panel.webview;
            const uri = file => webview.asWebviewUri(vscode.Uri.joinPath(context.extensionUri, 'dist', file));
            const language = languageId(vscode.env.language);
            webview.html = `<!DOCTYPE html><html lang="${language}"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta http-equiv="Content-Security-Policy" content="default-src 'none'; img-src ${webview.cspSource} data:; media-src ${webview.cspSource}; script-src 'nonce-${nonce}'; style-src ${webview.cspSource} 'unsafe-inline';"><link rel="stylesheet" href="${uri('editor.css')}"></head><body><div id="root"></div><script nonce="${nonce}" src="${uri('editor.js')}"></script></body></html>`;
        }
    };
    context.subscriptions.push(vscode.window.registerCustomEditorProvider('liarsofttool.graph', provider, { supportsMultipleEditorsPerDocument: true }));
    return { activeDocument: () => active()?.doc, provider };
}
module.exports = { register };
