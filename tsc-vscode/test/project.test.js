const { test, after } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const p = require('../src/project');
const g = require('../src/graph');
const { runTool } = require('../src/tool');
const { wav } = require('./media-fixture');
const tool = process.env.LIARSOFTTOOL_PATH || path.resolve(__dirname, '../../build/liarsofttool');
const temp = fs.mkdtemp(path.join(os.tmpdir(), 'tsc-project-'));
after(async () => fs.rm(await temp, { recursive: true, force: true }));
const png = Buffer.from('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+a2ioAAAAASUVORK5CYII=', 'base64');
async function fixture(engine) {
    const root = await fs.mkdtemp(path.join(await temp, engine)), resources = path.join(root, 'game');
    await fs.mkdir(resources);
    return p.createProject(path.join(root, '中 文 story'), engine, resources, engine === 'forest' ? 1000 : 2001);
}
test('new projects build both native containers; independent outputs and originals remain unchanged', async () => {
    for (const engine of ['forest', 'sevenbridge']) {
        const project = await fixture(engine), scene = engine === 'forest' ? '1000' : '2001';
        const file = path.join(project.root, 'scr', scene + '.tsc');
        const text = g.newScene(engine).replace('*goto _editor_stop', g.event('dialogue', { text: 'Hello' }, engine) + '*goto _editor_stop');
        await fs.writeFile(file, text);
        const image = path.join(await temp, 'art.png'); await fs.writeFile(image, png);
        await p.importAsset(project, image, 'background', 'My background', 12);
        const result = await p.buildProject(project, tool);
        const output = await fs.readFile(path.join(result.output, 'scr', scene + '.gsc'));
        assert.equal(output.readUInt32LE(4), engine === 'forest' ? 28 : 36);
        assert((await fs.stat(path.join(result.output, 'grpe', '0012.wcg'))).size > 0);
        assert.equal(await fs.readFile(file, 'utf8'), text);
        assert.deepEqual(await fs.readdir(project.config.resources), []);
        assert.equal((await p.findProject(file)).config.assets.length, 1);
        const rerun = await p.buildProject(project, tool);
        assert.deepEqual(await fs.readFile(path.join(rerun.output, 'scr', scene + '.gsc')), output);
    }
});
test('path overlap, traversal, symlinks and duplicate resources/registers are rejected', async () => {
    const project = await fixture('forest');
    await assert.rejects(p.safePath(project.root, '../escape'), /escapes/);
    await fs.symlink(project.config.resources, path.join(project.root, 'linked'), 'dir');
    await assert.rejects(p.safePath(project.root, 'linked/file'), /Symlinks/);
    await assert.rejects(p.createProject(path.join(project.config.resources, 'story'), 'forest', project.config.resources, 1000), /separate/);
    project.config.output = 'scr';
    await assert.rejects(p.buildProject(project, tool), /overlaps/);
    project.config.output = 'build';
    project.config.variables = [{ name: 'Score', register: 20 }, { name: 'Other', register: 20 }];
    assert.throws(() => p.validate(project.config), /Duplicate/);
    project.config.variables = []; project.config.assets = [{ kind: 'background', name: 'One', id: 1, source: 'assets/a.png' }, { kind: 'background', name: 'Two', id: 1, source: 'assets/b.png' }];
    assert.throws(() => p.validate(project.config), /Duplicate/);
    assert.throws(() => p.validate(null), /Invalid/);
});
test('failed source/encoding/call checks never replace a previous build', async () => {
    const project = await fixture('forest'), source = path.join(project.root, 'scr', '1000.tsc');
    const built = await p.buildProject(project, tool), file = path.join(built.output, 'scr', '1000.gsc'), old = await fs.readFile(file);
    for (const body of ['*missing', g.event('dialogue', { text: '😀' }, 'forest'), '*gosub 9999']) {
        await fs.writeFile(source, g.newScene('forest').replace('*goto _editor_stop', body));
        await assert.rejects(p.buildProject(project, tool));
        assert.deepEqual(await fs.readFile(file), old);
    }
    await assert.rejects(p.install(file, 'replace without consent'), { code: 'EEXIST' });
    assert.deepEqual(await fs.readFile(file), old);
    assert(!(await fs.readdir(path.dirname(file))).some(f => f.endsWith('.tmp')));
    const directory = path.join(project.root, 'directory'); await fs.mkdir(directory);
    await assert.rejects(p.install(directory, 'cannot replace directory', true));
    assert(!(await fs.readdir(project.root)).some(f => f.endsWith('.tmp')));
});
test('metadata coordinates and converted resource previews do not change source files', async () => {
    const project = await fixture('sevenbridge'), folder = path.join(project.config.resources, 'grpo', '0001');
    await fs.mkdir(folder, { recursive: true });
    await fs.writeFile(path.join(folder, '.meta.xml'), '<Canvas><Items><Item x="12" y="-4" flag="40">back</Item></Items></Canvas>');
    const image = path.join(folder, 'back.png'); await fs.writeFile(image, png);
    const asset = await p.baseResource(project, 'grpo', 1);
    assert.deepEqual([asset.x, asset.y, asset.firstFrame], [12, -4, true]);
    const wcg = path.join(folder, 'test.wcg');
    assert.equal((await runTool(tool, ['-o', wcg, image])).code, 0);
    const original = await fs.readFile(wcg), time = (await fs.stat(wcg)).mtimeMs;
    const preview = await p.previewFile(wcg, path.join(project.root, 'cache'), tool);
    assert.equal(path.extname(preview), '.png'); assert((await fs.stat(preview)).size > 0);
    assert.deepEqual(await fs.readFile(wcg), original); assert.equal((await fs.stat(wcg)).mtimeMs, time);
    await fs.writeFile(path.join(folder, '.meta.xml'), '<!DOCTYPE Canvas><Canvas/>');
    await assert.rejects(p.baseResource(project, 'grpo', 1), /XML/);
    await assert.rejects(p.baseResource(project, 'grpo', 123), /not found/);
});
test('malformed audio fails rather than copying junk to the exported game', async () => {
    const project = await fixture('forest'), wav = path.join(await temp, 'broken.wav'); await fs.writeFile(wav, 'not audio');
    await p.importAsset(project, wav, 'sound', 'Broken', 4);
    await assert.rejects(p.buildProject(project, tool));
    await assert.rejects(fs.access(path.join(project.root, 'build', 'wav', '0004.wav')));
});
test('valid PCM audio previews and exports unchanged with native grouped voice paths', async () => {
    const project = await fixture('sevenbridge'), file = path.join(await temp, 'tone.wav'), audio = wav(); await fs.writeFile(file, audio);
    await p.importAsset(project, file, 'voice', 'Test voice', 10001);
    const preview = await p.previewFile(file, path.join(project.root, 'audio-cache'), tool);
    assert.deepEqual(await fs.readFile(preview), audio);
    const result = await p.buildProject(project, tool);
    assert.deepEqual(await fs.readFile(path.join(result.output, 'voice', '1', '0001.wav')), audio);
});
