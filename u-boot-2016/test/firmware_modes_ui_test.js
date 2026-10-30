#!/usr/bin/env node
/* Mock-DOM regression tests for RT3000-B firmware modes and upload token flow. */
const assert = require('assert');
const fs = require('fs');
const path = require('path');
const vm = require('vm');
const sourcePath = path.join(__dirname, '..', 'failsafe', 'fsdata', 'main.js');
const source = fs.readFileSync(sourcePath, 'utf8');
const managerStart = source.indexOf('// RT3000-B firmware modes; all write targets come from the active layout API.');
const managerEnd = source.indexOf('// Boot card version rows appear only when the backend classification supports them.', managerStart);
const slotsStart = managerEnd;
const slotsEnd = source.indexOf('// Physical button status on RT3000-B', slotsStart);
assert(managerStart >= 0 && managerEnd > managerStart && slotsEnd > slotsStart, 'UI source blocks not found');
const uiCode = source.slice(managerStart, managerEnd) + source.slice(slotsStart, slotsEnd) +
    '\n;globalThis.testFirmwareModes = firmwareModeManager; globalThis.testBootSlots = bootSlotsManager;';

class Element {
    constructor(tag) {
        this.tagName = tag; this.children = []; this.listeners = {}; this._text = '';
        this.disabled = false; this.hidden = false; this.checked = false; this.files = [];
        this.innerHTMLWrites = 0; this.href = ''; this.value = ''; this.name = ''; this.type = ''; this.style = {};
    }
    set textContent(value) { this._text = String(value); this.children = []; }
    get textContent() { return this._text + this.children.map(x => x.textContent).join(''); }
    set innerHTML(value) { this.innerHTMLWrites++; this._text = String(value); this.children = []; }
    appendChild(child) { this.children.push(child); return child; }
    addEventListener(type, fn) { (this.listeners[type] ||= []).push(fn); }
    async dispatch(type) { for (const fn of this.listeners[type] || []) await fn({ target: this }); }
    async click() { if (!this.disabled) { if (this.onclick) await this.onclick(); await this.dispatch('click'); } }
    find(tag) { return (this.tagName === tag ? [this] : []).concat(...this.children.map(x => x.find(tag))); }
    querySelector(selector) { return selector === '.card-main-inner' ? this.inner : null; }
}
class FormDataMock {
    constructor() { this.values = {}; }
    append(key, value) { this.values[key] = value; }
}

async function bootContext({ lang = 'en', layout, replies = [], pathName = '/firmware-dual.html', legacy = false } = {}) {
    let elements = {};
    const body = new Element('body');
    Object.defineProperty(body, 'innerHTML', {
        set(markup) {
            body._text = markup; body.children = []; elements = {};
            for (const id of ['sidebar', 'sidebarOverlay', 'sidebarToggle', 'bFirmwareTitle', 'bFirmwareHint', 'bFirmwareContent', 'bFirmwareFoot', 'version']) {
                const node = new Element('div'); node.id = id; elements[id] = node;
                if (id === 'bFirmwareContent' || id === 'bFirmwareFoot') body.appendChild(node);
            }
        }
    });
    const document = {
        body, title: '',
        getElementById: id => elements[id] || null,
        createElement: tag => new Element(tag)
    };
    const requests = [];
    const pending = [];
    class XHR {
        constructor() { this.upload = { addEventListener() {} }; }
        open(method, url) { this.method = method; this.url = url; }
        send(form) {
            requests.push({ url: this.url, form: form.values });
            const result = replies.shift() || { status: 'fail', info: { type: 'unexpected_test_request' } };
            const complete = () => {
                this.status = result.httpStatus || 200;
                this.responseText = JSON.stringify(result);
                this.onload();
            };
            if (result.defer) pending.push({ complete });
            else Promise.resolve().then(complete);
        }
    }
    let legacyCalls = [];
    const context = {
        APP_STATE: { lang, page: pathName }, document, XMLHttpRequest: XHR, FormData: FormDataMock,
        window: { location: { href: '' } },
        fetch: async url => {
            if (url !== '/firmware/layout') throw new Error('unexpected fetch ' + url);
            return legacy ? { status: 404, ok: false } : { status: 200, ok: true, json: async () => layout };
        },
        ensureSidebar() {}, applyI18n() {},
        bytesToHuman: n => n >= 1024 * 1024 ? (n / 1048576).toFixed(1) + ' MiB' : n + ' B',
        getUnifiedUploadManager: () => ({ initForPage: (...args) => legacyCalls.push(args) })
    };
    vm.createContext(context);
    vm.runInContext(uiCode, context);
    return { context, requests, pending, legacyCalls, elements: () => elements, body };
}
const baseLayout = (mode = 'dual', write = true) => ({ status: 'ok', mode, write_supported: write, ptable_write_supported: false, partitions: [
    { name: 'rootfs', display_index: 16, mtd: 'mtd16', offset: 9437184, size: 41943040 },
    ...(mode === 'dual' ? [{ name: 'rootfs_1', display_index: 17, mtd: 'mtd17', offset: 51380224, size: 41943040 }] : [])
] });

