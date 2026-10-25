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
test('Chinese, Russian and Japanese strings cover manifest and runtime messages', () => {
    const keys = Object.keys(read('package.nls.json')).sort();
    for (const language of ['ru', 'zh-cn', 'ja']) {
        assert.deepEqual(Object.keys(read(`package.nls.${language}.json`)).sort(), keys);
        const strings = read(`l10n/bundle.l10n.${language}.json`);
        const source = ['extension', 'authoring'].map(name => fs.readFileSync(path.join(root, 'src', name + '.js'), 'utf8')).join('\n');
        for (const [, text] of source.matchAll(/\bt\('([^']+)'/g)) assert(strings[text], `Missing ${language}: ${text}`);
        for (const [, text] of source.matchAll(/\b(?:input|choose)\('([^']+)'/g)) assert(strings[text], `Missing ${language}: ${text}`);
    }
});
test('one complete catalog covers frontend, errors and format placeholders in all four languages', () => {
    const { translator, languageId, userError } = require('../src/messages');
    const english = read('l10n/bundle.l10n.en.json'), keys = Object.keys(english).sort();
    const sources = ['src/authoring.js', 'src/extension.js', 'src/graph.js', 'src/project.js', 'src/preview.js', 'src/tool.js', 'webview/editor.jsx']
        .map(file => fs.readFileSync(path.join(root, file), 'utf8')).join('\n');
    for (const [, key] of sources.matchAll(/(?:\bt|new Error|userError|warnings\.add)\('([^']+)'/g)) assert(english[key], `Missing source message: ${key}`);
    for (const language of ['en', 'zh-cn', 'ja', 'ru']) {
        const catalog = read(`l10n/bundle.l10n.${language}.json`), t = translator(language);
        assert.deepEqual(Object.keys(catalog).sort(), keys);
        for (const key of keys) {
            assert(catalog[key].trim(), `${language}: ${key}`);
            assert.deepEqual([...catalog[key].matchAll(/\{\d+\}/g)].map(m => m[0]).sort(), [...key.matchAll(/\{\d+\}/g)].map(m => m[0]).sort(), `${language}: ${key}`);
        }
        assert(!/\{[01]\}/.test(t('Moved node to X {0}, Y {1}', 4, 7)));
        const e = userError('Resource not found: {0}', 'grpo/日本語.png');
        assert(t(e.messageKey, ...e.messageArgs).includes('grpo/日本語.png'));
    }
    assert.equal(languageId('zh-Hans'), 'zh-cn'); assert.equal(languageId('JA_jp'), 'ja');
    assert.equal(languageId('ru-RU'), 'ru'); assert.equal(languageId('fr'), 'en');
    assert.equal(translator('fr')('dialogue'), 'Dialogue');
});
