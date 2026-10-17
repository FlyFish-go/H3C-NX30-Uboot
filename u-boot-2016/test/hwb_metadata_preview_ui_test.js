#!/usr/bin/env node
const assert = require('assert');
const fs = require('fs');
const path = require('path');
const vm = require('vm');
const source = fs.readFileSync(path.join(__dirname, '..', 'failsafe/fsdata/main.js'), 'utf8');
const begin = source.indexOf('const messageBuilder = (() => {');
const end = source.indexOf('// ==============================\n// \u7edf\u4e00\u7684\u4e0a\u4f20\u7ba1\u7406\u5668', begin);
assert(begin >= 0 && end > begin, 'messageBuilder production block missing');
const code = source.slice(begin, end) + '\nglobalThis.testMessageBuilder = messageBuilder;';
function localizedValues(key) {
    const marker = `"${key}":`;
    return source.split(/\r?\n/)
        .filter(line => line.trim().startsWith(marker))
        .map(line => JSON.parse(line.trim().slice(marker.length).replace(/,$/, '')));
}
const layoutTexts = localizedValues('metadata.error.layout_changed');
const stagingTexts = localizedValues('metadata.error.staging_unavailable');
const targetTexts = localizedValues('label.target');
const secondTargetTexts = localizedValues('label.second_target');
assert(layoutTexts.length === 2 && stagingTexts.length === 2, 'bilingual error translations missing');
assert(targetTexts.length === 2 && secondTargetTexts.length === 2, 'bilingual target labels missing');
const en = {
    'error.title': 'Error',
    'label.target': targetTexts[0],
    'label.second_target': secondTargetTexts[0],
    'metadata.error.layout_changed': layoutTexts[0],
    'metadata.error.staging_unavailable': stagingTexts[0]
};
const zh = {
    'error.title': '\u9519\u8bef',
    'label.target': targetTexts[1],
    'label.second_target': secondTargetTexts[1],
    'metadata.error.layout_changed': layoutTexts[1],
    'metadata.error.staging_unavailable': stagingTexts[1]
};
const context = {
    lang: 'en',
    t(key) { return (context.lang === 'zh' ? zh : en)[key] || key; },
    escapeHtml(value) {
        return String(value).replace(/[&<>"']/g, ch => ({
            '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'
        })[ch]);
    },
    bytesToHuman(value) { return value + ' B'; }
};
vm.createContext(context);
vm.runInContext(code, context);
const builder = context.testMessageBuilder;

let html = builder.buildSuccessMessage({
    name: 'bootloader.elf', type: 'ELF', size: 2048, md5: '0123456789abcdef',
    target: '0:APPSBL', target_offset: '0x00500000', target_size: '0x00140000',
    second_target_offset: '0x00000000', second_target_size: '0x00000000'
});
assert(html.includes('Target</td>') && html.includes('0:APPSBL (0x500000 + 0x140000)'), 'single target extent missing');
assert(!html.includes('Second target'), 'zero-sized second target must be omitted');

html = builder.buildSuccessMessage({target:'0:APPSBL + 0:APPSBL_1',target_offset:'0x640000',target_size:'0x140000',second_target_offset:'0x500000',second_target_size:'0x140000'});
assert(html.includes('0:APPSBL (0x640000 + 0x140000)'));
assert(html.includes('0:APPSBL_1 (0x500000 + 0x140000)'));
html = builder.buildErrorMessage({type:'uboot_update_partial',target:'0:APPSBL_1'});
assert(html.includes('/uboot/backup/appsbl.bin') && html.includes('/uboot/backup/appsbl_1.bin'));
html = builder.buildSuccessMessage({
    name: 'cdt.bin', type: 'CDT', size: 512, md5: 'abcdef',
    target: '0:CDT + 0:CDT_1', target_offset: '0x00400000', target_size: '0x00040000',
    second_target_offset: '0x00440000', second_target_size: '0x00040000'
});
assert(html.includes('0:CDT (0x400000 + 0x40000)'), 'first CDT target extent missing');
assert(html.includes('0:CDT_1 (0x440000 + 0x40000)'), 'second CDT target extent missing');
assert((html.match(/class="info-label" data-i18n="label\.(?:target|second_target)"/g) || []).length === 2,
    'CDT should render exactly two target rows');

html = builder.buildSuccessMessage({
    target: '<img src=x onerror="alert(1)">', target_offset: '0x10', target_size: '0x20'
});
assert(!html.includes('<img'), 'target name must be escaped');
assert(html.includes('&lt;img src=x onerror=&quot;alert(1)&quot;&gt;'), 'escaped target name missing');

for (const [offset, size] of [
    ['0x10junk', '0x20'], ['-1', '0x20'], ['Infinity', '0x20'], [1.5, 32],
    [Number.MAX_SAFE_INTEGER + 1, 32], ['0x10', 0], ['0x10', -1], ['0x10', 2.5]
]) {
    html = builder.buildSuccessMessage({target: '0:ART', target_offset: offset, target_size: size});
    assert(!html.includes('(0x'), 'invalid extent must not render hex: ' + String(offset) + '/' + String(size));
    assert(html.includes('0:ART'), 'logical target remains readable when extent is invalid');
}
html = builder.buildSuccessMessage({target: '0:ART', target_offset: 0, target_size: 16});
assert(html.includes('0:ART (0x0 + 0x10)'), 'valid numeric extent should render');

html = builder.buildSuccessMessage({name: 'legacy.bin', type: 'ART', size: 12, md5: 'deadbeef'});
assert((html.match(/<tr>/g) || []).length === 4, 'legacy preview row count changed');
assert(!html.includes('label.target') && !html.includes('Second target'), 'legacy preview gained target UI');
assert(html.includes('legacy.bin') && html.includes('ART') && html.includes('12 B (12 Bytes)') && html.includes('deadbeef'),
    'legacy preview fields changed');

for (const [language, expected] of [
    ['en', [en['metadata.error.layout_changed'], en['metadata.error.staging_unavailable']]],
    ['zh', [zh['metadata.error.layout_changed'], zh['metadata.error.staging_unavailable']]]
]) {
    context.lang = language;
    for (const [type, text] of [
        ['partition_layout_mismatch', expected[0]],
        ['metadata_staging_unavailable', expected[1]]
    ]) {
        const rendered = builder.buildErrorMessage({type});
        assert(rendered.includes(text), language + ' readable text missing for ' + type);
        assert(!rendered.includes(type), 'raw error type leaked: ' + type);
    }
}
assert(source.split('metadata.error.staging_unavailable').length - 1 >= 3,
    'staging-unavailable map plus bilingual translations missing');
console.log('PASS: metadata preview targets, strict extents, legacy display, and bilingual errors');
