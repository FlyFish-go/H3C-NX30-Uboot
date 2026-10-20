"use strict";
// Actual production HTML/CSS/JS + native C HTTP + synthetic NAND only.
const {spawn}=require("node:child_process");
const fs=require("node:fs"),path=require("node:path"),assert=require("node:assert/strict");
const configuredOutput=process.env.BOOTBRIDGE_BROWSER_OUTPUT_ROOT||process.env.BRIDGE_UI_BROWSER_OUTPUT;
const defaultOutputParent=path.resolve("work/nand-oem-repair-20261017/private");
let output;
if(configuredOutput){
  output=path.resolve(configuredOutput);
  fs.mkdirSync(path.dirname(output),{recursive:true});
  // Refuse to overwrite any previous evidence when a caller supplies a path.
  fs.mkdirSync(output);
}else{
  fs.mkdirSync(defaultOutputParent,{recursive:true});
  output=fs.mkdtempSync(path.join(defaultOutputParent,"browser-"));
}
const diagnosticOutput=path.join(output,"downloads/bootkit-bridge-diagnostics.json");
if(fs.existsSync(diagnosticOutput))fs.unlinkSync(diagnosticOutput);
const fixture=spawn("python3",[path.join(__dirname,"browser_fixture.py")],{stdio:["pipe","pipe","pipe"]});
let fixtureText="",fixtureErrors="",browserErrors="",browser,origin,seq=0,buffer="",pending=new Map(),errors=[],requests=[],logs=[],override=null,sessionId;
fixture.stderr.on("data",x=>fixtureErrors+=x);
fixture.stdout.on("data",x=>fixtureText+=x);
const delay=n=>new Promise(r=>setTimeout(r,n));
function call(method,params={}){return new Promise((resolve,reject)=>{const id=++seq;pending.set(id,{resolve,reject});browser.stdio[3].write(JSON.stringify({id,method,params,...(sessionId?{sessionId}:{})})+"\0");});}
async function handle(m){
  if(pending.has(m.id)){const p=pending.get(m.id);pending.delete(m.id);m.error?p.reject(Error(JSON.stringify(m.error))):p.resolve(m.result);return;}
  if(m.method==="Runtime.exceptionThrown")errors.push(m.params);
  if(m.method==="Log.entryAdded"&&m.params.entry.level==="error")logs.push(m.params.entry);
  if(m.method==="Network.requestWillBeSent")requests.push({url:m.params.request.url,method:m.params.request.method,postData:m.params.request.postData});
  if(m.method==="Fetch.requestPaused"){
    const {requestId,request}=m.params,p=new URL(request.url).pathname;
    if(override&&p==="/api/status")await call("Fetch.fulfillRequest",{requestId,responseCode:200,responseHeaders:[{name:"Content-Type",value:"application/json"}],body:Buffer.from(JSON.stringify(override.status)).toString("base64")});
    else if(override&&p==="/api/return-oem")await call("Fetch.fulfillRequest",{requestId,responseCode:202,responseHeaders:[{name:"Content-Type",value:"application/json"}],body:Buffer.from(JSON.stringify({accepted:true})).toString("base64")});
    else if(override&&p==="/api/install"&&override.loseInstall)await call("Fetch.failRequest",{requestId,errorReason:"ConnectionReset"});
    else await call("Fetch.continueRequest",{requestId});
  }
}
async function evaluate(expression,awaitPromise=false){const r=await call("Runtime.evaluate",{expression,returnByValue:true,awaitPromise});if(r.exceptionDetails)throw Error(JSON.stringify(r.exceptionDetails));return r.result.value;}
async function waitFor(expression){for(let i=0;i<100;i++){if(await evaluate(expression))return;await delay(100);}throw Error("Timed out: "+expression);}
const ui=`(()=>({phase:document.getElementById('app').dataset.state,title:document.getElementById('stage-title').textContent,message:document.getElementById('stage-message').textContent,button:document.getElementById('install-button').textContent,disabled:document.getElementById('install-button').disabled,backDisabled:document.getElementById('return-button').disabled,detailsOpen:document.getElementById('technical').open,backup:!document.getElementById('backup-area').hidden,success:!document.getElementById('success').hidden,caption:document.getElementById('primary-note').textContent,notice:!document.getElementById('notice').hidden,noticeTitle:document.getElementById('notice-title').textContent,noticeCopy:document.getElementById('notice-copy').textContent}))()`;
async function navigate(){await call("Page.navigate",{url:origin+"/"});await waitFor("document.getElementById('stage-title')&&document.getElementById('stage-title').textContent!=='正在读取设备状态'");await delay(150);}
(async()=>{
  for(let i=0;i<200&&!fixtureText.includes("\n");i++){if(fixture.exitCode!==null)throw Error(fixtureErrors);await delay(50);}
  origin=JSON.parse(fixtureText.split("\n")[0]).origin;
  assert.match(origin,/^http:\/\/127\.0\.0\.1:\d+$/);
  browser=spawn(process.env.CHROMIUM||"/usr/bin/chromium",["--headless","--no-sandbox","--disable-gpu","--disable-background-networking","--disable-component-update","--disable-sync","--no-first-run","--no-default-browser-check","--disable-extensions","--host-resolver-rules=MAP * 0.0.0.0,EXCLUDE 127.0.0.1","--user-data-dir="+output+"/profile","--remote-debugging-pipe"],{env:{...process.env,TMPDIR:process.env.BOOTBRIDGE_BROWSER_TMPDIR||"/tmp"},stdio:["ignore","ignore","pipe","pipe","pipe"]});
  browser.stderr.on("data",chunk=>browserErrors+=chunk.toString());
  const browserFailure=error=>{for(const p of pending.values())p.reject(new Error(String(error)+"\n"+browserErrors));pending.clear();};
  browser.on("error",browserFailure);browser.on("exit",code=>browserFailure("Chromium exited: "+code));
  browser.stdio[3].on("error",browserFailure);browser.stdio[4].on("error",browserFailure);
  browser.stdio[4].on("data",chunk=>{buffer+=chunk;let end;while((end=buffer.indexOf("\0"))>=0){const raw=buffer.slice(0,end);buffer=buffer.slice(end+1);if(raw)handle(JSON.parse(raw)).catch(e=>errors.push(String(e)));}});
  const {targetId}=await call("Target.createTarget",{url:"about:blank"});
  ({sessionId}=await call("Target.attachToTarget",{targetId,flatten:true}));
  await call("Runtime.enable");await call("Page.enable");await call("Network.enable");await call("Log.enable");
  await call("Browser.setDownloadBehavior",{behavior:"allow",downloadPath:output+"/downloads"});
  await call("Fetch.enable",{patterns:[{urlPattern:"*/api/status"},{urlPattern:"*/api/install"},{urlPattern:"*/api/return-oem"}]});
  await call("Emulation.setDeviceMetricsOverride",{width:1280,height:900,deviceScaleFactor:1,mobile:false});
  await navigate();await waitFor("document.getElementById('app').dataset.state==='ready'");
  const baseline=await evaluate("fetch('/api/status').then(r=>r.json())",true);
  for (const stage of ["checking","ready"]) {
    override={status:{...baseline,phase:stage,install_ready:stage==="ready"}};
    await navigate();
    assert.equal(await evaluate("document.getElementById('return-button').hidden"),false,"OEM return entry stays visible during first-stage "+stage);
    assert.equal(await evaluate("document.getElementById('quick-diagnostics-button').hidden"),false,"diagnostic entry stays visible during first-stage "+stage);
    if(stage==="checking") assert.equal(await evaluate("document.getElementById('return-button').disabled"),true);
  }
  override=null; await navigate();
  let view=await evaluate(ui);
  assert.equal(view.disabled,false);assert.equal(view.backDisabled,false);assert.equal(view.detailsOpen,false);
  assert.ok(!requests.some(r=>r.method==="POST"),"page load must only read");
  assert.equal(await evaluate("document.querySelectorAll('.bk-preview,select,footer,.bk-address,.bk-side-foot').length"),0);
  assert.equal(await evaluate("document.querySelector('.bk-author a').href"),"https://github.com/FlyFish-go");
  const backupHead=await evaluate("fetch('/backup.zip',{method:'HEAD'}).then(r=>({status:r.status,type:r.headers.get('content-type'),bytes:Number(r.headers.get('content-length'))}))",true);
  assert.equal(backupHead.status,200);assert.equal(backupHead.type,'application/zip');assert(backupHead.bytes>45*1024*1024);
  for(const width of [1024,1280,1440]){
    await call("Emulation.setDeviceMetricsOverride",{width,height:900,deviceScaleFactor:1,mobile:false});
    assert.equal(await evaluate("document.documentElement.scrollWidth<=innerWidth"),true);
  }
  await call("Emulation.setDeviceMetricsOverride",{width:1280,height:900,deviceScaleFactor:1,mobile:false});
  await call("Emulation.setEmulatedMedia",{features:[{name:"prefers-color-scheme",value:"dark"}]});
  assert.equal(await evaluate("getComputedStyle(document.getElementById('bootkit-bridge-proposal')).getPropertyValue('--bk-color-0').trim()"),"#0e1419");
  await call("Emulation.setEmulatedMedia",{features:[{name:"prefers-color-scheme",value:"light"}]});
  let shot=await call("Page.captureScreenshot",{format:"png",captureBeyondViewport:true});fs.writeFileSync(output+"/ready.png",Buffer.from(shot.data,"base64"));
  // Real HTTP install, exact form header/token and no backup prerequisite.
  await evaluate("document.getElementById('technical').open=true");
  await evaluate("document.getElementById('install-button').click()");
  assert.equal(await evaluate("document.getElementById('confirm-overlay').hidden"),false);
  assert.equal(await evaluate("(()=>{const r=document.querySelector('.bk-dialog').getBoundingClientRect();return r.top>=0&&r.bottom<=innerHeight;})()"),true,"expanded diagnostics must not push confirmation off-screen");
  await call("Input.dispatchKeyEvent",{type:"keyDown",key:"Escape",code:"Escape"});
  assert.equal(await evaluate("document.getElementById('confirm-overlay').hidden"),true);
  assert.equal(requests.filter(r=>r.method==="POST").length,0);
  await evaluate("document.getElementById('technical').open=false");
  await evaluate("document.getElementById('install-button').click()");
  await evaluate("document.getElementById('confirm-button').click()");
  await waitFor("document.getElementById('app').dataset.state==='complete'");
  view=await evaluate(ui);assert.equal(view.disabled,false);assert.equal(view.success,true);
  assert.match(view.caption,/手动重启/);
  assert.equal(requests.filter(r=>r.method==="POST"&&r.url===origin+"/api/install").length,1);
  assert.equal(requests.filter(r=>r.method==="POST"&&r.url.includes("reboot")).length,0);
  const cases=[{case:"native C synthetic installation + verified OEM selector; reboot awaits click",...view}];
  shot=await call("Page.captureScreenshot",{format:"png",captureBeyondViewport:true});fs.writeFileSync(output+"/complete.png",Buffer.from(shot.data,"base64"));
  await evaluate("document.getElementById('technical').open=true;document.getElementById('diagnostics-button').click()");
  const diagnosticPath=output+"/downloads/bootkit-bridge-diagnostics.json";
  for(let i=0;i<50&&!fs.existsSync(diagnosticPath);i++)await delay(50);
  const diagnostic=JSON.parse(fs.readFileSync(diagnosticPath,"utf8"));
  assert.equal(diagnostic.phase,"complete");assert.equal(diagnostic.token,undefined);assert.equal(diagnostic.session,undefined);
  // Contract-only state coverage: fulfilled status responses are labelled here;
  // these overrides test display contracts separately from the native C fixture.
  for(const phase of ["checking","ready","installing","complete","blocked","failed"]){
    override={status:{...baseline,actions:{},oem:{},boot_policy:{},phase,install_ready:phase==="ready",may_have_modified:["installing","complete","failed"].includes(phase),can_reboot:phase==="complete",message:"<img src=x onerror=alert(1)> fixture status",blocked_reason:phase==="blocked"?"synthetic blocked reason":"",stage:phase,checks:[{name:"fixture",status:phase==="ready"?"pass":"blocked",detail:"<script>unsafe</script>"}]}};
    await navigate();await waitFor(`document.getElementById('app').dataset.state===${JSON.stringify(phase)}`);
    view=await evaluate(ui);assert.equal(view.disabled,!["ready","failed"].includes(phase));assert.equal(view.backDisabled,true);
    assert.equal(await evaluate("document.querySelectorAll('#technical script,#technical img').length"),0);
    cases.push({case:"synthetic status contract",...view});
    if(phase==="failed"){
      const before=requests.filter(r=>r.method==="POST").length;
      assert.equal(view.backDisabled,true,"possible partial write freezes OEM return");
      await evaluate("document.getElementById('install-button').click()");
      await delay(100);
      assert.equal(requests.filter(r=>r.method==="POST").length,before,"failed partial write cannot retry or reboot");
      assert.equal(requests.some(r=>r.method==="POST"&&r.url.includes("reboot")),false,"no automatic reboot after partial write failure");
    }
  }
  override={status:{...baseline,phase:"ready",install_ready:false,may_have_modified:false,
    message:"安装资格不可用；候选载荷与设备合同不匹配，可明确选择返回原厂",
    blocked_reason:"候选载荷与设备合同不匹配",
    oem:{slot_verified:true,boot_records_verified:true},actions:{return_oem:true},
    boot_policy:{verified:false,normal_boot:"unknown",maintenance_entry:"unknown",next_boot:"unknown"}}};
  await navigate();await waitFor("document.getElementById('app').dataset.state==='ready'");
  view=await evaluate(ui);assert.equal(view.disabled,true,"candidate install stays disabled when only OEM return is ready");
  assert.equal(view.backDisabled,false,"independently verified OEM return remains available");
  assert.equal(view.title,"安装资格未通过，可返回原厂");
  assert.match(view.message,/候选载荷与设备合同不匹配/);
  assert.equal(view.button,"安装资格未通过");
  assert.equal(view.notice,true);assert.equal(view.noticeTitle,"安装资格未通过");
  assert.match(view.noticeCopy,/候选载荷与设备合同不匹配/);
  assert.match(view.caption,/可经核验后返回原厂/);
  cases.push({case:"return-only readiness leaves install disabled",...view});
  // Explicit OEM proof affects UI capability only, without submitting a made-up
  // backend request or claiming a successful return/reboot.
  override={status:{...baseline,phase:"complete",can_reboot:true,stage:"verified",oem:{slot_verified:true,boot_records_verified:true},actions:{reboot_oem:true},boot_policy:{verified:true,normal_boot:"selected_system",maintenance_entry:"reset",next_boot:"oem"}}};
  await navigate();await waitFor("document.getElementById('install-button').disabled===false");
  assert.equal((await evaluate(ui)).success,true);
  // A lost install response and repeated stale ready reads cannot enable a retry.
  override={status:{...baseline},loseInstall:true};await navigate();
  await evaluate("document.getElementById('install-button').click();document.getElementById('confirm-button').click()");
  await delay(1900);view=await evaluate(ui);assert.equal(view.disabled,true);assert.match(view.caption,/只发送一次|重复|等待/);
  assert.equal(requests.filter(r=>r.method==="POST"&&r.url===origin+"/api/install").length,2);
  // First-stage rejection keeps diagnostics and the independent OEM option
  // available even when no token/capability is present.
  override={status:{...baseline,phase:"blocked",install_ready:false,token:"",may_have_modified:false,
    message:"闪存配置不匹配",blocked_reason:"闪存配置不匹配",
    dispatch:{status:"rejected",code:"NAND_UNMATCHED",reason:"未找到唯一匹配的闪存配置"},
    nand_profile:{id:"",model:"未知"},source:{product:"NX30",route:"none"},
    oem_return:{available:false,reason:"未能核验原厂启动记录"},actions:{return_oem:false},oem:{},checks:[]}};
  await navigate();await waitFor("document.getElementById('app').dataset.state==='blocked'");
  assert.equal(await evaluate("document.getElementById('return-button').hidden"),false);
  assert.equal(await evaluate("document.getElementById('return-button').disabled"),true);
  assert.equal(await evaluate("document.getElementById('quick-diagnostics-button').hidden"),false);
  assert.match(await evaluate("document.getElementById('capability-note').textContent"),/未能核验原厂启动记录/);
  assert.match(await evaluate("document.getElementById('stage-message').textContent"),/闪存配置不匹配/);
  if(fs.existsSync(diagnosticPath))fs.unlinkSync(diagnosticPath);
  await evaluate("document.getElementById('quick-diagnostics-button').click()");
  for(let i=0;i<50&&!fs.existsSync(diagnosticPath);i++)await delay(50);
  const blockedDiagnostic=JSON.parse(fs.readFileSync(diagnosticPath,"utf8"));
  assert.equal(blockedDiagnostic.dispatch.code,"NAND_UNMATCHED");assert.equal(blockedDiagnostic.nand_profile.model,"未知");
  assert.equal(blockedDiagnostic.token,undefined,"diagnostics do not require or expose a token");
  // A dispatcher failure with incomplete facts leaves return disabled while
  // keeping a useful local diagnostic download available.
  override={status:{...baseline,phase:"blocked",install_ready:false,token:"",may_have_modified:false,
    message:"无法唯一匹配设备闪存配置",blocked_reason:"无法唯一匹配设备闪存配置",
    dispatch:{status:"rejected",code:"NAND_DISPATCH_AMBIGUOUS",reason:"闪存身份与几何无法唯一匹配目录"},
    nand_profile:{id:"",model:"不确定"},source:{product:"未知",route:"none",source_sha256:""},
    candidate:null,oem_return:{available:false,reason:"原厂启动记录字段不完整，返回能力不可用"},
    actions:{return_oem:false},oem:{slot_verified:false,boot_records_verified:false},checks:[]}};
  await navigate();await waitFor("document.getElementById('app').dataset.state==='blocked'");
  assert.equal(await evaluate("document.getElementById('return-button').hidden"),false);
  assert.equal(await evaluate("document.getElementById('return-button').disabled"),true);
  assert.equal(await evaluate("document.getElementById('quick-diagnostics-button').hidden"),false);
  assert.match(await evaluate("document.getElementById('capability-note').textContent"),/原厂启动记录字段不完整/);
  assert.match(await evaluate("document.getElementById('stage-message').textContent"),/无法唯一匹配设备闪存配置/);
  if(fs.existsSync(diagnosticPath))fs.unlinkSync(diagnosticPath);
  await evaluate("document.getElementById('quick-diagnostics-button').click()");
  for(let i=0;i<50&&!fs.existsSync(diagnosticPath);i++)await delay(50);
  const dispatchDiagnostic=JSON.parse(fs.readFileSync(diagnosticPath,"utf8"));
  assert.equal(dispatchDiagnostic.dispatch.code,"NAND_DISPATCH_AMBIGUOUS");
  assert.equal(dispatchDiagnostic.token,undefined);
  cases.push({case:"dispatch failure: no unique NAND profile, return disabled, diagnostics available"});
  // Every production dispatcher rejection code is paired with both backend
  // OEM-return decisions. Install/dispatch rejection alone must not prevent a
  // separately qualified return, while an unqualified return stays disabled.
  // These are injected UI status contracts, not device-generated rejections.
  const dispatchRejects=[
    ["runtime_guard","启动环境未通过只读校验"],
    ["runtime_facts_missing","无法读取完整设备信息"],
    ["runtime_compatible_invalid","设备树路由信息格式异常"],
    ["runtime_nand_identity_invalid","内核未提供唯一有效的 NAND 型号与几何信息"],
    ["runtime_mtd_sysfs_invalid","MTD 类型、偏移或几何信息无法核对"],
    ["runtime_mtd_device_mismatch","MTD 设备号与内核设备映射不一致"],
    ["runtime_proc_mtd_mismatch","内核 MTD 分区表与 sysfs 信息不一致"],
    ["runtime_facts_conflict","设备信息有冲突，无法唯一识别"],
    ["no_provider_match","此设备不在当前原厂版本的适配范围内"],
    ["candidate_oem_contract_unavailable","来源、SBL、APPSBL 或分区布局完整只读合同未通过"],
    ["candidate_contract_ambiguous","多个候选的完整原厂合同均通过"],
    ["provider_match_ambiguous","多个完整原厂来源同时匹配"],
    ["provider_bundle_invalid","桥接包内的设备配置表不完整"],
    ["resource_hash_mismatch","内置候选文件校验失败"],
    ["resource_file_invalid","内置候选文件不可安全读取"],
    ["resource_path_invalid","内置候选文件不可安全读取"],
    ["provider_exec_failed","provider 启动失败"],
    ["dispatch_blocked","设备配置或内置文件验证失败"]
  ];
  const rejectionCases=[];
  for(const [code,dispatchReason] of dispatchRejects){
    for(const returnEligible of [false,true]){
      const returnReason=returnEligible?`原厂启动记录已核验：${code}`:`原厂返回资格未通过：${code}`;
      override={status:{...baseline,phase:"blocked",install_ready:false,token:returnEligible?"synthetic-return-token":"",may_have_modified:false,
        message:dispatchReason,blocked_reason:dispatchReason,
        dispatch:{status:"blocked",code,reason:dispatchReason,return_reason:returnReason},
        oem_return:{available:returnEligible,reason:returnReason},
        oem:{slot_verified:returnEligible,boot_records_verified:returnEligible},actions:{return_oem:returnEligible},checks:[]}};
      await navigate();await waitFor("document.getElementById('app').dataset.state==='blocked'");
      assert.equal(await evaluate("document.getElementById('return-button').hidden"),false,code+" keeps the OEM return entry visible");
      assert.equal(await evaluate("document.getElementById('return-button').disabled"),!returnEligible,code+" reflects only backend return qualification");
      assert.match(await evaluate("document.getElementById('return-button').title"),new RegExp(returnEligible?"已核验":code));
      assert.match(await evaluate("document.getElementById('capability-note').textContent"),new RegExp(returnEligible?"已核验":code));
      assert.match(await evaluate("document.getElementById('stage-message').textContent"),new RegExp(dispatchReason));
      assert.equal(await evaluate("document.getElementById('quick-diagnostics-button').hidden"),false);
      if(fs.existsSync(diagnosticPath))fs.unlinkSync(diagnosticPath);
      await evaluate("document.getElementById('quick-diagnostics-button').click()");
      for(let i=0;i<50&&!fs.existsSync(diagnosticPath);i++)await delay(50);
      const rejectionDiagnostic=JSON.parse(fs.readFileSync(diagnosticPath,"utf8"));
      assert.equal(rejectionDiagnostic.dispatch.code,code);
      assert.equal(rejectionDiagnostic.dispatch.reason,dispatchReason);
      assert.equal(rejectionDiagnostic.oem_return.reason,returnReason);
      assert.equal(rejectionDiagnostic.token,undefined);
      rejectionCases.push({kind:"dispatcher status-contract fixture",code,return_eligible:returnEligible,
        return_reason_visible:true,diagnostic_preserved:true});
    }
  }
  // Provider/preflight rejection labels are likewise rendered as real check
  // events. The exact gate names mirror install_unavailable()/add_check() in
  // bridge_install.c; the test injects their published status shape and does
  // not claim the device produced each error.
  const providerRejects=[
    "runtime_source","bridge_runtime","mtd_map","nand_identity","nand_catalog",
    "env_structure","bootconfig","active_appsbl","write_targets",
    "oem_slot","protected_snapshot","appsbl_read_observation","first_install_record","payload",
    "install_identity","qfprom_authenticate","candidate_format","sbl1","appsblenv",
    "machid","candidate_target","candidate_payload","trusted_version",
    "oem_install_handoff","install_chain_qualification","oem_boot_contract","oem_return_contract"
  ];
  const unavailableChecks=new Set(["oem_return_contract","install_chain_qualification","oem_boot_contract"]);
  for(const name of providerRejects){
    for(const returnEligible of [false,true]){
      const detail=`设备资格检查未通过：${name}`;
      const returnReason=returnEligible?`原厂返回独立核验通过：${name}`:`独立原厂返回资格未通过：${name}`;
      override={status:{...baseline,phase:"ready",install_ready:false,token:returnEligible?"synthetic-return-token":"",may_have_modified:false,
        message:detail,blocked_reason:detail,dispatch:{status:"matched",code:"provider_preflight_rejected",reason:detail},
        oem_return:{available:returnEligible,reason:returnReason},oem:{slot_verified:returnEligible,boot_records_verified:returnEligible},
        actions:{return_oem:returnEligible},checks:[{name,status:unavailableChecks.has(name)?"unavailable":"blocked",detail}]}};
      await navigate();await waitFor("document.getElementById('app').dataset.state==='ready'");
      assert.equal(await evaluate("document.getElementById('install-button').disabled"),true,name+" cannot enable install");
      assert.equal(await evaluate("document.getElementById('return-button').hidden"),false,name+" keeps the return entry visible");
      assert.equal(await evaluate("document.getElementById('return-button').disabled"),!returnEligible,name+" reflects backend return qualification");
      assert.match(await evaluate("document.getElementById('capability-note').textContent"),new RegExp(returnEligible?"通过":name));
      await evaluate("document.getElementById('technical').open=true");
      assert.match(await evaluate("document.getElementById('technical-checks').textContent"),new RegExp(name));
      assert.match(await evaluate("document.getElementById('technical-checks').textContent"),new RegExp(detail));
      rejectionCases.push({kind:"provider/preflight check-contract fixture",check:name,
        install_eligible:false,return_eligible:returnEligible,reason_visible:true});
    }
  }
  cases.push({case:"all dispatcher and provider/preflight rejection reason contracts",count:rejectionCases.length,
    hardware:"NOT_EXECUTED",backend_rejections:"injected UI status fixtures",rejections:rejectionCases});
  // Candidate corruption is separately visible even when the OEM fallback is
  // independently ready; install remains disabled and the fixture is read-only.
  override={status:{...baseline,phase:"ready",install_ready:false,token:"synthetic-corrupt-candidate-token",may_have_modified:false,
    message:"候选文件摘要校验失败",blocked_reason:"候选文件摘要校验失败",
    dispatch:{status:"matched",code:"CANDIDATE_DIGEST_MISMATCH",reason:"候选摘要与可信清单不一致"},
    nand_profile:{id:"synthetic-known-profile",model:"合成已匹配 NAND 配置"},
    source:{product:"合成 NX30",route:"fixture-source",source_sha256:"synthetic-source-digest"},
    root_binding_contract:{volume_bytes:17395712,usable_leb_bytes:126976,reserved_ebs:137,mtd_write_bytes:2048,contract_sha256:"synthetic-fixture-only"},
    candidate:{version:"synthetic",sha256:"corrupt-preview-digest",bytes:123},
    oem_return:{available:true,reason:"合成原厂槽与启动记录已核验"},actions:{return_oem:true},
    oem:{slot_verified:true,boot_records_verified:true},checks:[{name:"candidate_payload",status:"fail",detail:"合成摘要不匹配"}]}};
  await navigate();await waitFor("document.getElementById('app').dataset.state==='ready'");
  assert.equal(await evaluate("document.getElementById('install-button').disabled"),true);
  assert.equal(await evaluate("document.getElementById('return-button').disabled"),false);
  assert.equal(await evaluate("document.getElementById('quick-diagnostics-button').hidden"),false);
  assert.match(await evaluate("document.getElementById('stage-message').textContent"),/候选摘要与可信清单不一致/);
  if(fs.existsSync(diagnosticPath))fs.unlinkSync(diagnosticPath);
  await evaluate("document.getElementById('quick-diagnostics-button').click()");
  for(let i=0;i<50&&!fs.existsSync(diagnosticPath);i++)await delay(50);
  const corruptDiagnostic=JSON.parse(fs.readFileSync(diagnosticPath,"utf8"));
  assert.equal(corruptDiagnostic.dispatch.code,"CANDIDATE_DIGEST_MISMATCH");
  assert.equal(corruptDiagnostic.candidate.sha256,"corrupt-preview-digest");
  assert.equal(corruptDiagnostic.root_binding_contract.contract_sha256,"synthetic-fixture-only");
  assert.equal(corruptDiagnostic.root_binding_contract.volume_bytes,17395712);
  assert.equal(corruptDiagnostic.token,undefined);
  cases.push({case:"corrupt candidate: install disabled, OEM return independently available"});
  // Backend capability independently enables return, but the single POST is
  // still gated by an explicit confirmation and carries the session token.
  override.status={...override.status,token:"synthetic-return-token",actions:{return_oem:true},oem_return:{available:true,reason:"原厂槽和启动记录已核验"},oem:{slot_verified:true,boot_records_verified:true}};
  await navigate();await waitFor("document.getElementById('return-button').disabled===false");
  assert.equal(await evaluate("document.getElementById('install-button').disabled"),true);
  assert.ok(!requests.some(r=>r.method==="POST"&&r.url===origin+"/api/return-oem"),"status load never submits return");
  await evaluate("document.getElementById('return-button').click()");
  assert.equal(await evaluate("document.getElementById('confirm-overlay').hidden"),false);
  await evaluate("document.getElementById('cancel-button').click()");
  assert.equal(requests.filter(r=>r.method==="POST"&&r.url===origin+"/api/return-oem").length,0,"cancel submits no return request");
  await evaluate("document.getElementById('return-button').click();document.getElementById('confirm-button').click();document.getElementById('confirm-button').click()");
  await waitFor("document.getElementById('stage-message').textContent.includes('请求已提交')");
  assert.equal(requests.filter(r=>r.method==="POST"&&r.url===origin+"/api/return-oem").length,1,"return request is sent exactly once");
  const returnPost=requests.find(r=>r.method==="POST"&&r.url===origin+"/api/return-oem");
  assert.equal(returnPost.postData,"token=synthetic-return-token&confirm=return-oem");
  assert.equal(await evaluate("document.getElementById('success').hidden"),true,"return action does not show install success");
  assert.ok(requests.every(r=>r.url.startsWith(origin+"/")||r.url.startsWith("blob:"+origin+"/")),"no external or device requests");
  // Lost synthetic connection is expected; other runtime failures are not.
  assert.deepEqual(errors,[]);
  const unexpectedLogs=logs.filter(l=>!l.text.includes("ERR_CONNECTION_RESET")&&!(l.url||"").endsWith("/favicon.ico"));
  assert.deepEqual(unexpectedLogs,[]);
  fs.writeFileSync(output+"/result.json",JSON.stringify({status:"PASS",output_root:output,native_http_install:true,frontend_oem_return:"synthetic response; hardware NOT_EXECUTED",backend_oem_routes:"synthetic native C slot/record integration; hardware NOT_EXECUTED",device_operations:false,cases,requests,expected_connection_reset:true},null,2));
  console.log("PASS: production DOM/assets, native C fixture install/backup, blocked-state diagnostics, token-bound confirmed OEM return, capability gates, status rendering, lost-response no-retry");
})().catch(e=>{console.error(e.stack||e);console.error(fixtureErrors);console.error(JSON.stringify({errors,logs,requests},null,2));process.exitCode=1;}).finally(async()=>{
  if(browser)browser.kill();fixture.stdin.end("done\n");await delay(150);if(fixture.exitCode===null)fixture.kill();
  fs.writeFileSync(output+"/fixture-summary.txt",fixtureText);
});
setTimeout(()=>{if(browser)browser.kill();fixture.kill();process.exit(2);},55000).unref();
