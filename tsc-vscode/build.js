// Graphical editor bundling is disabled with its extension entry point.
/*
const fs = require('node:fs/promises');
const path = require('node:path');
require('esbuild').build({ entryPoints: ['webview/editor.jsx'], bundle: true, outdir: 'dist', metafile: true,
    entryNames: 'editor', minify: true, legalComments: 'linked', sourcemap: false, target: 'es2022',
    define: { 'process.env.NODE_ENV': '"production"' } }).then(async result => {
    const packages = new Map();
    for (const input of Object.keys(result.metafile.inputs).filter(name => name.startsWith('node_modules/'))) {
        let directory = path.dirname(input);
        while (directory !== '.') {
            try { const pkg = JSON.parse(await fs.readFile(path.join(directory, 'package.json'), 'utf8')); packages.set(directory, pkg); break; }
            catch (error) { if (error.code !== 'ENOENT') throw error; directory = path.dirname(directory); }
        }
    }
    const notices = [];
    for (const [directory, pkg] of [...packages].sort()) {
        const licenses = (await fs.readdir(directory)).filter(name => /^licen[cs]e(?:\.|$)/i.test(name));
        if (!licenses.length) throw new Error('Missing bundled dependency license: ' + pkg.name);
        notices.push(pkg.name + ' ' + pkg.version + '\n' + (await Promise.all(licenses.map(name => fs.readFile(path.join(directory, name), 'utf8')))).join('\n'));
    }
    await fs.writeFile('dist/THIRD_PARTY_LICENSES.txt', notices.join('\n\n--------------------\n\n'));
}).catch(error => { console.error(error); process.exit(1); });
*/
