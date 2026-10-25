const { test, after } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const { activate } = require('../src/extension');
const temporary = fs.mkdtemp(path.join(os.tmpdir(), 'tsc-editor-'));
after(async () => fs.rm(await temporary, { recursive: true, force: true }));
function editor(source) {
    const commands = {}, events = {}, entries = new Map(), warnings = [], errors = [], logs = [], replies = [];
    const config = { path: process.env.LIARSOFTTOOL_PATH || path.resolve(__dirname, '../../build/liarsofttool'), encoding: 'CP932', outputDirectory: 'output folder' };
    const doc = {
        uri: { fsPath: source, scheme: 'file', toString: () => source }, languageId: 'codex-tsc',
        isDirty: false, isClosed: false, version: 1, text: ';@gsc-byte-format modern-36\n;@gsc-schema modern\n*missing',
        get lineCount() { return this.text.split('\n').length; },
        lineAt(line) { return { text: this.text.split('\n')[line], range: { line } }; },
        async save() { await fs.writeFile(source, this.text); this.isDirty = false; return true; }
    };
    const disposable = { dispose() {} };
    const vscode = {
        l10n: { t: text => text }, Range: class { constructor(...values) { this.values = values; } },
        Diagnostic: class { constructor(range, message, severity) { Object.assign(this, { range, message, severity }); } },
        DiagnosticSeverity: { Error: 0, Warning: 1 },
        languages: { createDiagnosticCollection: () => ({ ...disposable, set: (uri, value) => entries.set(uri.toString(), value), delete: uri => entries.delete(uri.toString()) }) },
        window: {
            registerCustomEditorProvider: () => disposable,
            activeTextEditor: { document: doc },
            createOutputChannel: () => ({ ...disposable, appendLine: text => logs.push(text), show() {} }),
            showWarningMessage: async text => { warnings.push(text); return replies.shift(); },
            showErrorMessage: text => errors.push(text), showInformationMessage() {}
        },
        commands: { registerCommand: (id, callback) => { commands[id] = callback; return disposable; } },
        workspace: {
            isTrusted: true, textDocuments: [], getConfiguration: () => ({ get: key => config[key] })
        }
    };
    for (const name of ['onDidOpenTextDocument', 'onDidSaveTextDocument', 'onDidChangeTextDocument', 'onDidCloseTextDocument', 'onDidGrantWorkspaceTrust'])
        vscode.workspace[name] = callback => { events[name] = callback; return disposable; };
    const context = { subscriptions: [] }; activate(context, vscode);
    return { commands, events, entries, doc, config, vscode, warnings, errors, replies, logs, context };
}
test('Problems, save prompts, compilation, missing tool and workspace trust', async () => {
    const source = path.join(await temporary, '中文 scenario.tsc');
    const e = editor(source); await e.doc.save();
    await e.commands['liarsofttool.checkTsc']();
    assert.equal(e.entries.get(source)[0].range.line, 2);
    assert.match(e.entries.get(source)[0].message, /unknown command/);
    e.doc.isDirty = true;
    e.events.onDidChangeTextDocument({ document: e.doc });
    assert.equal(e.entries.size, 0);
    await e.commands['liarsofttool.compileTsc']();
    await assert.rejects(fs.access(path.join(await temporary, 'output folder')));
    e.doc.text = ';@gsc-byte-format modern-36\n;@gsc-schema modern\n*end';
    e.replies.push('Save');
    await e.commands['liarsofttool.compileTsc']();
    assert.equal(e.doc.isDirty, false);
    const gsc = path.join(await temporary, 'output folder', '中文 scenario.gsc');
    assert.equal((await fs.readFile(gsc)).readUInt32LE(4), 36);
    e.config.path = path.join(await temporary, 'missing compiler');
    await e.commands['liarsofttool.checkTsc']();
    assert.equal(e.errors.length, 1); assert.match(e.logs.join('\n'), /ENOENT/);
    e.vscode.workspace.isTrusted = false;
    await e.commands['liarsofttool.checkTsc']();
    await e.commands['liarsofttool.compileTsc']();
    e.events.onDidOpenTextDocument(e.doc);
    assert.equal(e.errors.length, 1); assert.match(e.warnings.at(-1), /Trust/);
    for (const item of e.context.subscriptions) item.dispose();
});
