#!/usr/bin/env node
const assert = require('assert');
const fs = require('fs');
const path = require('path');
const vm = require('vm');
const source = fs.readFileSync(path.join(__dirname, '..', 'failsafe/fsdata/main.js'), 'utf8');
const start = source.indexOf('function isRt3000BProtectedEnvName(');
const end = source.indexOf('\nfunction t(key)', start);
assert(start >= 0 && end > start, 'production protected-name helper missing');
const context = {};
vm.runInNewContext(source.slice(start, end) + '\nglobalThis.protected = isRt3000BProtectedEnvName;', context);
for (const name of ['bootcmd', 'bootargs', 'bootdelay', 'ethact', 'mtdparts', 'mtdids']) {
    assert.strictEqual(context.protected(name, true), true, name + ' remains protected in B persistent mode');
    assert.strictEqual(context.protected(name, false), false, name + ' remains available on legacy mode');
}
assert.strictEqual(context.protected('ethaddr', true), false, 'ordinary ENV values remain editable');
assert.strictEqual(context.protected('custom_var', true), false, 'unlisted ENV values remain editable');
assert(source.includes('editBtn.disabled = protectedRow;'));
assert(source.includes('deleteBtn.disabled = protectedRow;'));
assert(source.includes('resetBtn.disabled = protectedRow;'));
assert(source.includes('if (els.protectedHint) els.protectedHint.hidden = !bPersistentMode;'));
assert(source.includes('env.mode.protected'));
function parseNotice(key, after) {
    const block = source.slice(after);
    const match = block.match(new RegExp('"' + key + '": ("(?:[^"\\\\]|\\\\.)*"),'));
    assert(match, 'production notice literal missing: ' + key);
    return JSON.parse(match[1]);
}
const englishStart = source.lastIndexOf('        en: {', source.indexOf('        "zh-cn": {', source.indexOf('env.mode.protected')));
const chineseStart = source.lastIndexOf('        "zh-cn": {', source.indexOf('env.mode.protected', source.indexOf('        "zh-cn": {')));
const englishNotice = parseNotice('env.mode.protected', englishStart);
const chineseNotice = parseNotice('env.mode.protected', chineseStart);
for (const name of ['bootcmd', 'bootargs', 'bootdelay', 'ethact', 'mtdparts', 'mtdids']) {
    assert(englishNotice.includes(name), 'English notice names ' + name);
    assert(chineseNotice.includes(name), 'Chinese notice names ' + name);
}
assert(source.includes('info.persistent_write_supported === true'));
assert(source.includes('info.persistent_target === "0:APPSBLENV"'));
console.log('B ENV protected controls and localized notices passed');
