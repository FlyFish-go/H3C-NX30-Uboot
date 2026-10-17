#!/usr/bin/env node
/* Focused host test for partition-name/capacity display and unchanged boot commands. */
const assert = require('assert');
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const mainPath = path.join(__dirname, '..', 'failsafe', 'fsdata', 'main.js');
const source = fs.readFileSync(mainPath, 'utf8');
const begin = source.indexOf('function bootSlotFirmwareText');
const end = source.indexOf('// Physical button status on RT3000-B', begin);
assert(begin >= 0 && end > begin, 'boot-slot display block not found');
const displayCode = source.slice(begin, end) +
    '\n;globalThis.testBootSlots = bootSlotsManager; globalThis.testFirmwareText = bootSlotFirmwareText;';

class Element {
    constructor(tag) {
        this.tagName = tag;
        this.children = [];
        this.className = '';
        this.hidden = false;
        this.disabled = false;
        this.checked = false;
        this.style = {};
        this.handlers = {};
        this.type = '';
        this._text = '';
        this.onclick = null;
        this.innerHTMLWrites = 0;
    }
    set textContent(value) { this._text = String(value); this.children = []; }
    get textContent() { return this._text + this.children.map(c => c.textContent).join(''); }
    set innerHTML(value) { this.innerHTMLWrites++; this._text = String(value); this.children = []; }
    appendChild(child) { this.children.push(child); return child; }
    querySelector(selector) { return selector === '.card-main-inner' ? this.inner : null; }
    setAttribute(k, v) { this[k] = v; }
    addEventListener(k, fn) { this.handlers[k] = fn; }
    findAll(tag) { return (this.tagName === tag ? [this] : []).concat(...this.children.map(c => c.findAll(tag))); }
}
class FormDataMock {
    constructor() { this.values = {}; }
    append(key, value) { this.values[key] = value; }
}

async function render(slots, language) {
    const section = new Element('section');
    const inner = new Element('div');
    section.inner = inner;
    section.appendChild(inner);
    const commands = [];
    const context = {
        APP_STATE: { lang: language },
        document: {
            body: { classList: { contains: () => false } },
            getElementById: id => id === 'boot_slots' ? section : null,
            createElement: tag => new Element(tag)
        },
        fetch: async (url, options) => {
            if (url === '/boot/slots') {
                return { status: 200, ok: true, json: async () => ({ slots }) };
            }
            if (url === '/boot/default') { return { status: 200, ok: true, json: async () => ({ status: 'ok', default: 'rootfs', can_change: true, locked: false }) }; }
            if (url === '/webterm/exec') {
                const cmd = options.body.values.cmd;
                commands.push(cmd);
                return { ok: true, text: async () => 'accepted' };
            }
            throw new Error('unexpected URL ' + url);
        },
        FormData: FormDataMock
    };
    vm.createContext(context);
    vm.runInContext(displayCode, context);
    await context.testBootSlots.init();
    return { section, inner, commands, context };
}

function findLine(root, prefix) {
    const matches = root.findAll('div').map(node => node.textContent).filter(text => text.startsWith(prefix));
    return matches;
}

