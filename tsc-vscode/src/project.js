const fs = require('node:fs/promises');
const path = require('node:path');
const crypto = require('node:crypto');
const os = require('node:os');
const { profile, newScene, integer } = require('./graph');
const { runTool } = require('./tool');
const { userError } = require('./messages');
const manifest = 'tsc.project.json';
const encodings = ['CP932', 'GBK', 'CP1251', 'UTF-8', 'GB18030', 'CP950', 'CP949', ...[1250, 1252, 1253, 1254, 1255, 1256, 1257, 1258].map(n => 'CP' + n)];
function within(root, file) { const relative = path.relative(root, file); return relative === '' || (!relative.startsWith('..' + path.sep) && relative !== '..' && !path.isAbsolute(relative)); }
async function safePath(root, relative) {
    if (typeof relative !== 'string' || path.isAbsolute(relative)) throw new Error('Use a relative project path');
    root = await fs.realpath(root);
    const file = path.resolve(root, relative);
    if (!within(root, file)) throw new Error('Path escapes its allowed directory');
    let current = root;
    for (const part of path.relative(root, file).split(path.sep).filter(Boolean)) {
        current = path.join(current, part);
        try { if ((await fs.lstat(current)).isSymbolicLink()) throw new Error('Symlinks are not accepted inside resource or output paths'); }
        catch (error) { if (error.code !== 'ENOENT') throw error; }
    }
    return file;
}
async function install(file, data, overwrite = false) {
    await fs.mkdir(path.dirname(file), { recursive: true });
    const temp = path.join(path.dirname(file), '.' + path.basename(file) + '.' + crypto.randomUUID() + '.tmp');
    try {
        const handle = await fs.open(temp, 'wx');
        try { await handle.writeFile(data); await handle.sync(); } finally { await handle.close(); }
        if (!overwrite) { await fs.link(temp, file); await fs.unlink(temp); }
        else await fs.rename(temp, file);
    } finally { await fs.rm(temp, { force: true }); }
}
function validate(config) {
    if (!config || config.version !== 1 || !encodings.includes(config.encoding) || typeof config.resources !== 'string' || !config.resources || typeof config.output !== 'string' || !config.output ||
        !Array.isArray(config.assets) || !Array.isArray(config.characters) || !Array.isArray(config.variables)) throw new Error('Invalid TSC project configuration');
    profile(config.profile);
    const registers = new Set(), names = new Set();
    for (const variable of config.variables) {
        if (!variable || typeof variable.register !== 'number') throw new Error('Invalid variable');
        integer(variable.register, 16, 6999);
        if (typeof variable.name !== 'string' || !variable.name || registers.has(variable.register) || names.has(variable.name)) throw new Error('Duplicate variable or register');
        registers.add(variable.register); names.add(variable.name);
    }
    const speakers = new Set();
    for (const character of config.characters) {
        if (!character || typeof character.name !== 'string' || !character.name || typeof character.speaker !== 'number' || speakers.has(character.speaker)) throw new Error('Invalid or duplicate character');
        integer(character.speaker, 0, 999); speakers.add(character.speaker);
    }
    const targets = new Set();
    for (const asset of config.assets) {
        if (!asset || !['background', 'sprite', 'music', 'sound', 'voice', 'nameplate'].includes(asset.kind) || typeof asset.name !== 'string' || !asset.name || typeof asset.source !== 'string' || typeof asset.id !== 'number') throw new Error('Invalid asset');
        integer(asset.id, 0, asset.kind === 'nameplate' ? 998 : 32767);
        const target = assetTarget(config, asset);
        if (targets.has(target)) throw new Error('Duplicate asset target'); targets.add(target);
    }
    return config;
}
async function findProject(source) {
    let directory = path.dirname(source);
    for (;;) {
        try { return { root: directory, config: validate(JSON.parse(await fs.readFile(path.join(directory, manifest), 'utf8'))) }; }
        catch (error) { if (error.code !== 'ENOENT') throw error; }
        const parent = path.dirname(directory); if (parent === directory) return null; directory = parent;
    }
}
async function createProject(directory, engine, resources, scene, titles) {
    profile(engine); integer(Number(scene), 0, 32767);
    resources = await fs.realpath(resources);
    directory = path.join(await fs.realpath(path.dirname(directory)), path.basename(directory));
    if (within(resources, directory) || within(directory, resources)) throw new Error('Project must be separate from the resource working copy');
    await fs.mkdir(directory); // Never replace an existing project.
    const config = { version: 1, profile: engine, encoding: 'CP932', resources: path.resolve(resources),
        output: 'build', assets: [], characters: [], variables: [] };
    await install(path.join(directory, manifest), JSON.stringify(config, null, 2) + '\n');
    await install(path.join(directory, 'scr', String(scene).padStart(4, '0') + '.tsc'), newScene(engine, titles));
    return { root: directory, config };
}
async function saveProject(project) {
    validate(project.config);
    await install(await safePath(project.root, manifest), JSON.stringify(project.config, null, 2) + '\n', true);
}
function assetTarget(config, asset) {
    if (asset.kind === 'nameplate') return `grps/gf${String(asset.id).padStart(3, '0')}.wcg`;
    if (asset.kind === 'background' || asset.kind === 'sprite') return `${asset.kind === 'background' ? 'grpe' : 'grpo'}/${String(asset.id).padStart(4, '0')}.wcg`;
    if (asset.kind === 'music') return `bgm/Track${String(asset.id).padStart(2, '0')}.wav`;
    if (asset.kind === 'sound') return `wav/${String(asset.id).padStart(4, '0')}.wav`;
    const group = config.profile === 'sevenbridge' ? Math.floor(asset.id / 10000) : 0;
    return `voice/${group ? group + '/' : ''}${String(group ? asset.id % 10000 : asset.id).padStart(4, '0')}.wav`;
}
async function importAsset(project, source, kind, name, id) {
    const extension = path.extname(source).toLowerCase();
    const image = ['background', 'sprite', 'nameplate'].includes(kind);
    if (!(image ? ['.png', '.jpg', '.jpeg', '.bmp'] : ['.ogg', '.wav']).includes(extension)) throw new Error('Import PNG/JPEG/BMP images or OGG/PCM WAV audio');
    const relative = `assets/${crypto.randomUUID()}${extension}`, asset = { kind, name, id, source: relative };
    const next = { ...project.config, assets: [...project.config.assets, asset] }; validate(next);
    await install(await safePath(project.root, relative), await fs.readFile(source));
    project.config = next; await saveProject(project); return asset;
}
async function inspect(text, executable, options = {}) {
    const result = await runTool(executable, ['--inspect-tsc', '-'], { ...options, input: text, maxOutput: 32 * 1024 * 1024 });
    let data;
    try { data = JSON.parse(result.stdout); } catch { throw new Error('Upgrade LiarsoftTool: --inspect-tsc protocol 1 is required'); }
    if (![0, 1].includes(result.code) || data.version !== 1 || !Array.isArray(data.lines) || !Array.isArray(data.diagnostics) || (result.code && !data.diagnostics.length))
        throw new Error('Upgrade LiarsoftTool: --inspect-tsc protocol 1 is required');
    return data;
}
async function buildProject(project, executable) {
    const { root, config } = project; validate(config);
    const output = await safePath(root, config.output), scripts = await safePath(root, 'scr'), assets = await safePath(root, 'assets');
    const resources = await fs.realpath(path.resolve(root, config.resources));
    for (const input of [scripts, assets, resources])
        if (within(output, input) || within(input, output)) throw new Error('Build output overlaps an input directory');
    const sources = (await fs.readdir(scripts)).filter(name => /^\d{4,5}\.tsc$/i.test(name)).sort();
    if (!sources.length) throw new Error('No numbered TSC scenes in scr');
    const inspected = [];
    for (const name of sources) {
        const source = await safePath(root, path.join('scr', name)), text = await fs.readFile(source, 'utf8');
        const result = await inspect(text, executable);
        if (result.diagnostics.length) throw userError('{0}: {1}', name, result.diagnostics[0].message);
        if (result.byteFormat !== profile(config.profile).byteFormat || (config.profile === 'forest' && result.schema !== 'early')) throw userError('{0}: incompatible engine dialect', name);
        const checked = await runTool(executable, ['--check-tsc', '--diagnostics=json', '-e', config.encoding, source]);
        if (checked.code !== 0) throw new Error(checked.stdout || checked.stderr);
        inspected.push({ name, source, document: result });
    }
    const sceneNumbers = new Set(sources.map(name => Number(path.basename(name, path.extname(name)))));
    for (const item of inspected) for (const line of item.document.lines) {
        if (![12, 15, 200].includes(line.opcode)) continue;
        const number = Number(line.tokens[line.opcode === 200 ? 2 : 1]?.value);
        if (!Number.isInteger(number)) throw userError('{0}: dynamic scene calls must be resolved in source before building', item.name);
        if (!sceneNumbers.has(number)) {
            const name = String(number).padStart(4, '0');
            const present = await Promise.all(['.tsc', '.gsc'].map(async ext => fs.access(await safePath(resources, 'scr/' + name + ext)).then(() => true, () => false)));
            if (!present.some(Boolean)) throw userError('{0}: missing called scene {1}', item.name, name);
        }
    }
    for (const asset of config.assets) await fs.access(await safePath(root, asset.source));
    const logs = [];
    for (const { name, source } of inspected) {
        const destination = await safePath(root, path.join(config.output, 'scr', name.replace(/\.tsc$/i, '.gsc')));
        await fs.mkdir(path.dirname(destination), { recursive: true });
        const result = await runTool(executable, ['-e', config.encoding, '-o', destination, source]);
        if (result.code !== 0) throw new Error(result.stderr); logs.push(result.stdout);
    }
    for (const asset of config.assets) {
        const source = await safePath(root, asset.source), destination = await safePath(root, path.join(config.output, assetTarget(config, asset)));
        await fs.mkdir(path.dirname(destination), { recursive: true });
        let result;
        if (path.extname(source).toLowerCase() === '.wav') {
            const cache = await fs.mkdtemp(path.join(os.tmpdir(), 'tsc-audio-'));
            try {
                result = await runTool(executable, ['--unpack-only', '-o', path.join(cache, 'check.ogg'), source]);
                if (!result.code) await install(destination, await fs.readFile(source), true);
            } finally { await fs.rm(cache, { recursive: true, force: true }); }
        } else result = await runTool(executable, ['-e', config.encoding, '-o', destination, source]);
        if (result.code !== 0) throw new Error(result.stderr); logs.push(result.stdout);
    }
    return { output, logs };
}
async function baseResource(project, folder, id, engine = project.config.profile) {
    const root = await fs.realpath(path.resolve(project.root, project.config.resources));
    const base = folder === 'bgm' ? `bgm/Track${String(id).padStart(2, '0')}` :
        folder === 'voice' && engine === 'sevenbridge' && id >= 10000 ? `voice/${Math.floor(id / 10000)}/${String(id % 10000).padStart(4, '0')}` :
        folder === 'grps' ? `grps/gf${String(id).padStart(3, '0')}` : `${folder}/${String(id).padStart(4, '0')}`;
    for (const ext of ['.png', '.webp', '.jpg', '.bmp', '.wcg', '.lim', '.ogg', '.wav']) {
        const file = await safePath(root, base + ext);
        try { if ((await fs.stat(file)).isFile()) return { file }; } catch (error) { if (error.code !== 'ENOENT') throw error; }
    }
    const meta = await safePath(root, base + '/.meta.xml');
    try {
        const xml = await fs.readFile(meta, 'utf8');
        // LWG metadata is a fixed Canvas/Item format, not a general XML document.
        if (/<!DOCTYPE|<!ENTITY/i.test(xml)) throw new Error('External XML declarations are not allowed');
        const items = [...xml.matchAll(/<Item\s+([^>]*)>([^<]*)<\/Item>/g)].map(match => {
            const attributes = Object.fromEntries([...match[1].matchAll(/(x|y|flag)="(-?\d+)"/g)].map(m => [m[1], Number(m[2])]));
            return { ...attributes, name: match[2] };
        });
        if (!items.length || !Number.isFinite(items[0].x) || !Number.isFinite(items[0].y)) throw new Error('Unsupported LWG metadata');
        const first = items[0];
        if (!/^[A-Za-z0-9_ -]+$/.test(first.name)) throw new Error('Unsupported LWG frame name');
        for (const ext of ['.png', '.webp', '.bmp', '.wcg']) {
            const file = await safePath(root, base + '/' + first.name + ext);
            try { await fs.access(file); return { file, x: first.x, y: first.y, firstFrame: true }; } catch (error) { if (error.code !== 'ENOENT') throw error; }
        }
    } catch (error) { if (error.code !== 'ENOENT') throw error; }
    throw userError('Resource not found: {0}', base);
}
async function previewFile(file, cache, executable) {
    const ext = path.extname(file).toLowerCase();
    if (['.png', '.jpg', '.jpeg', '.webp', '.ogg'].includes(ext)) return file;
    await fs.mkdir(cache, { recursive: true });
    const destination = path.join(cache, crypto.randomUUID() + (ext === '.wav' ? '.ogg' : ext === '.bmp' ? '.bmp' : ext === '.lim' ? '.webp' : '.png'));
    const result = await runTool(executable, ['--unpack-only', '-o', destination, file]);
    if (result.code !== 0) throw new Error(result.stderr);
    try { await fs.access(destination); return destination; }
    catch { if (ext === '.wav') return file; throw new Error('Preview conversion produced no image'); }
}
module.exports = { manifest, encodings, within, safePath, install, validate, findProject, createProject, saveProject, importAsset,
    assetTarget, inspect, buildProject, baseResource, previewFile };
