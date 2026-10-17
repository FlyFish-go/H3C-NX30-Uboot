#!/usr/bin/env node
/* HW-C rootfs classification and default-selection UI contract. */
const assert = require('assert');
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const mainPath = path.join(__dirname, '..', 'failsafe', 'fsdata', 'main.js');
const source = fs.readFileSync(mainPath, 'utf8');
const begin = source.indexOf('function bootSlotFirmwareText');
const end = source.indexOf('// Physical button status on RT3000-B', begin);
assert(begin >= 0 && end > begin, 'boot-slot UI block not found');
const code = source.slice(begin, end) +
    '\n;globalThis.testBootSlots = bootSlotsManager; globalThis.testFirmwareText = bootSlotFirmwareText;';

class Element {
    constructor(tag) {
        this.tagName = tag;
        this.children = [];
        this.className = '';
        this.hidden = false;
        this.disabled = false;
        this.checked = false;
        this.type = '';
        this.style = {};
        this.attributes = {};
        this._text = '';
        this.onclick = null;
        this.listeners = {};
        this.href = '';
        this.download = '';
    }
    set textContent(value) { this._text = String(value); this.children = []; }
    get textContent() { return this._text + this.children.map(c => c.textContent).join(''); }
    appendChild(child) { this.children.push(child); return child; }
    querySelector(selector) { return selector === '.card-main-inner' ? this.inner : null; }
    setAttribute(key, value) { this.attributes[key] = String(value); }
    addEventListener(name, fn) { this.listeners[name] = fn; }
    findAll(tag) { return (this.tagName === tag ? [this] : []).concat(...this.children.map(c => c.findAll(tag))); }
}
class FormDataMock {
    constructor() { this.values = {}; }
    append(key, value) { this.values[key] = value; }
}

async function render(slots, language, options = {}) {
    const section = new Element('section');
    const inner = new Element('div');
    section.inner = inner;
    const requests = [];
    const defaultGets = [];
    const post = options.postResponses ? [...options.postResponses] : [];
    const context = {
        APP_STATE: {
            lang: language,
            sysinfo: { board: { family: options.family || 'HW-C' } }
        },
        document: {
            body: { classList: { contains: () => false } },
            getElementById: id => id === 'boot_slots' ? section : null,
            createElement: tag => new Element(tag)
        },
        fetch: async (url, config = {}) => {
            if (url === '/boot/slots') {
                return { status: 200, ok: true, json: async () => ({ slots, read_only: options.readOnly === true, boot_supported: options.readOnly !== true }) };
            }
            if (url === '/boot/default' && (!config.method || config.method === 'GET')) {
                defaultGets.push(config.cache || 'initial');
                const response = options.defaultResponse;
                return response || { status: 404, ok: false, json: async () => ({}) };
            }
            if (url === '/boot/default' && config.method === 'POST') {
                requests.push({ url, target: config.body.values.target });
                return post.length ? post.shift() : {
                    status: 200, ok: true,
                    json: async () => ({ status: 'ok', default: config.body.values.target })
                };
            }
            if (url === '/webterm/exec') {
                requests.push({ url, command: config.body.values.cmd });
                return { status: 200, ok: true, text: async () => 'accepted' };
            }
            throw new Error('unexpected URL ' + url);
        },
        FormData: FormDataMock
    };
    vm.createContext(context);
    vm.runInContext(code, context);
    await context.testBootSlots.init();
    assert(!inner.textContent.includes('Partition detection failed:') && !inner.textContent.includes('分区信息读取失败：'), 'slot UI must initialize without swallowed errors');
    return { section, inner, requests, defaultGets, context };
}

const response = obj => ({ status: 200, ok: true, json: async () => obj });

