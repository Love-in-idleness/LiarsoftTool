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
    console.log('OK: actual VS Code extension host, diagnostics, saved edit and GSC compilation');
}
module.exports = { run };
