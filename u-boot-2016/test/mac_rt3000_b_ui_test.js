#!/usr/bin/env node
const assert = require('assert');
const fs = require('fs');
const path = require('path');
const vm = require('vm');
const source = fs.readFileSync(path.join(__dirname, '..', 'failsafe/fsdata/main.js'), 'utf8');
const begin = source.indexOf('const macManager = (() => {');
const end = source.indexOf('// ==============================\n// 国际化数据', begin);
assert(begin >= 0 && end > begin, 'macManager production block missing');
const code = source.slice(begin, end) + '\nglobalThis.testMacManager = macManager;';

class Element {
    constructor(tag = 'div') {
        this.tagName = tag; this.children = []; this.style = {}; this.dataset = {};
        this.listeners = {}; this.disabled = false; this.readOnly = false;
        this.value = ''; this._text = ''; this.attributes = {};
        this.className = ''; this.classList = {
            add: (...names) => { this.className += ' ' + names.join(' '); },
            remove: () => {}
        };
    }
    set textContent(v) { this._text = String(v); this.children = []; }
    get textContent() { return this._text + this.children.map(c => c.textContent || '').join(''); }
    set innerHTML(v) { this._text = String(v); this.children = []; }
    get innerHTML() { return this._text; }
    appendChild(c) { this.children.push(c); return c; }
    addEventListener(name, fn) { this.listeners[name] = fn; }
    setAttribute(k, v) { this.attributes[k] = v; }
    closest(selector) { return selector === '.mac-input-group' && this.parentGroup ? this.parentGroup : null; }
}
class FormDataMock {
    constructor() { this.values = {}; }
    append(k, v) { this.values[k] = v; }
}
function makeContext(info) {
    const ids = {};
    for (const id of ['mac_status','mac_part_name','mac_table_body','mac_table_container','mac_modify_btn','mac_save_btn','mac_cancel_btn','mac_action_group','mac_save_cancel_group']) ids[id] = new Element();
    const warnings = Array.from({length: 4}, () => new Element('li'));
    const posts = [];
    let current = info;
    const doc = {
        getElementById: id => ids[id] || null,
        createElement: tag => new Element(tag),
        querySelectorAll(selector) {
            if (selector.startsWith('[data-i18n^="mac.warn."]')) return warnings;
            if (selector === '.mac-input-group') {
                const groups = [];
                const visit = node => { if (node.className === 'mac-input-group') groups.push(node); node.children.forEach(visit); };
                visit(ids.mac_table_body); return groups;
            }
            if (selector === '.mac-byte-input') {
                const inputs = []; const visit = node => { if (node.className === 'mac-byte-input') inputs.push(node); node.children.forEach(visit); };
                visit(ids.mac_table_body); return inputs;
            }
            return [];
        }
    };
    const ctx = {
        document: doc, FormData: FormDataMock, console,
        setTimeout, clearTimeout, t: key => key, applyI18n: () => {},
        fetch: async (url, options = {}) => {
            if (url === '/mac/info' && (!options.method || options.method === 'GET'))
                return { ok: true, status: 200, json: async () => current };
            if (url === '/mac/set' && options.method === 'POST') {
                posts.push(options.body.values.mac_data);
                current = {...current, macs: [options.body.values.mac_data]};
                return { ok: true, status: 200, text: async () => 'ok' };
            }
            throw new Error('unexpected fetch ' + url);
        }
    };
    vm.createContext(ctx); vm.runInContext(code, ctx);
    return { manager: ctx.testMacManager, ids, warnings, posts, get current() { return current; } };
}

(async () => {
    const info = {part_name:'0:APPSBLENV', variable:'ethaddr', source:'uboot_environment', persistent:true, writable:true, macs:['02:11:22:33:44:55']};
    const env = makeContext(info);
    await env.manager.init(); await env.manager.refresh();
    assert.strictEqual(env.ids.mac_modify_btn.disabled, false);
    assert(env.warnings[0].attributes['data-i18n'] === 'mac.warn.uboot.1');
    env.ids.mac_modify_btn.listeners.click();
    const group = env.ids.mac_table_body.children[0].children[1].children[0];
    assert.strictEqual(group.inputs.length, 6);
    group.inputs[5].value = '66';
    await env.manager.save();
    assert.deepStrictEqual(env.posts, ['02:11:22:33:44:66']);
    assert.strictEqual(env.current.macs[0], '02:11:22:33:44:66');

    const readonly = makeContext({...info, writable:false});
    await readonly.manager.init(); await readonly.manager.refresh();
    assert.strictEqual(readonly.ids.mac_modify_btn.disabled, true);
    await readonly.manager.save();
    assert.deepStrictEqual(readonly.posts, []);

    const invalid = makeContext(info);
    await invalid.manager.init(); await invalid.manager.refresh();
    invalid.ids.mac_modify_btn.listeners.click();
    const badGroup = invalid.ids.mac_table_body.children[0].children[1].children[0];
    badGroup.inputs[0].value = '03';
    await invalid.manager.save();
    assert.deepStrictEqual(invalid.posts, []);
    console.log('PASS: B MAC UI gates by persistent capability, validates unicast, posts one ethaddr and confirms refreshed value');
})().catch(e => { console.error(e); process.exit(1); });
