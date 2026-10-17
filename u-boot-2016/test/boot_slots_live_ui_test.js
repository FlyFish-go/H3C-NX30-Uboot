#!/usr/bin/env node
const assert = require('assert');
const fs = require('fs');
const path = require('path');
const vm = require('vm');
const main = fs.readFileSync(path.join(__dirname, '..', 'failsafe', 'fsdata', 'main.js'), 'utf8');
const begin = main.indexOf('function bootSlotFirmwareText');
const end = main.indexOf('// Physical button status on RT3000-B', begin);
assert(begin >= 0 && end > begin, 'current boot slot UI block missing');
const code = main.slice(begin, end) + '\nglobalThis.manager = bootSlotsManager; globalThis.locationText = bootSlotLocationText;';

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


async function run() {
 const section=new Element('section'),inner=new Element('div');section.inner=inner;section.appendChild(inner);
 let slots=[{name:'rootfs',offset:'0x900000',size:'0x2800000',physical_index:1},{name:'rootfs_1',offset:'0x3100000',size:'0x2800000',physical_index:2}];
 let timers=[],gets=0,pending=false,release;
 const context={APP_STATE:{lang:'zh-cn'},FormData:FormDataMock,
  setTimeout:(fn,ms)=>{assert.equal(ms,5000);timers.push(fn);return timers.length;},clearTimeout:()=>{},
  document:{body:{classList:{contains:()=>false}},hidden:false,getElementById:()=>section,createElement:t=>new Element(t)},
  fetch:async(url,opts={})=>{
   if(opts.method==='POST'){pending=true;return new Promise(resolve=>{release=()=>{pending=false;resolve({ok:true,json:async()=>({status:'ok',default:'rootfs_1'})});};});}
   assert.equal(opts.cache,'no-store');gets++;
   return {ok:true,status:200,json:async()=>url==='/boot/slots'?{mode:'nand',slots}: {status:'ok',default:pending?'rootfs':'rootfs_1',can_change:true}};
  }};
 vm.createContext(context);vm.runInContext(code,context);await context.manager.init();
 assert(inner.textContent.includes('物理第 1 区 · 地址范围：0x900000 – 0x30fffff'));
 assert(inner.textContent.includes('物理第 2 区 · 地址范围：0x3100000 – 0x58fffff'));
 const previousTable=section.findAll('table')[0];await timers.shift()();assert.strictEqual(section.findAll('table')[0],previousTable,'unchanged poll must preserve controls');
 slots=[{name:'rootfs',offset:'0x3100000',size:'0x2800000',physical_index:2},{name:'rootfs_1',offset:'0x900000',size:'0x2800000',physical_index:1}];
 await timers.shift()();
 const cells=section.findAll('td');assert(cells[0].textContent.startsWith('rootfs物理第 2 区 · 地址范围：0x3100000 – 0x58fffff'));assert(cells[2].textContent.startsWith('rootfs_1物理第 1 区 · 地址范围：0x900000 – 0x30fffff'));
 const beforeHidden=gets;context.document.hidden=true;await timers.shift()();assert.equal(gets,beforeHidden);context.document.hidden=false;
 slots=[{name:'rootfs',offset:'garbage',size:1},{name:'rootfs_1',offset:-1,size:1}];await timers.shift()();assert(!inner.textContent.includes('0x900000'));assert(inner.textContent.includes('当前地址不可用'));
 for(const value of [-1,NaN,Infinity,1.5,'0x12junk',undefined]) assert.equal(context.locationText({offset:value,physical_index:1},false),null);
 assert.equal(context.locationText({offset:0x3100000,physical_index:2},false),'Physical slot 2 · Start: 0x3100000');
 console.log('PASS: live slot labels refresh actual mapping, retain unchanged controls, pause while hidden, and discard stale/invalid addresses');
}
run().catch(e=>{console.error(e);process.exit(1);});
