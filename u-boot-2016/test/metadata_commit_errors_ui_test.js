#!/usr/bin/env node
const assert = require('assert');
const fs = require('fs');
const path = require('path');
const vm = require('vm');
const source = fs.readFileSync(path.join(__dirname, '..', 'failsafe/fsdata/main.js'), 'utf8');
const begin = source.indexOf('const messageBuilder = (() => {');
const end = source.indexOf('// ==============================\n// 统一的上传管理器', begin);
assert(begin >= 0 && end > begin, 'messageBuilder production block missing');
const code = source.slice(begin, end) + '\nglobalThis.testMessageBuilder = messageBuilder;';
const messages = {
    'metadata.error.stale_commit':'token stale or mismatched',
    'metadata.error.staged_invalid':'staged image invalid',
    'metadata.error.bytes_changed':'staged bytes changed',
    'metadata.error.layout_changed':'layout changed',
    'metadata.error.write_failed':'write or readback failed',
    'metadata.error.staging_failed':'staging could not be validated'
};
const context = {
    t:key => messages[key] || key,
    escapeHtml:s => String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;'),
    bytesToHuman:n => String(n)
};
vm.createContext(context); vm.runInContext(code, context);
for (const [type, text] of Object.entries({
    stale_or_mismatched_commit:messages['metadata.error.stale_commit'],
    staged_image_invalid:messages['metadata.error.staged_invalid'],
    staged_bytes_changed:messages['metadata.error.bytes_changed'],
    partition_layout_mismatch:messages['metadata.error.layout_changed'],
    metadata_write_or_readback_failed:messages['metadata.error.write_failed'],
    owned_staging_validation_failed:messages['metadata.error.staging_failed']
})) {
    const rendered = context.testMessageBuilder.buildErrorMessage({type});
    assert(rendered.includes(text), 'missing readable text for ' + type);
    assert(!rendered.includes(type), 'raw backend code leaked for ' + type);
}
for (const key of Object.keys(messages)) {
    assert(source.split(key).length - 1 >= 2, 'missing English/Chinese translation for ' + key);
}
console.log('PASS: all metadata commit failures render localized typed messages');
