const { test, after } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const { runTool } = require('../src/tool');
const tool = process.env.LIARSOFTTOOL_PATH || path.resolve(__dirname, '../../build/liarsofttool');
const header = ';@gsc-byte-format modern-36\n;@gsc-schema modern\n';
const temporary = fs.mkdtemp(path.join(os.tmpdir(), 'tsc-vscode-'));
after(async () => fs.rm(await temporary, { recursive: true, force: true }));

test('argument arrays preserve spaces, Chinese and shell metacharacters', async () => {
    const directory = await temporary, value = '中 文;$(touch INJECTED)';
    const result = await runTool(process.execPath, ['-e', 'console.log(JSON.stringify(process.argv.slice(1)))', value], { cwd: directory });
    assert.deepEqual(JSON.parse(result.stdout), [value]);
    await assert.rejects(fs.access(path.join(directory, 'INJECTED')));
});
test('missing executable and cancelled checks are failures', async () => {
    await assert.rejects(runTool(path.join(await temporary, 'missing-tool'), []), { code: 'ENOENT' });
    const controller = new AbortController();
    const running = runTool(process.execPath, ['-e', 'setInterval(() => {}, 1000)'], { signal: controller.signal });
    controller.abort();
    await assert.rejects(running, { name: 'AbortError' });
});
test('large tool output is bounded', async () => {
    await assert.rejects(runTool(process.execPath, ['-e', "process.stdout.write('x'.repeat(9*1024*1024))"]), /too large/);
});
test('real compiler checks without writing and compiles saved source', async () => {
    const directory = await temporary;
    const source = path.join(directory, '中 文 ; script.tsc'), output = path.join(directory, 'result.gsc');
    await fs.writeFile(source, header + '*goto missing\n');
    await fs.writeFile(output, 'PREVIOUS');
    const time = (await fs.stat(output)).mtimeMs;
    const checked = await runTool(tool, ['--check-tsc', '--diagnostics=json', '-e', 'CP932', source]);
    assert.equal(checked.code, 1);
    const [diagnostic] = JSON.parse(checked.stdout).diagnostics;
    assert.equal(diagnostic.file, source); assert.equal(diagnostic.line, 3); assert.equal(diagnostic.column, null);
    const failed = await runTool(tool, ['-e', 'CP932', '-o', output, source]);
    assert.equal(failed.code, 1); assert.equal(await fs.readFile(output, 'utf8'), 'PREVIOUS');
    assert.equal((await fs.stat(output)).mtimeMs, time);
    await fs.writeFile(source, header + '*end\n');
    const valid = await runTool(tool, ['--check-tsc', '--diagnostics=json', '-e', 'CP932', source]);
    assert.equal(valid.code, 0); assert.deepEqual(JSON.parse(valid.stdout), { diagnostics: [] });
    assert.equal(await fs.readFile(output, 'utf8'), 'PREVIOUS');
    const compiled = await runTool(tool, ['-e', 'CP932', '-o', output, source]);
    assert.equal(compiled.code, 0); assert.equal((await fs.readFile(output)).readUInt32LE(4), 36);
});
