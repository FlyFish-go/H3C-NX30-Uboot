#!/usr/bin/env node
const assert=require('assert'),fs=require('fs'),path=require('path'),vm=require('vm');
const src=fs.readFileSync(path.join(__dirname,'..','failsafe','fsdata','main.js'),'utf8');
const a=src.indexOf('function renderRt3000StorageDevices('),b=src.indexOf('const sysinfoManager = (() => {',a);
const c=src.indexOf('    function renderDeviceInfo(',b),d=src.indexOf('    function renderPartitions(',c);
assert(a>=0&&b>a&&c>=0&&d>c);
const code=src.slice(c,d).replace(/^    /gm,'')+'\n'+src.slice(a,b);
const x={rows:[],t:k=>k,formatHexBytes:n=>'0x'+Number(n).toString(16),renderLifeInfo:()=>{},
createHeaderRow:(span,value)=>({type:'header',value}),createInfoRow:(label,value)=>({type:'info',label,value}),
table:{appendChild(v){x.rows.push(v);return v;}}};
vm.createContext(x);
vm.runInContext(code+'\nglobalThis.run=(d,z)=>renderRt3000StorageDevices(d,table,renderDeviceInfo,(t,s,v)=>t.appendChild(createHeaderRow(s,v)),(t,k,v)=>t.appendChild(createInfoRow(k,v)),formatHexBytes,z);',x);
const text=()=>x.rows.map(r=>r.type==='header'?r.value:r.label+'='+r.value).join('|');
x.run({spi_nor:{present:true,name:'W25Q128',model:'W25Q128',size:16777216,page_size:256,sector_size:4096,erase_size:65536,bus:0,chip_select:0},
nand:{present:true,name:'nand0',model:'GD5F1GQ5REYIG',size:134217728,page_size:2048,block_size:131072}},true);
let out=text();
assert(out.includes('SPI NOR')&&out.includes('W25Q128'));
assert(!out.includes('sysinfo.spi_nor_status')&&!out.includes('fake-nor'));
assert(out.includes('sysinfo.size=0x1000000')&&out.includes('sysinfo.sector_size=0x1000'));
assert(out.includes('sysinfo.erase_size=0x10000')&&out.includes('sysinfo.bus=0')&&out.includes('sysinfo.chip_select=0'));
assert(out.includes('NAND (nand0)')&&out.includes('sysinfo.model=GD5F1GQ5REYIG'));
assert(out.includes('sysinfo.size=0x8000000'));
x.rows.length=0;
x.run({spi_nor:{present:false,name:null,model:null,size:null,bus:1,chip_select:2},
spi:{present:false,name:'fake-nor',size:8388608},
nand:{present:true,name:'nand0',model:null,size:134217728,page_size:2048,block_size:131072}},true);
out=text();
assert(!out.includes('SPI NOR'));
assert(!out.includes('sysinfo.spi_nor_status')&&!out.includes('fake-nor')&&!out.includes('W25Q128'));
assert(!out.includes('sysinfo.bus=1')&&!out.includes('sysinfo.chip_select=2'));
assert(!out.includes('sysinfo.page_size=0x100')&&!out.includes('sysinfo.size=0x800000|'));
assert(!out.includes('sysinfo.sector_size=0x1000')&&!out.includes('sysinfo.erase_size=0x10000'));
assert(!out.includes('sysinfo.model=')&&out.includes('sysinfo.size=0x8000000'));
x.rows.length=0;
x.run({spi:{present:true,name:'legacy-spi',size:8388608}},false);
assert(text().includes('SPI (legacy-spi)'));
console.log('PASS: detected SPI NOR geometry and bus/CS, omission when undetected, NAND model/name/capacity, null models, legacy SPI fallback');
