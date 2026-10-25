const catalogs = {
    en: require('../l10n/bundle.l10n.en.json'), 'zh-cn': require('../l10n/bundle.l10n.zh-cn.json'),
    ja: require('../l10n/bundle.l10n.ja.json'), ru: require('../l10n/bundle.l10n.ru.json')
};
function languageId(language = 'en') {
    const base = String(language).toLowerCase().split(/[-_]/)[0];
    return base === 'zh' ? 'zh-cn' : ['en', 'ja', 'ru'].includes(base) ? base : 'en';
}
const format = (text, args) => text.replace(/\{(\d+)\}/g, (match, index) => index < args.length ? String(args[index]) : match);
function translator(language) {
    const catalog = catalogs[languageId(language)];
    return (key, ...args) => format(catalog[key] ?? catalogs.en[key] ?? key, args);
}
function userError(messageKey, ...messageArgs) {
    return Object.assign(new Error(format(messageKey, messageArgs)), { messageKey, messageArgs });
}
module.exports = { languageId, translator, userError };