(async () => {
    let c = await bootContext({ layout: baseLayout('dual') });
    await c.context.testFirmwareModes.initLanding();
    assert(c.elements().bFirmwareContent.textContent.includes('Dual rootfs slots'));
    assert(c.elements().bFirmwareContent.textContent.includes('Partition-table writing is not enabled'));
    assert(c.elements().bFirmwareFoot.textContent.includes('Do not power off the device during update.'));
    assert(c.elements().bFirmwareFoot.textContent.includes('device model and active partition layout'));
    assert(!c.elements().bFirmwareFoot.textContent.includes('QSDK sysupgrade'));
    const landingButtons = c.elements().bFirmwareContent.find('button');
    assert(landingButtons.some(button => !button.disabled && typeof button.onclick === 'function'),
        'enabled mode navigation must use a styled button');
    await landingButtons.find(button => !button.disabled).click();
    assert(c.context.window.location.href === '/firmware-dual.html',
        'mode button must navigate to the selected same-origin page');

    // A bootloader that writes tables points to the PTABLE page instead.
    c = await bootContext({ layout: { ...baseLayout('dual'), ptable_write_supported: true } });
    await c.context.testFirmwareModes.initLanding();
    assert(c.elements().bFirmwareContent.textContent.includes('generate and write the table on the PTABLE page'));
    assert(!c.elements().bFirmwareContent.textContent.includes('not enabled'));
    c = await bootContext({ layout: { ...baseLayout('dual'), ptable_write_supported: true } });
    await c.context.testFirmwareModes.initMode('single');
    assert(c.elements().bFirmwareContent.textContent.includes('generate and write the table on the PTABLE page'));

    c = await bootContext({ layout: baseLayout('dual') });
    await c.context.testFirmwareModes.initMode('single');
    assert(c.elements().bFirmwareContent.textContent.includes('First install and activate a compatible large-rootfs table'));
    assert.strictEqual(c.elements().bFirmwareContent.find('input').length, 0, 'mismatched layout must not expose upload controls');

    c = await bootContext({ layout: baseLayout('dual'), replies: [
        { status: 'ok', token: 'tok-one', target: 'rootfs', mode: 'dual', bytes: 100, sha256: 'a'.repeat(64), format: 'ubi_unidentified' },
        { status: 'ok', token: 'tok-two', target: 'rootfs_1', mode: 'dual', bytes: 101, sha256: 'b'.repeat(64), format: 'h3c_rt3000_oem_ubi' },
        { status: 'ok', verified: true, target: 'rootfs_1', mode: 'dual' }
    ] });
    await c.context.testFirmwareModes.initMode('dual');
    let inputs = c.elements().bFirmwareContent.find('input');
    const radios = inputs.filter(x => x.type === 'radio');
    const fileInput = inputs.find(x => x.type === 'file');
    let buttons = c.elements().bFirmwareContent.find('button');
    const validate = buttons[0], write = buttons[1];
    assert.strictEqual(radios.length, 2);
    assert(radios.every(r => !r.checked), 'target must never be silently selected');
    assert(c.elements().bFirmwareFoot.textContent.includes('Do not power off the device during update.'));
    assert(c.elements().bFirmwareFoot.textContent.includes('confirm the target partition before writing'));
    assert(!c.elements().bFirmwareFoot.textContent.includes('QSDK sysupgrade'));
    radios[0].checked = true; await radios[0].dispatch('change');
    fileInput.files = [{ name: 'fixture.bin' }]; await fileInput.dispatch('change');
    assert.strictEqual(validate.disabled, false);
    await validate.click();
    assert.strictEqual(write.disabled, false);
    assert(c.elements().bFirmwareContent.textContent.includes('rootfs'));
    assert(!c.elements().bFirmwareContent.textContent.includes('mtd'));
    assert(c.elements().bFirmwareContent.textContent.includes('Capacity: 41,943,040 bytes'));
    const targetCards = c.elements().bFirmwareContent.find('label').filter(x => x.find('input').some(i => i.type === 'radio'));
    assert(targetCards[0].textContent.includes('Physical slot 1 · Address range: 0x900000 – 0x30fffff'));
    assert(targetCards[1].textContent.includes('Physical slot 2 · Address range: 0x3100000 – 0x58fffff'));
    assert(c.elements().bFirmwareContent.textContent.includes('Target: rootfsPhysical slot 1 · Address range: 0x900000 – 0x30fffff'));

    assert(c.elements().bFirmwareContent.textContent.includes('Complete UBI system image (kernel and system volumes)'));
    radios[1].checked = true; radios[0].checked = false; await radios[1].dispatch('change');
    assert.strictEqual(write.disabled, true, 'changing target must invalidate the prior token');
    assert.strictEqual(c.elements().bFirmwareContent.find('div').some(x => x.textContent.includes('SHA-256:')), false);
    await validate.click();
    assert.strictEqual(write.disabled, false);
    await write.click();
    assert.strictEqual(c.requests[0].url, '/firmware/validate');
    assert.strictEqual(c.requests[0].form.target, 'rootfs');
    assert.strictEqual(c.requests[1].form.target, 'rootfs_1');
    assert.strictEqual(c.requests[2].url, '/firmware/write');
    assert.strictEqual(c.requests[2].form.target, 'rootfs_1');
    assert.strictEqual(c.requests[2].form.mode, 'dual');
    assert(c.elements().bFirmwareContent.textContent.includes('Write and backend verification completed'));

    c = await bootContext({ layout: baseLayout('single'), replies: [
        { status: 'ok', token: 'tok-single', target: 'rootfs', mode: 'single', bytes: 102, sha256: 'c'.repeat(64), format: 'ubi_unidentified' },
        { status: 'fail', info: { type: 'write_not_verified' } }
    ], pathName: '/firmware-single.html' });
    await c.context.testFirmwareModes.initMode('single');
    inputs = c.elements().bFirmwareContent.find('input');
    assert.deepStrictEqual(inputs.filter(x => x.type === 'radio').map(x => x.value), ['rootfs']);
    inputs.find(x => x.type === 'radio').checked = true;
    await inputs.find(x => x.type === 'radio').dispatch('change');
    inputs.find(x => x.type === 'file').files = [{ name: 'single.bin' }];
    await inputs.find(x => x.type === 'file').dispatch('change');
    buttons = c.elements().bFirmwareContent.find('button');
    await buttons[0].click(); await buttons[1].click();
    assert(c.elements().bFirmwareContent.textContent.includes('Write or full readback failed'));
    assert(c.elements().bFirmwareContent.textContent.includes('do not retry automatically'));

    /* A window refused before erase (HW-D ECC gate) says flash is unchanged and shows errno. */
    c = await bootContext({ layout: baseLayout('single'), replies: [
        { status: 'ok', token: 'tok-refused', target: 'rootfs', mode: 'single', bytes: 102, sha256: 'd'.repeat(64), format: 'ubi_unidentified' },
        { status: 'fail', target: 'rootfs', mode: 'single', error: 'write refused before erase', stage: 2, flash_changed: false, errno: -30 }
    ], pathName: '/firmware-single.html' });
    await c.context.testFirmwareModes.initMode('single');
    inputs = c.elements().bFirmwareContent.find('input');
    inputs.find(x => x.type === 'radio').checked = true;
    await inputs.find(x => x.type === 'radio').dispatch('change');
    inputs.find(x => x.type === 'file').files = [{ name: 'refused.bin' }];
    await inputs.find(x => x.type === 'file').dispatch('change');
    buttons = c.elements().bFirmwareContent.find('button');
    await buttons[0].click(); await buttons[1].click();
    assert(c.elements().bFirmwareContent.textContent.includes('flash was not changed'));
    assert(c.elements().bFirmwareContent.textContent.includes('(error code -30)'));
    assert(!c.elements().bFirmwareContent.textContent.includes('readback failed'));

    c = await bootContext({ lang: 'zh-cn', layout: baseLayout('dual'), replies: [
        { defer: true, status: 'ok', token: 'stale-token', target: 'rootfs', mode: 'dual' },
        { status: 'ok', token: 'fresh-token', target: 'rootfs_1', mode: 'dual', format: 'ubi_unidentified' }
    ] });
    await c.context.testFirmwareModes.initMode('dual');
    inputs = c.elements().bFirmwareContent.find('input');
    const raceRadios = inputs.filter(x => x.type === 'radio');
    const raceFile = inputs.find(x => x.type === 'file');
    buttons = c.elements().bFirmwareContent.find('button');
    const raceValidate = buttons[0], raceWrite = buttons[1];
    assert(c.elements().bFirmwareFoot.textContent.includes('刷写过程中请勿断电。'));
    assert(c.elements().bFirmwareFoot.textContent.includes('并在写入前确认目标分区。'));
    const oldFile = { name: 'old.bin' };
    raceRadios[0].checked = true; await raceRadios[0].dispatch('change');
    raceFile.files = [oldFile]; await raceFile.dispatch('change');
    const oldValidation = raceValidate.click();
    await Promise.resolve();
    assert(raceValidate.disabled && raceFile.disabled && raceRadios.every(x => x.disabled),
        'controls must lock while validation is pending');
    await raceValidate.click();
    assert.strictEqual(c.requests.length, 1, 'duplicate validation must not issue another request');
    assert.strictEqual(c.requests[0].form.target, 'rootfs');
    assert.strictEqual(c.requests[0].form.firmware, oldFile);
    raceRadios[1].checked = true; raceRadios[0].checked = false;
    await raceRadios[1].dispatch('change');
    const newFile = { name: 'new.bin' };
    raceFile.files = [newFile]; await raceFile.dispatch('change');
    c.pending[0].complete();
    await oldValidation;
    assert(raceWrite.disabled, 'stale validation response must not issue a usable token');
    assert(!raceFile.disabled && !raceRadios.some(x => x.disabled), 'controls unlock after stale response is discarded');
    await raceValidate.click();
    assert.strictEqual(c.requests[1].form.target, 'rootfs_1');
    assert.strictEqual(c.requests[1].form.firmware, newFile);
    assert(!raceWrite.disabled, 'fresh response for the current file and target is accepted');

    c = await bootContext({ layout: baseLayout('dual'), replies: [
        { status: 'ok', token: 'wrong-mode', target: 'rootfs', mode: 'single' }
    ] });
    await c.context.testFirmwareModes.initMode('dual');
    inputs = c.elements().bFirmwareContent.find('input');
    inputs.find(x => x.type === 'radio').checked = true;
    await inputs.find(x => x.type === 'radio').dispatch('change');
    inputs.find(x => x.type === 'file').files = [{ name: 'wrong-mode.bin' }];
    await inputs.find(x => x.type === 'file').dispatch('change');
    buttons = c.elements().bFirmwareContent.find('button');
    await buttons[0].click();
    assert(buttons[1].disabled, 'validation response with wrong mode must not enable write');

    c = await bootContext({ lang: 'zh-cn', layout: baseLayout('dual'), replies: [
        { status: 'fail', error: 'unsupported package or invalid UBI image', httpStatus: 422 }
    ] });
    await c.context.testFirmwareModes.initMode('dual');
    inputs = c.elements().bFirmwareContent.find('input');
    inputs.find(x => x.type === 'radio').checked = true;
    await inputs.find(x => x.type === 'radio').dispatch('change');
    inputs.find(x => x.type === 'file').files = [{ name: 'bad.bin' }];
    await inputs.find(x => x.type === 'file').dispatch('change');
    buttons = c.elements().bFirmwareContent.find('button');
    await buttons[0].click();
    assert(c.elements().bFirmwareContent.textContent.includes('固件包不支持，或完整 UBI 系统镜像结构无效、不完整。'));
    assert(!c.elements().bFirmwareContent.textContent.includes('固件验证失败'));

    c = await bootContext({ legacy: true, pathName: '/firmware.html' });
    await c.context.testFirmwareModes.initLanding();
    assert.strictEqual(c.legacyCalls.length, 1);
    assert.strictEqual(c.legacyCalls[0][0], 'firmware');
    assert.strictEqual(c.legacyCalls[0][1].formDataKey, 'firmware');

    // The layout API is the authority even when logical names swap physical extents.
    const swapped = baseLayout('dual');
    [swapped.partitions[0].offset, swapped.partitions[1].offset] =
        [swapped.partitions[1].offset, swapped.partitions[0].offset];
    swapped.partitions.reverse();
    c = await bootContext({ layout: swapped, lang: 'zh-cn' });
    await c.context.testFirmwareModes.initMode('dual');
    const swappedCards = c.elements().bFirmwareContent.find('label').filter(x => x.find('input').some(i => i.type === 'radio'));
    assert(swappedCards[0].textContent.startsWith('rootfs物理第 2 区 · 地址范围：0x3100000 – 0x58fffff'));
    assert(swappedCards[1].textContent.startsWith('rootfs_1物理第 1 区 · 地址范围：0x900000 – 0x30fffff'));
    assert.strictEqual(c.requests.length, 0, 'displaying locations must not submit any writes');
    const single = baseLayout('single');
    single.partitions[0].offset = '0x900000';
    single.partitions[0].size = '0x5000000';
    c = await bootContext({ layout: single, lang: 'zh-cn' });
    await c.context.testFirmwareModes.initMode('single');
    assert(c.elements().bFirmwareContent.textContent.includes('rootfs物理第 1 区 · 地址范围：0x900000 – 0x58fffff'));

    console.log('PASS: dual/single layout gates, explicit no-default targets, target-bound token invalidation, validate/write, deferred file/target race rejection, typed non-2xx errors, unverified failure, refused-before-erase with errno, legacy other-board upload route');
})().catch(error => { console.error(error); process.exitCode = 1; });
