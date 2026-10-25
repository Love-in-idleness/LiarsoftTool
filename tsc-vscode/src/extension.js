const path = require('node:path');
const fs = require('node:fs/promises');
const { runTool } = require('./tool');

function activate(context, vscode = require('vscode')) {
    const t = vscode.l10n.t;
    const authoring = require('./authoring').register(context, vscode);
    const output = vscode.window.createOutputChannel('LiarsoftTool TSC');
    const problems = vscode.languages.createDiagnosticCollection('codex-tsc');
    const checks = new Map(), compiling = new Set();
    const eligible = doc => doc && doc.languageId === 'codex-tsc' && doc.uri.scheme === 'file';
    const settings = doc => vscode.workspace.getConfiguration('liarsofttool', doc.uri);
    function cancel(doc) {
        const key = doc.uri.toString();
        checks.get(key)?.abort(); checks.delete(key); problems.delete(doc.uri);
    }
    function report(error) {
        output.appendLine(String(error.message || error));
        vscode.window.showErrorMessage(t('LiarsoftTool failed. See the LiarsoftTool TSC output for details.'));
        output.show(true);
    }
    async function savedDocument() {
        if (!vscode.workspace.isTrusted) {
            vscode.window.showWarningMessage(t('Trust this workspace before running LiarsoftTool.')); return;
        }
        const doc = authoring.activeDocument() || vscode.window.activeTextEditor?.document;
        if (!eligible(doc)) { vscode.window.showWarningMessage(t('Open a local TSC file first.')); return; }
        if (doc.isDirty) {
            const save = t('Save');
            if (await vscode.window.showWarningMessage(t('Save this file before checking or compiling?'), { modal: true }, save) !== save ||
                !await doc.save()) return;
        }
        return doc;
    }
    async function check(doc) {
        if (!eligible(doc) || doc.isDirty || !vscode.workspace.isTrusted) return;
        cancel(doc);
        const key = doc.uri.toString(), controller = new AbortController();
        checks.set(key, controller);
        const version = doc.version, config = settings(doc);
        try {
            const result = await runTool(config.get('path'), [
                '--check-tsc', '--diagnostics=json', '-e', config.get('encoding'), doc.uri.fsPath
            ], { cwd: path.dirname(doc.uri.fsPath), signal: controller.signal });
            if (checks.get(key) !== controller || doc.version !== version || doc.isClosed) return;
            const data = JSON.parse(result.stdout);
            if (!Array.isArray(data.diagnostics) || ![0, 1].includes(result.code) ||
                (result.code !== 0 && data.diagnostics.length === 0))
                throw new Error(result.stderr || result.stdout || t('The tool did not return TSC diagnostics.'));
            const diagnostics = data.diagnostics.map(item => {
                if (typeof item.message !== 'string' || path.resolve(item.file) !== path.resolve(doc.uri.fsPath))
                    throw new Error(t('The tool did not return TSC diagnostics.'));
                const known = Number.isInteger(item.line) && item.line > 0 && item.line <= doc.lineCount;
                const range = known ? doc.lineAt(item.line - 1).range
                    : new vscode.Range(0, 0, doc.lineCount - 1, doc.lineAt(doc.lineCount - 1).text.length);
                const diagnostic = new vscode.Diagnostic(range, item.message,
                    item.severity === 'warning' ? vscode.DiagnosticSeverity.Warning : vscode.DiagnosticSeverity.Error);
                diagnostic.source = 'LiarsoftTool'; return diagnostic;
            });
            problems.set(doc.uri, diagnostics);
        } catch (error) {
            if (!controller.signal.aborted && checks.get(key) === controller) report(error);
        } finally {
            if (checks.get(key) === controller) checks.delete(key);
        }
    }
    async function compile() {
        const doc = await savedDocument();
        if (!doc) return;
        const key = doc.uri.toString();
        if (compiling.has(key)) return;
        compiling.add(key);
        try {
            const config = settings(doc), source = doc.uri.fsPath;
            const directory = path.resolve(path.dirname(source), config.get('outputDirectory') || '.');
            const destination = path.join(directory, path.basename(source).replace(/\.tsc$/i, '.gsc'));
            if (destination === source) throw new Error(t('Open a local TSC file first.'));
            await fs.mkdir(directory, { recursive: true });
            const result = await runTool(config.get('path'), [
                '-e', config.get('encoding'), '-o', destination, source
            ], { cwd: path.dirname(source) });
            output.appendLine(result.stdout); output.appendLine(result.stderr);
            if (result.code !== 0) throw new Error(t('GSC compilation failed.'));
            vscode.window.showInformationMessage(t('GSC written: {0}', destination));
        } catch (error) { report(error); }
        finally { compiling.delete(key); }
    }
    context.subscriptions.push(output, problems,
        vscode.commands.registerCommand('liarsofttool.checkTsc', async () => {
            const doc = await savedDocument(); if (doc) await check(doc);
        }),
        vscode.commands.registerCommand('liarsofttool.compileTsc', compile),
        vscode.workspace.onDidOpenTextDocument(doc => { void check(doc); }),
        vscode.workspace.onDidSaveTextDocument(doc => { void check(doc); }),
        vscode.workspace.onDidChangeTextDocument(event => { if (eligible(event.document)) cancel(event.document); }),
        vscode.workspace.onDidCloseTextDocument(doc => { if (eligible(doc)) cancel(doc); }),
        vscode.workspace.onDidGrantWorkspaceTrust(() => {
            for (const doc of vscode.workspace.textDocuments) void check(doc);
        }),
        { dispose() { for (const controller of checks.values()) controller.abort(); checks.clear(); } }
    );
    for (const doc of vscode.workspace.textDocuments) void check(doc);
}
module.exports = { activate };
