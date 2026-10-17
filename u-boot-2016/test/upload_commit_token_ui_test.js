#!/usr/bin/env node
const assert = require('assert');
const fs = require('fs');
const path = require('path');
const vm = require('vm');
const source = fs.readFileSync(path.join(__dirname, '..', 'failsafe/fsdata/main.js'), 'utf8');
const begin = source.indexOf('class FileUploadComponent {');
const end = source.indexOf('let unifiedUploadManager = null;', begin);
assert(begin >= 0 && end > begin, 'production upload component missing');
const code = source.slice(begin, end) + '\nglobalThis.TestUpload = FileUploadComponent;';
class FormDataMock { constructor(){this.values={};} append(k,v){this.values[k]=v;} }
class XhrMock {
    constructor(){ this.upload={addEventListener(){}}; this.readyState=0; this.status=0; this.responseText=''; this.onreadystatechange=null; XhrMock.all.push(this); }
    open(method,url){this.method=method;this.url=url;}
    send(body){this.body=body;}
    finish(status, body){this.status=status;this.responseText=body;this.readyState=4;this.onreadystatechange();}
}
XhrMock.all=[];
const context={FormData:FormDataMock,XMLHttpRequest:XhrMock,document:{getElementById(){return null;}},t:k=>k,window:{location:{}},messageBuilder:{},console,alert(){}};
vm.createContext(context);vm.runInContext(code,context);
function component(){
    const c=new context.TestUpload({}, {pageName:'art',formDataKey:'art'});
    c.showProgress=()=>{}; c.updateProgress=()=>{}; c.showSuccess=(type,info)=>{c.lastSuccess={type,info};}; c.showError=(type,info)=>{c.lastError={type,info};};
    c.setTitleAndHint=()=>{}; c.showElementAndHideOthers=()=>{};
    return c;
}
function finishUpload(c, info){ c.upload(); const xhr=XhrMock.all.at(-1); xhr.finish(200,JSON.stringify({status:'success',info})); return xhr; }
const token='a'.repeat(64);
const stale=component(); const first={name:'first.bin'}, second={name:'second.bin'}; stale.selectedFile=first;
const staleXhr=stale.upload();
const pending=XhrMock.all.at(-1);
stale.handleFileSelect(second);
pending.finish(200,JSON.stringify({status:'success',info:{commit_token:token,target:'0:ART'}}));
assert.strictEqual(stale.commitToken,null,'stale response must not attach to a different selected file');
assert(stale.lastError);
const c=component(); c.selectedFile={name:'art.bin'};
finishUpload(c,{commit_token:token,target:'0:ART'});
assert.strictEqual(c.commitToken,token);
c.continue(false);
const requestCount=XhrMock.all.length; c.continue(false);
assert.strictEqual(XhrMock.all.length,requestCount,'one-use token must not submit twice');
const write=XhrMock.all.at(-1);
assert.strictEqual(write.url,'/write');
assert.strictEqual(write.body.values.commit_token,token);
assert.strictEqual(write.body.values.auto_reboot,'false');
assert.strictEqual(write.body.values.target,undefined);
assert.strictEqual(c.commitToken,null,'token is consumed locally before write attempt');
write.finish(200,JSON.stringify({status:'success',info:{reboot:false}}));
const dual=component(); dual.options.pageName='uboot'; dual.options.formDataKey='uboot'; dual.selectedFile={name:'dual.bin'};
finishUpload(dual,{commit_token:token,target:'0:APPSBL + 0:APPSBL_1',target_offset:'0x500000',target_size:'0x140000',second_target_offset:'0x640000',second_target_size:'0x140000'});
assert.strictEqual(dual.commitToken,token,'dual U-Boot preview must retain the one-use token');
dual.continue(false); const dualWrite=XhrMock.all.at(-1);
assert.strictEqual(dualWrite.url,'/write'); assert.strictEqual(dualWrite.body.values.commit_token,token);
const dualCount=XhrMock.all.length;dual.continue(false);assert.strictEqual(XhrMock.all.length,dualCount);
const legacy=component(); legacy.selectedFile={name:'legacy.bin'};
finishUpload(legacy,{partname:'ART',partsize:4096});
assert.strictEqual(legacy.commitToken,null);
legacy.continue(false);
assert.strictEqual(XhrMock.all.at(-1).url,'/result','legacy upload remains on /result without a token');
console.log('PASS: metadata commit token is file-bound, one-use, sent only to /write; legacy uploads stay token-free');