(async () => {
    const unknown = await render([
        { name: 'rootfs', known: false, type: 'unknown', detection: 'unknown_rootfs_sha256',
          label: 'Unknown system' },
        { name: 'rootfs_1', known: true, type: 'OEM', firmware_version: 'RT3000 R012',
          dashboard_status: 'not_read' }
    ], 'zh-cn', { family: 'HW-C' });
    assert(unknown.inner.textContent.includes('系统身份：未识别'));
    assert(!unknown.inner.textContent.includes('第三方系统'));
    assert(unknown.inner.textContent.includes('固件：RT3000 R012'));

    const thirdParty = await render([
        { name: 'rootfs', known: true, type: 'OpenWrt',
          version: 'OpenWrt 21.02.7 r16847-f8282da11e', detection: 'rootfs_release_file' }
    ], 'zh-cn', { family: 'HW-C' });
    assert(thirdParty.inner.textContent.includes('系统：第三方系统（OpenWrt 21.02.7 r16847-f8282da11e）'));

    const production = await render([
        { name: 'rootfs', known: false, type: 'unknown', detection: 'unknown_rootfs_sha256' },
        { name: 'rootfs_1', known: true, type: 'OEM', firmware_version: 'RT3000 R012' },
        { name: 'mtd16', known: true, type: 'OEM', firmware_version: 'MUST NOT DISPLAY' }
    ], 'zh-cn', {
        family: 'HW-C',
        defaultResponse: response({
            default: 'rootfs_1', can_change: true, locked: false,
            auto_web: false, default_applies_to: 'bootipq'
        })
    });
    assert(production.inner.textContent.includes('上电将启动已保存的默认分区'));
    assert(production.inner.textContent.includes('上电时按住 RESET 约 3 秒进入维护网页'));
    assert(!production.inner.textContent.includes('MUST NOT DISPLAY'));
    const checks = production.inner.findAll('input');
    assert.strictEqual(checks.length, 2);
    assert.strictEqual(checks[0].checked, false);
    assert.strictEqual(checks[1].checked, true);
    assert(checks.every(input => input.disabled === false));
    checks[0].checked = true;
    await checks[0].listeners.change();
    assert(production.requests.some(item => item.url === '/boot/default' && item.target === 'rootfs'));
    assert.strictEqual(checks[0].checked, true);
    assert.strictEqual(checks[1].checked, false);
    assert.strictEqual(production.inner.findAll('a').length, 2);

    const readonly = await render([
        { name: 'rootfs', known: false, type: 'unknown', detection: 'unknown_rootfs_sha256' }
    ], 'en', {
        readOnly: true, defaultResponse: response({
            default: 'rootfs', can_change: false, locked: true, reason: 'readonly_profile',
            auto_web: true, default_applies_to: 'bootipq'
        })
    });
    assert.strictEqual(readonly.inner.findAll('input').length, 1);
    assert(readonly.inner.findAll('input')[0].disabled);
    assert(readonly.inner.findAll('button').filter(button => button.textContent.startsWith('Boot ')).every(button => button.disabled));
    assert(readonly.inner.textContent.includes('read-only preview cannot save'));

    const incompatible = await render([
        { name: 'rootfs', known: false, type: 'unknown', detection: 'unknown_rootfs_sha256' }
    ], 'en', {
        defaultResponse: response({
            default: 'rootfs_1', can_change: false, locked: true,
            reason: 'bootcmd_incompatible'
        })
    });
    assert(incompatible.inner.findAll('input')[0].disabled);
    assert(incompatible.inner.textContent.includes('does not allow a safe default change'));

    const missingEndpoint = await render([
        { name: 'rootfs', known: false, type: 'unknown', detection: 'unknown_rootfs_sha256' }
    ], 'zh-cn', { defaultResponse: { status: 404, ok: false } });
    assert.strictEqual(missingEndpoint.section.hidden, false);
    assert.strictEqual(missingEndpoint.inner.findAll('input').length, 1);
    assert(missingEndpoint.inner.findAll('input')[0].disabled);
    const fallbackBoot = missingEndpoint.inner.findAll('button').find(button => button.textContent.includes('rootfs') && !button.textContent.includes('刷新'));
    assert(fallbackBoot);
    await fallbackBoot.onclick();
    assert(missingEndpoint.requests.some(item => item.command === 'bootipq rootfs'));

    const partial = await render([
        { name: 'rootfs', known: false, type: 'unknown', detection: 'unknown_rootfs_sha256' },
        { name: 'rootfs_1', known: false, type: 'unknown', detection: 'unknown_rootfs_sha256' }
    ], 'en', {
        defaultResponse: response({ default: 'rootfs_1', can_change: true }),
        postResponses: [
            { status: 500, ok: false, json: async () => ({
                status: 'partial', error: 'partial_update', default: 'rootfs_1'
            }) },
            response({ default: 'rootfs_1', can_change: false, locked: true, reason: 'partial_update' })
        ]
    });
    const partialChecks = partial.inner.findAll('input');
    partialChecks[0].checked = true;
    await partialChecks[0].listeners.change();
    assert(partialChecks.every(input => input.disabled));
    assert.strictEqual(partial.inner.findAll('a').length, 2);
    assert(partial.inner.textContent.includes('may be partial'));

    console.log('PASS: HW-C unknown vs verified OpenWrt identity, rootfs-only default selection, normal-autoboot/RESET wording, readonly action disabling, incompatible and partial-write guards, unavailable-default fallback');
})().catch(error => { console.error(error); process.exitCode = 1; });
