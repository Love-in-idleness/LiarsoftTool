const vscode = require('vscode');
const fs = require('node:fs/promises');
const path = require('node:path');
const assert = require('node:assert/strict');

async function run() {
    const directory = process.env.TSC_HOST_FIXTURES;
    assert(directory, 'TSC_HOST_FIXTURES is required');
    const source = path.join(directory, '中文 path', 'scene.tsc');
    await fs.mkdir(path.dirname(source), { recursive: true });
    await fs.writeFile(source, ';@gsc-byte-format modern-36\n;@gsc-schema modern\n*missing\n');
    const config = vscode.workspace.getConfiguration('liarsofttool');
    await config.update('path', process.env.LIARSOFTTOOL_PATH, vscode.ConfigurationTarget.Global);
    await config.update('outputDirectory', 'output', vscode.ConfigurationTarget.Global);
    const extension = vscode.extensions.getExtension('love-in-idleness.liarsofttool-tsc');
    assert(extension, 'Development extension not discovered');
    await extension.activate();
    const { languageId, translator } = require('../src/messages');
    const language = languageId(vscode.env.language);
    if (process.env.TSC_HOST_LANGUAGE) assert.equal(language, languageId(process.env.TSC_HOST_LANGUAGE));
    assert.equal(vscode.l10n.t('Save'), translator(language)('Save'));
    const doc = await vscode.workspace.openTextDocument(source);
    await vscode.window.showTextDocument(doc);
    assert.equal(doc.languageId, 'codex-tsc');
    await vscode.commands.executeCommand('liarsofttool.checkTsc');
    const diagnostics = vscode.languages.getDiagnostics(doc.uri);
    assert(diagnostics.some(d => d.range.start.line === 2 && d.message.includes('unknown command')));
    const edit = new vscode.WorkspaceEdit();
    edit.replace(doc.uri, doc.lineAt(2).range, '*end');
    await vscode.workspace.applyEdit(edit);
    await doc.save();
    await vscode.commands.executeCommand('liarsofttool.checkTsc');
    assert.equal(vscode.languages.getDiagnostics(doc.uri).length, 0);
    await vscode.commands.executeCommand('liarsofttool.compileTsc');
    assert.equal((await fs.readFile(path.join(directory, '中文 path', 'output', 'scene.gsc'))).readUInt32LE(4), 36);
    /* Graphical authoring is disabled.
    const resources = path.join(directory, 'resources'); await fs.mkdir(resources, { recursive: true });
    const project = await require('../src/project').createProject(path.join(directory, 'sevenbridge-' + process.pid), 'sevenbridge', resources, 2001);
    const scene = vscode.Uri.file(path.join(project.root, 'scr', '2001.tsc'));
    await vscode.commands.executeCommand('vscode.openWith', scene, 'liarsofttool.graph');
    assert(vscode.window.tabGroups.all.flatMap(g => g.tabs).some(t => t.input instanceof vscode.TabInputCustom && t.input.viewType === 'liarsofttool.graph'));
    await vscode.commands.executeCommand('liarsofttool.buildTscProject');
    assert.equal((await fs.readFile(path.join(project.root, 'build', 'scr', '2001.gsc'))).readUInt32LE(4), 36);
    await vscode.commands.executeCommand('vscode.openWith', scene, 'default');
    assert(vscode.window.activeTextEditor.document.uri.toString() === scene.toString());
    */
    console.log('OK: actual VS Code extension host, diagnostics, compilation; language=' + language);
}
module.exports = { run };
