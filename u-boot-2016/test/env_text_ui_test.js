#!/usr/bin/env node
const assert = require('assert');
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const source = fs.readFileSync(path.join(__dirname, '..', 'failsafe/fsdata/main.js'), 'utf8');
const start = source.indexOf('const envManager = (() => {');
const marker = source.indexOf('\n})();', start);
assert(start >= 0 && marker > start, 'production envManager closure missing');
const managerSource = source.slice(start, marker + 6);

class Element {
    constructor() {
        this.children = [];
        this.dataset = {};
        this.style = {};
        this.classList = { add() {}, remove() {} };
        this.disabled = false;
        this.hidden = false;
    }
    appendChild(child) { this.children.push(child); return child; }
    addEventListener() {}
    set innerHTML(value) { if (value === '') this.children = []; }
}

async function render(text) {
    const elements = new Map();
    const get = id => {
        if (!elements.has(id)) elements.set(id, new Element());
        return elements.get(id);
    };
    const controls = ['env.action.add', 'env.action.reset', 'env.action.refresh']
        .map(key => { const button = new Element(); button.dataset.i18n = key; return button; });
    const actionHeader = new Element();
    const document = {
        getElementById: get,
        createElement: () => new Element(),
        addEventListener() {},
        querySelector(selector) {
            if (selector === '#env_table thead .env-col-actions') return actionHeader;
            return null;
        },
        querySelectorAll(selector) {
            if (selector === '.env-main-actions button') return controls;
            return [];
        }
    };
    const context = {
        document, console, clearTimeout, setTimeout,
        t: key => key,
        fetch: async url => url === '/env/info'
            ? { ok: true, json: async () => ({ read_only: true }) }
            : { ok: true, text: async () => text }
    };
    vm.createContext(context);
    vm.runInContext(managerSource + '\nglobalThis.testEnvManager = envManager;', context);
    await context.testEnvManager.init();
    return { elements, controls, actionHeader };
}

(async () => {
    const lf = 'alpha=first\nempty=\nvalue=left=middle=right\nbare\n=ignored\n\n';
    const crlf = lf.replace(/\n/g, '\r\n');
    const expected = [
        ['alpha', 'first'], ['bare', ''], ['empty', ''],
        ['value', 'left=middle=right']
    ];
    for (const text of [lf, crlf]) {
        const ui = await render(text);
        const rows = ui.elements.get('env_table_body').children;
        const actual = rows.map(row => [row.children[0].textContent, row.children[1].children[0].value]);
        assert.deepStrictEqual(actual, expected, 'LF and CRLF render identical rows');
        assert(rows.every(row => row.children.length === 2), 'read-only rows omit mutation controls');
        assert(rows.every(row => row.children[1].children[0].disabled &&
            row.children[1].children[0].readOnly), 'read-only values remain disabled');
        assert.strictEqual(ui.controls[0].disabled, true);
        assert.strictEqual(ui.controls[1].disabled, true);
        assert.strictEqual(ui.controls[2].disabled, false, 'refresh remains available');
        assert.strictEqual(ui.elements.get('env_restore_file').disabled, true);
        assert.strictEqual(ui.actionHeader.style.display, 'none');
        assert.strictEqual(ui.elements.get('env_count').textContent, 'env.count 4');
        assert.strictEqual(ui.elements.get('env_status').textContent, 'env.status.ready');
    }
    const empty = await render('');
    assert.strictEqual(empty.elements.get('env_table_body').children.length, 0);
    assert.strictEqual(empty.elements.get('env_empty').style.display, 'flex');
    assert.strictEqual(empty.elements.get('env_table').style.display, 'none');
    console.log('Actual envManager init/refresh/createDataRow LF/CRLF/read-only tests passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
