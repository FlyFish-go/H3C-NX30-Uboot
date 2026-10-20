"use strict";
const assert = require("node:assert/strict");
const {Wizard, mount, latestChecks, checkSummary} = require("../web/main.js");

const response = value => ({ok:true, json:async()=>value});
const baseStatus = overrides => Object.assign({
  session:"test-session", token:"token / value", phase:"ready", install_ready:true,
  blocked_reason:"", stage:"ready", progress_bytes:0, total_bytes:100,
  message:"检查通过", may_have_modified:false, can_reboot:false,
  candidate:{version:"NX30-D",sha256:"abc",bytes:3,ip:"192.168.10.1"},
  backup:{available:true,format:"zip",complete:true,size_bytes:512,url:"/backup.zip"},
  target:{name:"APPSBL",offset:"0x640000",size:"0x140000"},checks:[]
}, overrides || {});

async function run() {
  // Check events are not separate current checks. Preserve order and input history.
  const events=[{name:"bridge_profile",status:"checking",detail:"start"},
    {name:"nand_identity",status:"checking"},
    {name:"bridge_profile",status:"pass",detail:"verified"},
    {name:"nand_identity",status:"blocked",detail:"specific reason"}];
  assert.deepEqual(latestChecks(events),[events[2],events[3]]);
  assert.equal(events.length,4);
  assert.equal(latestChecks(events).some(item=>item.status==="checking"),false);
  assert.deepEqual(latestChecks(null),[]);
  assert.deepEqual(latestChecks([null,events[0]]),[events[0]]);
  assert.equal(checkSummary(events[3]),"specific reason");
  assert.equal(checkSummary({name:"future_check",status:"pass",detail:"new detail"}),"new detail");
  assert.equal(checkSummary({name:"nand_identity",status:"checking"}),"正在核对，请稍候。");

  // Exercise the renderer: latest cards and full event history remain separate
  // across refreshes, and success details use readable summaries.
  class Element {
    constructor() { this.children=[]; this.classList={toggle(){}}; this.style={}; this.dataset={}; this.textContent=""; this.attributes={}; this.listeners={}; }
    append(...children) { this.children.push(...children); }
    replaceChildren(...children) { this.children=[...children]; }
    addEventListener(name,callback) { this.listeners[name]=callback; }
    querySelector() { return new Element(); }
    focus() {}
    click() { if(this.listeners.click)this.listeners.click({target:this}); }
    setAttribute(name,value) { this.attributes[name]=value; }
    removeAttribute(name) { delete this.attributes[name]; }
  }
  const nodes=new Map();
  const element=id=>{if(!nodes.has(id)) nodes.set(id,new Element());return nodes.get(id);};
  element("connection-label").previousElementSibling=new Element();
  const doc={getElementById:element,createElement:()=>new Element(),querySelectorAll:()=>[]};
  const mounted=mount({document:doc,location:{hostname:"127.0.0.1"},
    fetch:async path=>response(path==="/api/status"?baseStatus({checks:events}):{})});
  await mounted.load();
  assert.equal(element("check-list").children.length,2);
  assert.equal(element("technical-checks").children.length,4);
  assert.equal(element("check-list").children[0].children[0].children[0].textContent,"桥接系统");
  assert.equal(element("check-list").children[0].children[0].children[1].textContent,"通过");
  assert.equal(element("check-list").children[1].children[1].textContent,"specific reason");
  assert.equal(element("technical-checks").children[0].children[2].textContent,"start");
  await mounted.poll();
  assert.equal(element("check-list").children.length,2);
  assert.equal(element("technical-checks").children.length,4);
  assert.equal(element("backup-area").hidden,false);
  assert.equal(element("backup-link").href,"/backup.zip");
  mounted.update({status:baseStatus({backup:{available:true,url:"/backup.bin"}})});
  assert.equal(element("backup-area").hidden,true,"legacy partial startup data is not advertised as a full recovery ZIP");
  mounted.update({status:baseStatus({dispatch:{status:"blocked",code:"candidate_unavailable",
    result_code:"candidate_unavailable",reason:"provider contract unavailable",
    candidate_source_revision_sha256:"0123456789abcdef".repeat(4)}})});
  const dispatchRows=element("target-details").children;
  assert(dispatchRows.some(row=>row.children[0].textContent==="分发结果"&&row.children[1].textContent==="blocked"));
  assert(dispatchRows.some(row=>row.children[0].textContent==="分发代码"&&row.children[1].textContent==="candidate_unavailable"));
  assert(dispatchRows.some(row=>row.children[0].textContent==="分发原因"&&row.children[1].textContent==="provider contract unavailable"));
  mounted.update({status:baseStatus({install_ready:false,blocked_reason:"candidate payload unsupported",
    message:"安装资格不可用；候选载荷与设备合同不匹配，可明确选择返回原厂",
    oem:{slot_verified:true,boot_records_verified:true},actions:{return_oem:true}})});
  assert.equal(element("stage-title").textContent,"安装资格未通过，可返回原厂");
  assert.equal(element("phase-badge").textContent,"暂不可安装");
  assert.equal(element("stage-message").textContent,"candidate payload unsupported");
  assert.equal(element("notice").hidden,false,"ready phase with failed install qualification must surface the reason");
  assert.equal(element("notice-title").textContent,"安装资格未通过");
  assert.equal(element("notice-copy").textContent,"candidate payload unsupported");
  assert.equal(element("install-button").disabled,true);
  assert.equal(element("install-button").textContent,"安装资格未通过");
  assert.equal(element("primary-note").textContent,"安装不可用；可经核验后返回原厂");
  assert.equal(element("return-button").disabled,false,"independent OEM return action remains available");
  mounted.update({status:baseStatus({install_ready:false,message:"",blocked_reason:"mtd facts mismatch",
    oem:{slot_verified:true,boot_records_verified:true},actions:{return_oem:true}})});
  assert.equal(element("stage-message").textContent,"mtd facts mismatch","blocked reason is the fallback when the API message is empty");
  mounted.update({status:baseStatus({install_ready:false,message:"",blocked_reason:"",
    oem:{slot_verified:true,boot_records_verified:true},actions:{return_oem:false}})});
  assert.match(element("stage-message").textContent,/安装资格未通过；安装已禁用/);
  assert.equal(element("return-button").disabled,true);
  mounted.update({status:baseStatus({operation:"return_oem",phase:"installing",stage:"recheck",install_ready:false})});
  assert.equal(element("stage-title").textContent,"正在返回原厂系统");
  assert.equal(element("install-button").textContent,"正在返回原厂…");
  mounted.update({status:baseStatus({operation:"return_oem",phase:"complete",stage:"oem_return_verified",install_ready:false})});
  assert.equal(element("stage-title").textContent,"正在重启并返回原厂系统");
  mounted.update({status:baseStatus({operation:"return_oem",phase:"complete",stage:"reboot_scheduled",install_ready:false})});
  assert.equal(element("stage-title").textContent,"正在重启并返回原厂系统");
  assert.equal(element("success").hidden,true,"OEM return must not claim U-Boot/RESET installation success");
  mounted.update({status:baseStatus({operation:"return_oem",phase:"complete",stage:"reboot_failed",install_ready:false})});
  assert.equal(element("stage-title").textContent,"原厂返回已核验，重启未成功");
  assert.equal(element("install-button").disabled,true,"failed return reboot is never retried");
  mounted.stop();

  // Reload restores the current status and check details from the two GET endpoints.
  let requests=[];
  const wizard=new Wizard(async (path,opts)=>{requests.push([path,opts]);return path==="/api/check"?response({observed:{device_tree_model:"NX30 D"},checks:[],wizard:{checks:[]}}):response(baseStatus());});
  assert.equal(await wizard.load(),true);
  assert.equal(wizard.status.phase,"ready");
  assert.equal(wizard.canInstall(),true);
  assert.deepEqual(requests.map(r=>r[0]).sort(),["/api/check","/api/status"]);
  wizard.stop();

  // HTTP errors are treated as missing state, never as a successful empty response.
  const httpFailure=new Wizard(async path=>path==="/api/check"?response({}):({ok:false,status:503,json:async()=>({})}));
  assert.equal(await httpFailure.load(),false);
  assert.equal(httpFailure.status,null);
  httpFailure.stop();

  // Backup availability is informational: installation has no backup marker dependency.
  let posts=[];
  const withoutBackup=new Wizard(async(path,opts)=>{
    if(path==="/api/status") return response(baseStatus({backup:{available:false,size_bytes:0,url:""}}));
    if(path==="/api/install") {posts.push([path,opts]); return response({accepted:true});}
    return response({});
  });
  await withoutBackup.load();
  assert.equal(withoutBackup.status.backup.available,false);
  assert.equal(withoutBackup.canInstall(),true);
  assert.equal(await withoutBackup.install(),true);
  assert.equal(posts.length,1);
  assert.equal(posts[0][1].headers["Content-Type"],"application/x-www-form-urlencoded");
  assert.equal(posts[0][1].body,"token=token%20%2F%20value");
  withoutBackup.stop();

  // A lost POST response must not trigger a second POST; polling only reads status.
  let postCount=0, getCount=0;
  const disconnected=new Wizard(async(path)=>{
    if(path==="/api/check") return response({});
    if(path==="/api/status") {getCount++; return response(baseStatus());}
    if(path==="/api/install") {postCount++; throw new Error("connection lost after request");}
    throw new Error("unexpected endpoint");
  });
  await disconnected.load();
  assert.equal(await disconnected.install(),false);
  assert.equal(disconnected.uncertain,true);
  assert.equal(disconnected.canInstall(),false);
  assert.equal(await disconnected.install(),false);
  assert.equal(postCount,1);
  await disconnected.poll();
  assert.equal(postCount,1);
  assert.ok(getCount>=2);
  disconnected.stop();

  // Reboot stays unavailable until both completion phase and can_reboot are reported.
  let rebootPosts=0;
  const reboot=new Wizard(async(path)=>{
    if(path==="/api/status") return response(baseStatus({phase:"complete",install_ready:true,can_reboot:false}));
    if(path==="/api/reboot") {rebootPosts++; return response({accepted:true});}
    return response({});
  });
  await reboot.load();
  assert.equal(reboot.canReboot(),false);
  assert.equal(await reboot.reboot(),false);
  assert.equal(rebootPosts,0);
  reboot.status.can_reboot=true;
  assert.equal(reboot.canReboot(),true);
  reboot.connected=false;
  assert.equal(reboot.canReboot(),false);
  reboot.connected=true;
  assert.equal(await reboot.reboot(),true);
  assert.equal(rebootPosts,1);
  reboot.stop();

  // A delayed reboot stays under read-only polling until its result is known.
  // Fake timers exercise scheduled polling without waiting or invoking a reboot.
  const savedTimeout=global.setTimeout, savedClearTimeout=global.clearTimeout;
  const timers=new Map(); let nextTimer=0, scheduledReads=0, scheduledPosts=0;
  const scheduledStatuses=[
    baseStatus({phase:"complete",stage:"verified",can_reboot:true}),
    baseStatus({phase:"complete",stage:"reboot_scheduled",can_reboot:false}),
    baseStatus({phase:"complete",stage:"reboot_scheduled",can_reboot:false}),
    baseStatus({phase:"complete",stage:"reboot_failed",can_reboot:false,
      message:"安装已完成，但设备重启失败"})
  ];
  let scheduled;
  try {
    global.setTimeout=callback=>{const id=++nextTimer;timers.set(id,callback);return id;};
    global.clearTimeout=id=>timers.delete(id);
    scheduled=new Wizard(async(path,options)=>{
      if(path==="/api/status") return response(scheduledStatuses[scheduledReads++]);
      if(path==="/api/reboot") {scheduledPosts++;return response({accepted:true});}
      assert.equal(options.method,"GET");
      return response({});
    });
    await scheduled.load();
    assert.equal(await scheduled.reboot(),true);
    assert.equal(scheduled.status.stage,"reboot_scheduled");
    async function fireNextTimer() {
      const [id,callback]=timers.entries().next().value;
      timers.delete(id);
      await callback();
    }
    await fireNextTimer();
    assert.equal(scheduled.status.stage,"reboot_scheduled");
    assert.equal(timers.size,1,"waiting for reboot must continue polling");
    await fireNextTimer();
    assert.equal(scheduled.status.stage,"reboot_failed");
    assert.equal(scheduled.status.message,"安装已完成，但设备重启失败");
    assert.equal(scheduled.canReboot(),false);
    assert.equal(timers.size,0,"definitive reboot failure must stop active polling");
    assert.equal(scheduledPosts,1,"status recovery must never repeat the reboot POST");
    assert.equal(scheduledReads,4);
  } finally {
    if(scheduled) scheduled.stop();
    global.setTimeout=savedTimeout;
    global.clearTimeout=savedClearTimeout;
  }

  // A rejected status must never allow an optimistic install button state.
  const blocked=new Wizard(async(path)=> path==="/api/check"?response({}):response(baseStatus({phase:"blocked",install_ready:false,blocked_reason:"型号不符"})));
  await blocked.load();
  assert.equal(blocked.canInstall(),false);
  blocked.stop();

  for (const state of [
    {phase:"failed",install_ready:true,may_have_modified:false},
    {phase:"ready",install_ready:true,may_have_modified:true},
    {phase:"blocked",install_ready:true,may_have_modified:false}
  ]) {
    const unsafe=new Wizard(async path=>path==="/api/check"?response({}):response(baseStatus(state)));
    await unsafe.load();
    assert.equal(unsafe.canInstall(),false,`unsafe state ${JSON.stringify(state)} must not allow install`);
    unsafe.stop();
  }

  // A newer status poll wins if the initial read resolves late (manual refresh/poll race).
  const deferred=[];
  const racing=new Wizard(async path=>{
    if(path==="/api/check") return response({});
    if(path==="/api/status") return new Promise(resolve=>deferred.push(resolve));
    throw new Error("unexpected request");
  });
  const initialLoad=racing.load();
  while(deferred.length<1) await new Promise(resolve=>setImmediate(resolve));
  const newerPoll=racing.poll();
  while(deferred.length<2) await new Promise(resolve=>setImmediate(resolve));
  deferred[1](response(baseStatus({phase:"blocked",install_ready:false,blocked_reason:"newer status"})));
  await newerPoll;
  deferred[0](response(baseStatus({phase:"ready",install_ready:true,message:"stale status"})));
  await initialLoad;
  assert.equal(racing.status.phase,"blocked");
  assert.equal(racing.status.blocked_reason,"newer status");
  racing.stop();

  // A non-OK install response remains uncertain and cannot trigger an automatic retry.
  let failedPosts=0;
  const postFailure=new Wizard(async path=>{
    if(path==="/api/check") return response({});
    if(path==="/api/status") return response(baseStatus());
    if(path==="/api/install") {failedPosts++; return {ok:false,status:500,json:async()=>({})};}
  });
  await postFailure.load();
  assert.equal(await postFailure.install(),false);
  assert.equal(postFailure.uncertain,true);
  await postFailure.poll();
  assert.equal(failedPosts,1);
  postFailure.stop();

  // A stale ready poll or manual reload after an uncertain POST cannot unlock
  // another install. The latch follows the server boot/session, not connectivity.
  assert.equal(disconnected.canInstall(),false);
  let latchPosts=0;
  const latch=new Wizard(async path=>{
    if(path==="/api/status")return response(baseStatus());
    if(path==="/api/install"){latchPosts++;throw Error("lost accepted response");}
    return response({});
  });
  await latch.load();await latch.install();await latch.poll();await latch.load();
  assert.equal(latch.canInstall(),false);
  assert.equal(await latch.install(),false);
  assert.equal(latchPosts,1);
  latch.update({status:baseStatus({session:"new-boot-session"})});
  assert.equal(latch.canInstall(),true);
  latch.stop();

  // OEM actions are explicit capabilities, never inferred from an ordinary
  // reboot permission, a retail name, a NAND ID or a successful install.
  let oemPosts=[];
  const oem=new Wizard(async(path,opts)=>{
    if(opts&&opts.method==="POST")oemPosts.push(path);
    if(path==="/api/status")return response(oem.status||baseStatus());
    return response({});
  });
  await oem.load();
  assert.equal(oem.canReturnOEM(),false);
  assert.equal(await oem.returnOEM(),false);
  oem.update({status:baseStatus({phase:"complete",can_reboot:true})});
  assert.equal(oem.canReboot(),true);
  assert.equal(oem.canRebootOEM(),false);
  assert.equal(await oem.rebootOEM(),false);
  assert.deepEqual(oemPosts,[]);
  const proof={oem:{slot_verified:true,boot_records_verified:true},actions:{return_oem:true,reboot_oem:true},boot_policy:{verified:true,normal_boot:"selected_system",maintenance_entry:"reset",next_boot:"oem"}};
  const returnOnly=Object.assign({},proof,{install_ready:false});
  oem.update({status:baseStatus(returnOnly)});
  assert.equal(oem.canInstall(),false,"OEM return readiness must not grant candidate installation");
  assert.equal(oem.canReturnOEM(),true);
  for(const patch of [{may_have_modified:true},{may_have_modified:undefined},{oem:{slot_verified:true,boot_records_verified:false}},{actions:{return_oem:"true"}}]){
    oem.update({status:baseStatus(Object.assign({},proof,patch))});
    assert.equal(oem.canReturnOEM(),false,JSON.stringify(patch));
  }
  oem.update({status:baseStatus(Object.assign({},proof,{phase:"failed",may_have_modified:false}))});
  assert.equal(oem.canReturnOEM(),true,"an independently verified pre-write failure may return to OEM");
  oem.update({status:baseStatus(Object.assign({},proof,{phase:"complete",can_reboot:true}))});
  assert.equal(oem.canRebootOEM(),true);
  for(const policy of [null,{verified:false},{verified:true,normal_boot:"web",maintenance_entry:"reset",next_boot:"oem"},{verified:true,normal_boot:"selected_system",maintenance_entry:"reset",next_boot:"bridge"}]){
    oem.update({status:baseStatus(Object.assign({},proof,{phase:"complete",can_reboot:true,boot_policy:policy}))});
    assert.equal(oem.canRebootOEM(),false);
  }
  oem.update({status:baseStatus(returnOnly)});
  assert.equal(await oem.returnOEM(),true);
  assert.deepEqual(oemPosts,["/api/return-oem"]);
  assert.equal(await oem.returnOEM(),false);
  oem.stop();

  // Maintenance entry must be an explicitly verified supported enum. Legacy
  // reset evidence remains valid; UART evidence grants the same gated action
  // while the displayed recovery instructions follow the UART path exactly.
  const uartProof=Object.assign({},proof,{boot_policy:{verified:true,normal_boot:"selected_system",maintenance_entry:"uart",reset_verified:false,next_boot:"oem"}});
  oem.update({status:baseStatus(Object.assign({},uartProof,{phase:"complete",can_reboot:true}))});
  assert.equal(oem.maintenanceEntry(),"uart");
  assert.equal(oem.canRebootOEM(),true);
  for(const entry of ["unknown","UART","serial","reset ",1,null]) {
    const invalid=Object.assign({},proof,{boot_policy:{verified:true,normal_boot:"selected_system",maintenance_entry:entry,next_boot:"oem"}});
    oem.update({status:baseStatus(Object.assign({},invalid,{phase:"complete",can_reboot:true}))});
    assert.equal(oem.maintenanceEntry(),null,`invalid maintenance entry ${JSON.stringify(entry)} must be rejected`);
    assert.equal(oem.canRebootOEM(),false,`invalid maintenance entry ${JSON.stringify(entry)} must not grant OEM reboot`);
  }
  const unverifiedUART=Object.assign({},uartProof,{boot_policy:Object.assign({},uartProof.boot_policy,{verified:false})});
  oem.update({status:baseStatus(Object.assign({},unverifiedUART,{phase:"complete",can_reboot:true}))});
  assert.equal(oem.canRebootOEM(),false,"UART entry requires explicit policy verification");
  const unverifiedReset=Object.assign({},proof,{boot_policy:Object.assign({},proof.boot_policy,{reset_verified:false})});
  oem.update({status:baseStatus(Object.assign({},unverifiedReset,{phase:"complete",can_reboot:true}))});
  assert.equal(oem.canRebootOEM(),false,"a reset entry explicitly marked unverified cannot be advertised");
  oem.stop();

  // Rendered success and confirmation copy describe the actual UART entry,
  // explicitly preserving the unverified status of physical RESET.
  const uiNodes=new Map();
  const uiElement=id=>{if(!uiNodes.has(id))uiNodes.set(id,new Element());return uiNodes.get(id);};
  uiElement("connection-label").previousElementSibling=new Element();
  const uiDoc={getElementById:uiElement,createElement:()=>new Element(),querySelectorAll:()=>[],activeElement:new Element()};
  const uartStatus=baseStatus(Object.assign({},uartProof,{phase:"complete",install_ready:false,can_reboot:true}));
  const uartUI=mount({document:uiDoc,location:{hostname:"127.0.0.1"},fetch:async path=>response(path==="/api/status"?uartStatus:{})});
  await uartUI.load();
  assert.equal(uartUI.canRebootOEM(),true);
  assert.equal(uiElement("success").hidden,false);
  assert.match(uiElement("success-maintenance-title").textContent,/串口中断/);
  assert.match(uiElement("success-maintenance-copy").textContent,/3 秒内/);
  assert.match(uiElement("success-maintenance-copy").textContent,/192\.168\.10\.1/);
  assert.match(uiElement("success-maintenance-copy").textContent,/物理 RESET 入口未核验/);
  uiElement("install-button").click();
  assert.match(uiElement("dialog-copy").textContent,/3 秒内通过串口/);
  assert.match(uiElement("dialog-copy").textContent,/物理 RESET 入口未核验/);
  const unknownStatus=baseStatus(Object.assign({},uartProof,{phase:"complete",install_ready:false,can_reboot:true,
    boot_policy:{verified:true,normal_boot:"selected_system",maintenance_entry:"unknown",next_boot:"oem"}}));
  uartUI.update({status:unknownStatus});
  assert.equal(uartUI.canRebootOEM(),false);
  assert.equal(uiElement("success").hidden,true,"unknown maintenance entries never show success instructions");
  assert.equal(uiElement("install-button").disabled,true,"unknown maintenance entries cannot enable reboot");
  uartUI.stop();

  // Missing/non-boolean safety facts cannot become an optimistic install grant.
  for(const patch of [{may_have_modified:undefined},{may_have_modified:"false"},{install_ready:"true"},{token:""}]){
    const strict=new Wizard(async path=>response(path==="/api/status"?baseStatus(patch):{}));
    await strict.load();assert.equal(strict.canInstall(),false,JSON.stringify(patch));strict.stop();
  }
  console.log("wizard UI state checks passed");
}

run().catch(error=>{console.error(error);process.exitCode=1;});
