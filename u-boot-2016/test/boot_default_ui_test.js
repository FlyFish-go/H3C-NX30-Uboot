#!/usr/bin/env node
const assert = require('assert');
const fs = require('fs');
const path = require('path');
const vm = require('vm');
const main = fs.readFileSync(path.join(__dirname, '..', 'failsafe', 'fsdata', 'main.js'), 'utf8');
const begin = main.indexOf('function bootSlotFirmwareText');
const end = main.indexOf('// Physical button status on RT3000-B', begin);
assert(begin >= 0 && end > begin, 'current boot slot UI block missing');
const code = main.slice(begin, end) + '\nglobalThis.manager = bootSlotsManager;';

class Element {
    constructor(tag) { this.tagName=tag; this.children=[]; this.className=''; this.hidden=false; this.disabled=false; this.checked=false; this.style={}; this.handlers={}; this._text=''; }
    set textContent(v) { this._text=String(v); this.children=[]; }
    get textContent() { return this._text+this.children.map(c=>c.textContent).join(''); }
    appendChild(c) { this.children.push(c); return c; }
    querySelector(s) { return s === '.card-main-inner' ? this.inner : null; }
    setAttribute(k,v) { this[k]=v; }
    addEventListener(k,fn) { this.handlers[k]=fn; }
    async change() { if(this.handlers.change) await this.handlers.change(); }
    findAll(tag) { return (this.tagName===tag?[this]:[]).concat(...this.children.map(c=>c.findAll(tag))); }
}
class FormDataMock { constructor(){this.values={};} append(k,v){this.values[k]=v;} }

async function render(foreignBoard=false) {
    const section=new Element('section'), inner=new Element('div'); section.inner=inner; section.appendChild(inner);
    const posts=[],boots=[]; let partialLock=false,defaultCurrent='rootfs',resolvePost,lostPostResponse=false;
    let next={ok:true,status:200,json:async()=>({status:'ok',default:'rootfs_1'})};
    const context={APP_STATE:{lang:'en'},FormData:FormDataMock,
        document:{body:{classList:{contains:()=>false}},getElementById:id=>id==='boot_slots'?section:null,createElement:t=>new Element(t)},
        fetch:async(url,opts={})=>{
            if(url==='/boot/slots') return {status:200,ok:true,json:async()=>({slots:[{name:'rootfs',size:4000},{name:'rootfs_1',size:4000}]})};
            if(url==='/boot/default'&&!opts.method) {
                if(foreignBoard) return {status:404,ok:false,json:async()=>({})};
                return {status:200,ok:true,json:async()=>({status:'ok',default:defaultCurrent,can_change:!partialLock,locked:partialLock,reason:partialLock?'partial_update':''})};
            }
            if(url==='/boot/default'&&opts.method==='POST'){
                const target=opts.body.values.target; posts.push(target);
                if(lostPostResponse){lostPostResponse=false;defaultCurrent=target;return Promise.reject(new Error('response lost'));}
                if(next.status===500) partialLock=true;
                return new Promise(resolve=>{resolvePost=async()=>{if(next.status!==500)defaultCurrent=target;resolve(next);};});
            }
            if(url==='/webterm/exec'){boots.push(opts.body.values.cmd);return {ok:true,text:async()=>'accepted'};}
            throw new Error('unexpected URL '+url);
        }};
    vm.createContext(context); vm.runInContext(code,context); await context.manager.init();
    assert(!inner.textContent.includes('Partition detection failed:') && !inner.textContent.includes('分区信息读取失败：'), 'slot UI must initialize without swallowed errors');
    return {section,inner,posts,boots,checks:section.findAll('input'),buttons:section.findAll('button').filter(b => b.textContent.startsWith('Boot ')),setNext:v=>next=v,setLost:()=>{lostPostResponse=true;},resolve:()=>resolvePost()};
}

async function run() {
    const ui=await render();
    assert.equal(ui.checks.length,2);
    assert.equal(ui.checks[0].checked,true); assert.equal(ui.checks[1].checked,false);
    assert.equal(ui.buttons.length,2);
    await ui.buttons[0].onclick();
    assert.deepEqual(ui.posts,[]); assert.deepEqual(ui.boots,['bootipq rootfs']);

    ui.checks[1].checked=true; const saving=ui.checks[1].change();
    assert.equal(ui.checks[0].checked,true); assert.equal(ui.checks[1].checked,false);
    await ui.resolve(); await saving;
    assert.deepEqual(ui.posts,['rootfs_1']);
    assert.equal(ui.checks[0].checked,false); assert.equal(ui.checks[1].checked,true);
    ui.checks[1].checked=false; await ui.checks[1].change();
    assert.equal(ui.checks[1].checked,true); // current default cannot be cleared

    ui.setLost(); ui.checks[0].checked=true; const uncertain=ui.checks[0].change();
    assert.equal(ui.checks[0].checked,false); assert.equal(ui.checks[1].checked,true);
    await uncertain; // refresh discovers that the write completed despite lost response
    assert.equal(ui.checks[0].checked,true); assert.equal(ui.checks[1].checked,false);

    ui.setNext({ok:false,status:500,json:async()=>({status:'partial',error:'copy_failed'})});
    ui.checks[1].checked=true; const failing=ui.checks[1].change();
    assert.equal(ui.checks[0].checked,true); assert.equal(ui.checks[1].checked,false);
    await ui.resolve(); await failing;
    assert.equal(ui.checks[0].checked,true); assert.equal(ui.checks[1].checked,false);
    assert.equal(ui.checks[0].disabled,true); assert.equal(ui.checks[1].disabled,true);

    const other=await render(true);
    assert.equal(other.section.hidden,false); // optional endpoint 404 keeps boot controls
    assert.equal(other.buttons.length,2); assert.equal(other.checks.length,2);
    assert.equal(other.checks[0].disabled,true); assert.equal(other.checks[1].disabled,true);
    await other.buttons[0].onclick(); assert.deepEqual(other.boots,['bootipq rootfs']);
    console.log('persistent default UI PASS');
}
run().catch(e=>{console.error(e);process.exit(1);});
