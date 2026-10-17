#!/usr/bin/env node
const assert = require('assert');
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const src = fs.readFileSync(path.join(__dirname, '..', 'failsafe', 'fsdata', 'main.js'), 'utf8');
const start = src.indexOf('function appendSysinfoBoardRows(');
const end = src.indexOf('\nconst sysinfoManager = (() => {', start);
assert(start >= 0 && end > start, 'production board row renderer is present');
const context = {
    formatHexBytes: value => '0x' + Number(value).toString(16),
    rows: [],
    createInfoRow: (label, value) => ({ label, value }),
    table: { appendChild(row) { context.rows.push(row); return row; } }
};
vm.createContext(context);
vm.runInContext(src.slice(start, end) +
    '\nglobalThis.render = board => appendSysinfoBoardRows(table, board, createInfoRow, formatHexBytes);', context);
context.render({ hostname: 'RT3000-B', family: 'HW-B', profile: 'RT3000-B', model: 'RT3000-B',
    compatible: 'vendor,rt3000-b', machid: '1234', ram_size: 536870912, uid: 'not-for-display' });
assert.deepStrictEqual(context.rows.map(row => [row.label, row.value]), [
    ['sysinfo.hostname', 'RT3000-B'], ['sysinfo.family', 'HW-B'],
    ['sysinfo.machid', '1234'],
    ['sysinfo.ram_size', '0x20000000']
]);
assert(!context.rows.some(row => row.label === 'sysinfo.profile'));
context.rows.length = 0;
context.render({ hostname: 'BOOTBRIDGE-HW-D', family: 'HW-D', model: 'H3C HW-D',
    identity_source: 'build_configuration', uid: 'not-for-display' });
assert.deepStrictEqual(context.rows.map(row => [row.label, row.value]), [
    ['sysinfo.build_name', 'H3C HW-D'], ['sysinfo.family', 'HW-D'],
    ['sysinfo.model', 'H3C HW-D']
]);
assert(!context.rows.some(row => row.value === 'BOOTBRIDGE-HW-D'));
context.rows.length = 0;
context.render({ hostname: 'old-sample-name', identity_source: 'build_configuration' });
assert.deepStrictEqual(context.rows.map(row => [row.label, row.value]), [
    ['sysinfo.build_name', 'old-sample-name']
]);
context.rows.length = 0;
context.render({ hostname: 'legacy-host', identity_source: 'unknown' });
assert.deepStrictEqual(context.rows.map(row => [row.label, row.value]), [
    ['sysinfo.hostname', 'legacy-host']
]);
context.rows.length = 0;
context.render({ model: 'legacy-model', compatible: 'legacy,board' });
assert.deepStrictEqual(context.rows.map(row => [row.label, row.value]), [
    ['sysinfo.model', 'legacy-model'], ['sysinfo.compatible', 'legacy,board']
]);
assert(src.includes('"sysinfo.build_name": "Build Configuration Name"'));
assert(src.includes('"sysinfo.build_name": "编译配置名称"'));
assert(src.includes('"sysinfo.family": "Hardware Family"'));
assert(src.includes('"sysinfo.family": "硬件系列"'));
console.log('PASS: compiled board name distinguished from legacy hostname, HW-D generic display, optional family row, ignored profile field, HW-B family-only identity and preserved legacy rows, localized labels, and no UID rendering');
