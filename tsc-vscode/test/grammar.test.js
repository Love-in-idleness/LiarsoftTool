const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const root = path.resolve(__dirname, '..');
const read = file => JSON.parse(fs.readFileSync(path.join(root, file), 'utf8'));
test('current TSC token patterns and metadata, strings, labels, expressions', () => {
    const grammar = read('syntaxes/tsc.tmLanguage.json');
    const matches = (name, text) => new RegExp(grammar.patterns.find(p => p.name.includes(name)).match).test(text);
    assert(matches('annotation', ';@gsc-byte-format modern-36'));
    assert(matches('comment', '; note'));
    assert(matches('control', '*TXT 0 1 0 0 "" "text" 0'));
    assert(matches('name.label.codex', ':L_00002A'));
    assert(matches('label.reference', 'L_00002A'));
    assert(matches('indirect', '@@123'));
    assert(matches('numeric', '-123')); assert(matches('numeric', '0xf001'));
    const strings = grammar.patterns.find(p => p.begin);
    assert(new RegExp(strings.patterns[0].match).test('\\n'));
    assert(new RegExp(strings.patterns[1].match).test('\\q'));
});
test('Chinese and Russian strings cover manifest and runtime messages', () => {
    const keys = Object.keys(read('package.nls.json')).sort();
    for (const language of ['ru', 'zh-cn']) {
        assert.deepEqual(Object.keys(read(`package.nls.${language}.json`)).sort(), keys);
        const strings = read(`l10n/bundle.l10n.${language}.json`);
        const source = fs.readFileSync(path.join(root, 'src/extension.js'), 'utf8');
        for (const [, text] of source.matchAll(/\bt\('([^']+)'/g)) assert(strings[text], `Missing ${language}: ${text}`);
    }
});