(async () => {
    const dual = await render([
        { name: 'rootfs', display_index: 16, physical_index: 1, offset: '0x900000', mtd: 'mtd16', size: '0x2800000', known: true, type: 'OEM', label: 'OEM RT3000 R007', version: 'RT3000 R007',
          firmware_version: 'RT3000 R007', boot_command: 'bootipq mtd99' },
        { name: 'rootfs_1', display_index: 17, physical_index: 2, offset: '0x3100000', mtd: 'mtd17', size: 41943040, known: true, type: 'OEM', label: 'OEM RT3000 R005', version: 'RT3000 R005',
          firmware_version: 'RT3000 R005', boot_command: 'bootipq mtd88' },
        { name: 'untrusted-rootfs', display_index: 18, mtd: 'mtd15', size: '0x2800000', known: true, type: 'OEM', label: 'MUST NOT DISPLAY', firmware_version: 'BAD' }
    ], 'zh-cn');
    const text = dual.inner.textContent;
    const nameCells = dual.inner.findAll('td').filter(cell => cell.className === 'td left');
    assert.strictEqual(nameCells[0]._text, 'rootfs');
    assert.strictEqual(nameCells[1]._text, 'rootfs_1');
    assert(nameCells[1].children[0].textContent.startsWith('物理第 2 区 · 地址范围：0x3100000 – 0x58fffff'));
    assert(nameCells[1].children[1].textContent.startsWith('\u5bb9\u91cf\uff1a'));
    assert(!text.includes('mtd'));
    assert(text.includes('\u5bb9\u91cf\uff1a41,943,040 \u5b57\u8282'));
    assert(!text.includes('MUST NOT DISPLAY'));
    assert(!text.includes('untrusted-rootfs'));
    assert(text.includes('\u56fa\u4ef6\uff1aRT3000 R007'));
    assert(text.includes('\u56fa\u4ef6\uff1aRT3000 R005'));
    assert(!text.includes('\u539f\u56fa\u4ef6'));
    assert.strictEqual(dual.inner.findAll('div').reduce((n, el) => n + el.innerHTMLWrites, 0), 0,
        'version strings must be inserted with textContent, never innerHTML');
    const buttons = dual.inner.findAll('button').filter(button => button.textContent.startsWith('启动 '));
    assert.strictEqual(buttons.length, 2);
    await buttons[0].onclick();
    await buttons[1].onclick();
    assert.deepStrictEqual(dual.commands, ['bootipq rootfs', 'bootipq rootfs_1']);

    assert(text.includes('物理第 1 区 · 地址范围：0x900000 – 0x30fffff'));
    assert(text.includes('物理第 2 区 · 地址范围：0x3100000 – 0x58fffff'));

    const notRead = await render([
        { name: 'rootfs', display_index: 16, mtd: 'mtd16', size: '0x2800000', known: true, type: 'OEM', label: 'OEM RT3000 R007',
          firmware_version: 'RT3000 R007' },
        { name: 'rootfs_1', display_index: 17, mtd: 'mtd17', size: 41943040, known: false, type: 'unknown', label: 'Unknown system',
          firmware_version: null }
    ], 'zh-cn');
    assert(!notRead.inner.textContent.includes('\u56fa\u4ef6\uff1a\u7b2c\u4e09\u65b9\u56fa\u4ef6'));

    const qsdk = await render([
        { name: 'rootfs', display_index: 16, mtd: 'mtd16', size: '0x2800000', known: true, type: 'QSDK', label: 'QSDK Product R1 Upgrade Preview',
          version: 'Product R1 Upgrade Preview', firmware_version: null,
          version: 'Product R1 Upgrade Preview' },
        { name: 'rootfs_1', display_index: 17, mtd: 'mtd17', size: 41943040, known: true, type: 'ThirdParty', label: 'Third-party system', detection: 'rootfs_release_file', version: 'OpenWrt 21.02.7 r16847-f8282da11e' }
    ], 'en');
    assert(qsdk.inner.textContent.includes('rootfs'));
    assert(qsdk.inner.textContent.includes('rootfs_1'));
    assert(!qsdk.inner.textContent.includes('mtd'));
    assert(qsdk.inner.textContent.includes('Capacity: 41,943,040 bytes'));
    assert(qsdk.inner.textContent.includes('Firmware: Third-party firmware'));
    assert(qsdk.inner.textContent.includes('System: Third-party system (OpenWrt 21.02.7 r16847-f8282da11e)'));
    assert(!qsdk.inner.textContent.includes('Unknown system'));

    console.log('PASS: plain partition names, capacities, OEM/third-party/unresolved firmware rows, release-file version display, safe text rendering, name-derived bootipq commands');
})().catch(error => { console.error(error); process.exitCode = 1; });
