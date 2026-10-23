"use strict";
(function (root, factory) {
  const api = factory();
  if (typeof module === "object" && module.exports) module.exports = api;
  if (root && root.document) api.mount(root);
})(typeof globalThis !== "undefined" ? globalThis : this, function () {
  const ACTIVE_POLL_MS = 1600;
  const PHASES = new Set(["checking", "ready", "installing", "complete", "failed", "blocked"]);
  const phaseText = {checking:"检查中",ready:"可以安装",installing:"安装中",complete:"已完成",failed:"安装失败",blocked:"暂不可安装"};
  const checkText = {pass:"通过",ok:"通过",ready:"通过",fail:"未通过",failed:"未通过",blocked:"受阻",unknown:"未知",skip:"跳过",checking:"检查中",available:"可下载",optional:"可跳过"};
  const labels = {candidate:"候选文件",target:"安装目标",range:"目标范围",backup:"备份",integrity:"完整性",compatibility:"兼容性",device:"设备",model:"设备型号",flash:"闪存",ecc:"ECC",boot_chain:"启动链",slot:"目标槽位",qfprom_authenticate:"启动认证",bridge_profile:"桥接系统",mtd_map:"分区布局",nand_identity:"闪存身份",nand_catalog:"闪存兼容性",nand_catalog_digest:"兼容目录完整性",sbl1:"前级启动程序",appsblenv:"设备环境与型号",machid:"平台标识",bootconfig:"启动选择记录",write_targets:"安装区域",active_appsbl:"当前启动程序",candidate_payload:"内置安装文件",trusted_version:"启动程序版本",payload:"安装文件",protected_snapshot:"保护区快照"};
  const safe = (value, fallback="未提供") => value === null || value === undefined || value === "" ? fallback : String(value);

  // The API returns check events, including checking -> pass for one name.
  // Cards show the latest event; the technical table retains the complete sequence.
  function latestChecks(checks) {
    const latest=new Map();
    (Array.isArray(checks)?checks:[]).forEach((item,index)=>{
      if (!item || typeof item!=="object") return;
      const key=typeof item.name==="string" && item.name ? item.name : index;
      latest.set(key,item);
    });
    return Array.from(latest.values());
  }
  function checkSummary(item) {
    const status=String(item.status||"unknown").toLowerCase();
    if (status==="checking") return "正在核对，请稍候。";
    if (item.name==="backup" && status==="available") return "本机恢复备份已准备好，可下载保存。";
    if (item.name==="backup" && status==="optional") return "备份暂不可下载，可跳过此步骤。";
    const summaries={qfprom_authenticate:"启动认证条件符合本候选要求。",bridge_profile:"当前桥接系统与安装要求一致。",nand_identity:"闪存身份与实际几何已核对。",nand_catalog:"本机闪存符合此候选的限定条件。",nand_catalog_digest:"已绑定候选使用的兼容目录。",sbl1:"前级启动程序与已验证版本一致。",appsblenv:"设备环境完整性及产品字段已核对。",machid:"平台标识检查通过。",mtd_map:"实际分区位置与安装布局一致。",bootconfig:"两份启动选择记录一致且有效。",write_targets:"安装区域的坏块检查通过。",active_appsbl:"当前启动程序与可信版本一致。",candidate_payload:"内置安装文件完整性检查通过。",trusted_version:"已核对可信版本，禁止安装较旧版本。",protected_snapshot:"保护区快照已保存在内存中。"};
    return ["pass","ok","ready"].includes(status) && summaries[item.name] || safe(item.detail,"设备未提供说明。");
  }
  const stageText={accepted:"开始安装",recheck:"核对安装前数据",appsbl_erase:"准备安装区域",appsbl_write:"写入启动程序",appsbl_readback:"核对启动程序读回",appsbl_verified:"启动程序读回通过",bootconfig1_erase:"准备备用启动记录",bootconfig1_write:"写入备用启动记录",bootconfig0_erase:"准备主启动记录",bootconfig0_write:"写入主启动记录",bootconfig_readback:"核对启动记录读回"};

  class Wizard {
    constructor(request, onChange) {
      this.request = request;
      this.onChange = onChange || function () {};
      this.status = null;
      this.check = null;
      this.connected = false;
      this.uncertain = false;
      this.pending = false;
      this.pollTimer = null;
      this.stopped = false;
      this.statusReadId = 0;
      this.loadId = 0;
      this.sentActions = new Set();
      this.session = null;
    }
    update(patch) {
      if (patch.status) {
        const next = patch.status.session;
        if (typeof next === "string" && next && next !== this.session) {
          this.session = next;
          this.sentActions.clear();
        }
      }
      Object.assign(this, patch); this.onChange(this);
    }
    async get(path) {
      const response = await this.request(path, {method:"GET", cache:"no-store"});
      if (!response || !response.ok) throw new Error("设备读取失败");
      return response.json();
    }
    async readStatus() {
      const id=++this.statusReadId;
      try { return {id, data:await this.get("/api/status"), error:null}; }
      catch (error) { return {id, data:null, error}; }
    }
    async load() {
      const loadId=++this.loadId;
      this.update({connected:false, uncertain:false});
      try {
        const results = await Promise.all([this.get("/api/check"), this.readStatus()]);
        if (loadId!==this.loadId || results[1].id!==this.statusReadId) return false;
        if (results[1].error) throw results[1].error;
        this.update({check:results[0], status:results[1].data, connected:true, uncertain:false});
        this.schedulePoll();
        return true;
      } catch (_) {
        if (loadId!==this.loadId) return false;
        this.update({connected:false, uncertain:!!this.status});
        this.schedulePoll();
        return false;
      }
    }
    schedulePoll() {
      if (this.stopped || this.pollTimer) return;
      this.pollTimer = setTimeout(async () => {
        this.pollTimer = null;
        await this.poll();
      }, ACTIVE_POLL_MS);
    }
    async poll() {
      const result=await this.readStatus();
      if (result.id===this.statusReadId) {
        if (result.error) this.update({connected:false, uncertain:!!this.status || this.pending});
        else this.update({status:result.data, connected:true, uncertain:false});
      }
      if (this.status && (["checking", "installing"].includes(this.status.phase) || ["reboot_scheduled","oem_return_verified"].includes(this.status.stage)) || !this.connected || this.pending) this.schedulePoll();
    }
    stop() { this.stopped = true; if (this.pollTimer) clearTimeout(this.pollTimer); this.pollTimer = null; }
    hasToken() { return !!(this.status && typeof this.status.token === "string" && this.status.token); }
    canInstall() { return !!(this.connected && !this.uncertain && this.status && this.status.phase==="ready" && this.status.install_ready===true && this.status.may_have_modified===false && this.hasToken() && !this.pending && !this.sentActions.size); }
    canReboot() { return !!(this.connected && !this.uncertain && this.status && this.status.phase === "complete" && this.status.can_reboot===true && this.hasToken() && !this.pending && !this.sentActions.has("reboot") && !this.sentActions.has("reboot-oem")); }
    oemVerified() {
      const oem=this.status && this.status.oem;
      return !!(oem && oem.slot_verified===true && oem.boot_records_verified===true);
    }
    maintenanceEntry() {
      const policy=this.status && this.status.boot_policy;
      return policy && policy.verified===true &&
        (policy.maintenance_entry==="uart" ||
          (policy.maintenance_entry==="reset" && policy.reset_verified!==false))
        ? policy.maintenance_entry : null;
    }
    canReturnOEM() {
      const s=this.status;
      const capability=!!(s && s.actions && s.actions.return_oem===true &&
        (s.oem_return ? s.oem_return.available===true : this.oemVerified()));
      return !!(s && this.connected && !this.uncertain && !this.pending && this.hasToken() && !this.sentActions.size &&
        ["ready","blocked","failed"].includes(s.phase) && s.may_have_modified===false && capability);
    }
    canRebootOEM() {
      const s=this.status, policy=s && s.boot_policy;
      return !!(this.canReboot() && this.oemVerified() && s.actions && s.actions.reboot_oem===true && policy &&
        policy.normal_boot==="selected_system" && this.maintenanceEntry() && policy.next_boot==="oem");
    }
    async postAction(action) {
      if (!["install","reboot","return-oem","reboot-oem"].includes(action) || !this.hasToken() || this.pending || this.sentActions.has(action)) return false;
      // Latch before sending: a lost POST followed by a stale ready GET cannot
      // permit a second write. Only a new backend boot/session clears the latch.
      this.sentActions.add(action);
      this.update({pending:true, uncertain:false});
      const body = "token=" + encodeURIComponent(this.status.token) + (action==="return-oem"?"&confirm=return-oem":"");
      try {
        const response = await this.request("/api/" + action, {
          method:"POST",
          headers:{"Content-Type":"application/x-www-form-urlencoded"},
          body
        });
        if (!response || !response.ok) throw new Error("请求未确认");
        // The POST only starts an operation. The status endpoint is authoritative.
        this.update({connected:true, uncertain:false});
        await this.poll();
        return true;
      } catch (_) {
        // Never repeat a potentially accepted POST; recover by reading status only.
        this.update({connected:false, uncertain:true});
        this.schedulePoll();
        return false;
      } finally {
        this.update({pending:false});
        this.schedulePoll();
      }
    }
    async install() { if (!this.canInstall()) return false; return this.postAction("install"); }
    async reboot() { if (!this.canReboot()) return false; return this.postAction("reboot"); }
    async returnOEM() { if (!this.canReturnOEM()) return false; return this.postAction("return-oem"); }
    async rebootOEM() { if (!this.canRebootOEM()) return false; return this.postAction("reboot-oem"); }
  }


  function mount(win) {
    const doc=win.document, $=id=>doc.getElementById(id);
    const setText=(id,value)=>{const el=$(id);if(el)el.textContent=safe(value);};
    const show=(id,visible)=>{const el=$(id);if(el)el.hidden=!visible;};
    const request=(path,options)=>win.fetch(path,options);
    const wizard=new Wizard(request,render);
    let dialogAction=null, dialogBinding=null, previousFocus=null, toastTimer=null;
    const stateTitle={checking:"正在检查设备",ready:"设备检查通过，可以安装",installing:"正在安装，请保持供电",complete:"U-Boot 安装完成",failed:"安装未完成，已停止继续写入",blocked:"当前设备暂不支持安装"};
    function kv(dl,name,value) {
      const row=doc.createElement("div"),dt=doc.createElement("dt"),dd=doc.createElement("dd");
      dt.textContent=name;dd.textContent=safe(value);row.append(dt,dd);dl.append(row);
    }
    function renderChecks(checks) {
      const cards=$("check-list"), rows=$("technical-checks");cards.replaceChildren();rows.replaceChildren();
      latestChecks(checks).forEach(item=>{
        const status=String(item.status||"unknown").toLowerCase(),name=safe(labels[item.name]||item.name);
        const card=doc.createElement("article"),head=doc.createElement("div"),title=doc.createElement("b"),badge=doc.createElement("span"),detail=doc.createElement("p");
        card.className="check-row";head.className="check-head";title.textContent=name;badge.className="check-badge "+(["pass","ok","ready","fail","failed","blocked","unknown","checking","available","optional","skip"].includes(status)?status:"unknown");badge.textContent=checkText[status]||safe(item.status,"信息不足");detail.textContent=checkSummary(item);head.append(title,badge);card.append(head,detail);cards.append(card);
      });
      (Array.isArray(checks)?checks:[]).filter(item=>item && typeof item==="object").forEach(item=>{
        const status=String(item.status||"unknown").toLowerCase(),tr=doc.createElement("tr");
        [safe(item.name),checkText[status]||safe(item.status,"信息不足"),safe(item.detail)].forEach(value=>{const td=doc.createElement("td");td.textContent=value;tr.append(td);});rows.append(tr);
      });
      if(!checks || !checks.length){const p=doc.createElement("p");p.className="empty-checks";p.textContent="等待设备返回检查明细。";cards.append(p);}
    }
    function currentChecks(app) {
      const s=app.status||{},check=app.check||{};
      return Array.isArray(s.checks)&&s.checks.length?s.checks:check.wizard&&check.wizard.checks||check.checks||[];
    }
    function renderTechnical(app) {
      const s=app.status||{},o=app.check&&app.check.observed||{},candidate=s.candidate||{},target=s.target||{};
      const primary=$("target-details"),secondary=$("candidate-details-2");primary.replaceChildren();secondary.replaceChildren();
      const partitions=Array.isArray(o.partitions)?o.partitions:[];
      const positive=v=>Number(v)>0?String(v):"未知";
      const formats=Array.from(new Set(partitions.filter(p=>p.type && /nand/i.test(p.type)).map(p=>`${positive(p.writesize)} / ${positive(p.erasesize)} / ECC${positive(p.ecc_strength)}`)));
      const dispatch=s.dispatch||{},source=s.source||{},nand=s.nand_profile||{};
      [["设备树型号",o.device_tree_model],["安装配置",app.check&&app.check.profile],["匹配 NAND 配置",nand.model||nand.id],["NAND 页 / 擦除 / ECC",formats.join("；")],["分发结果",dispatch.status],["分发代码",dispatch.code],["分发原因",dispatch.reason],["来源产品",source.product],["来源路径",source.route],["来源摘要",source.source_sha256],["内核",o.kernel],["内存",Number(o.ram_total_kib)>0?`${Math.round(o.ram_total_kib/1024)} MiB`:null],["安装目标",target.name],["目标物理偏移",target.offset],["目标大小",target.size?`${target.size} 字节`:null],["当前阶段",s.stage],["设备状态说明",s.message],["受阻原因",s.blocked_reason||"无"],["可能已修改",typeof s.may_have_modified==="boolean"?(s.may_have_modified?"是":"否"):"未知"],["原厂槽与启动记录",app.oemVerified()?"后端已明确核验":"后端尚未提供核验结果"],["返回原厂能力",s.oem_return&&s.oem_return.available===true?"可用":s.oem_return&&s.oem_return.reason||"后端未提供"]].forEach(([k,v])=>kv(primary,k,v));
      [["版本",candidate.version],["SHA-256",candidate.sha256],["大小",candidate.bytes?`${candidate.bytes} 字节`:null],["维护地址",candidate.ip],["原厂返回接口",s.actions&&s.actions.return_oem===true?"后端已声明，仍需安全条件通过":"当前后端未提供"],["重启进入原厂接口",s.actions&&s.actions.reboot_oem===true?"后端已声明，仍需完整安装及启动策略核验":"当前后端未提供"]].forEach(([k,v])=>kv(secondary,k,v));
      const entry=app.maintenanceEntry();
      const verifiedPolicy=app.canRebootOEM() && entry;
      setText("capability-note",verifiedPolicy?("原厂槽、启动记录和 RESET 救援入口均已核验。"):(s.oem_return&&s.oem_return.reason||dispatch.reason||"原厂返回及重启进入原厂，需要后端明确核验原厂槽与启动记录。缺少核验时对应按钮保持禁用。"));
    }
    function render(app) {
      const s=app.status||{},phase=PHASES.has(s.phase)?s.phase:"checking",waiting=app.uncertain||(!app.connected&&!!app.status);
      const installUnavailable=phase==="ready"&&s.install_ready!==true;
      const returning=s.operation==="return_oem" || app.sentActions.has("return-oem");
      const submitted=(app.sentActions.has("install")&&phase==="ready") || (app.sentActions.has("return-oem")&&["ready","blocked","failed"].includes(phase));
      const rebooting=s.stage==="reboot_scheduled"||(returning&&s.stage==="oem_return_verified"),rebootFailed=s.stage==="reboot_failed"||s.stage==="oem_return_reboot_failed";
      $("app").dataset.state=waiting?"offline":rebooting?"rebooting":phase;
      const stage=returning?1:phase==="complete"?4:["installing","failed"].includes(phase)?3:phase==="ready"?2:1;
      doc.querySelectorAll("[data-step]").forEach(node=>{
        const n=Number(node.dataset.step),done=n<stage||(!returning&&phase==="complete");
        node.classList.toggle("bk-active",n===stage);node.classList.toggle("bk-done",done);
        const number=node.querySelector && node.querySelector(".bk-number");if(number)number.textContent=done?"✓":String(n);
        if(node.setAttribute){if(n===stage)node.setAttribute("aria-current","step");else node.removeAttribute("aria-current");}
      });
      setText("stage-kicker",`第 ${stage} 步 · ${["","设备检查","备份与准备","安装 U-Boot","完成与重启"][stage]}`);
      const returnTitles={checking:"正在核验原厂系统",ready:"准备返回原厂系统",blocked:"返回原厂的检查未通过",installing:"正在返回原厂系统",complete:"原厂启动记录已核验",failed:"返回原厂未完成，已停止操作"};
      setText("stage-title",waiting?"连接中断，正在确认操作结果":rebooting?(returning?"正在重启并返回原厂系统":"设备正在重启"):rebootFailed?(returning?"原厂返回已核验，重启未成功":"安装完成，重启未成功"):installUnavailable?"安装资格未通过，可返回原厂":(returning?returnTitles[phase]:stateTitle[phase]));
      setText("phase-badge",waiting?"结果待确认":submitted?"请求已提交":rebooting?"正在重启":installUnavailable?"暂不可安装":returning&&phase==="complete"?"原厂返回已核验":phaseText[phase]);
      const messages={checking:"正在读取设备状态、安装文件和检查结果。",ready:"安装前检查已通过。可以先下载备份，也可以直接安装。",installing:"正在写入并完整读回核验。完成前请保持供电和网线连接。",complete:"安装内容与启动记录的读回核验已完成，尚未重新启动验证。",failed:"核验发现异常。请保持供电，保存诊断信息后按恢复说明处理。",blocked:"设备未通过本候选的安装检查。具体原因见技术明细。"};
      const installUnavailableMessage=s.dispatch&&s.dispatch.reason||s.blocked_reason||s.message||"安装资格未通过；安装已禁用。请查看检查明细，或在原厂返回能力已核验时选择返回原厂。";
      setText("stage-message",waiting?"仅查询设备已有操作状态，请勿重复提交或断电。":submitted?(returning?"返回原厂请求已提交，正在等待设备确认，不会重复发送。":"安装请求已经提交，正在等待设备确认，不会重复发送。"):rebooting?"已收到重启请求，请等待设备重新启动。":rebootFailed?s.message:installUnavailable?installUnavailableMessage:phase==="blocked"?(s.message||s.blocked_reason||s.dispatch&&s.dispatch.reason||messages.blocked):returning?(s.message||"正在核验原厂槽与启动记录；通过后返回原厂系统。"):messages[phase]);
      const o=app.check&&app.check.observed||{};
      show("device",["ready","checking"].includes(phase)&&!!o.device_tree_model&&!waiting);
      setText("device-name",o.device_tree_model);setText("device-sub","设备报告的型号");
      const backupReady=s.backup&&s.backup.available===true&&s.backup.format==="zip"&&s.backup.complete===true&&app.connected&&!waiting;
      show("backup-area",!!backupReady&&["ready","blocked"].includes(phase));
      $("backup-link").href="/backup.zip";$("backup-link").download="bootbridge-recovery-backup.zip";
      renderChecks(currentChecks(app));renderTechnical(app);
      show("progress-area",["installing","failed"].includes(phase)||waiting&&s.may_have_modified===true);
      const bytes=Number(s.progress_bytes),total=Number(s.total_bytes),valid=Number.isFinite(bytes)&&Number.isFinite(total)&&total>0&&bytes>=0;
      const percent=valid?Math.max(0,Math.min(100,bytes/total*100)):0;
      $("progress-fill").style.width=percent+"%";$("progress-track").setAttribute("aria-valuenow",String(Math.round(percent)));
      setText("progress-count",valid?`${Math.round(percent)}%`:"等待设备报告");setText("progress-text",waiting?"等待确认进度":phase==="failed"?"安装已停止":stageText[s.stage]||"等待设备报告进度");
      show("notice",waiting||phase==="failed"||phase==="blocked"||installUnavailable);
      setText("notice-title",waiting?"只确认状态，不重复安装":phase==="failed"?"保持设备供电":installUnavailable?"安装资格未通过":"安装功能已锁定");
      setText("notice-copy",waiting?"恢复连接后继续读取同一操作的状态。":phase==="failed"?(s.may_have_modified===true?"设备报告可能已写入部分内容。已禁止重试、返回原厂和重启，请保存诊断。":"安装事务已停止。只有后端明确核验返回能力后才能返回原厂；请保存诊断。"):(installUnavailable?installUnavailableMessage:(s.blocked_reason||s.message||"设备未提供通过结果，安装保持禁用。")));
      show("success",!returning&&phase==="complete"&&app.canRebootOEM()&&!waiting&&!rebooting);
      const entry=app.maintenanceEntry();
      setText("success-maintenance-title",entry?"按住 RESET 开机，进入救援系统":"救援系统入口尚未核验");
      setText("success-maintenance-copy",entry?"断电后按住 RESET 键再接通电源，红灯闪烁约 3 秒后转为绿灯常亮，此时松手即进入救援系统。用网线连接 LAN 口，在浏览器访问 192.168.10.1。":"后端尚未提供已核验的救援系统入口。");
      show("return-button",["checking","ready","blocked","failed"].includes(phase));
      $("return-button").disabled=!app.canReturnOEM();
      $("return-button").title=app.canReturnOEM()?"后端已核验返回原厂能力":waiting?"连接状态待确认，暂不能提交返回原厂":submitted?"请求已发送，不会重复提交":(s.oem_return&&s.oem_return.reason)||s.dispatch&&s.dispatch.reason||"当前后端未提供已核验的返回原厂能力";
      show("quick-diagnostics-button",["checking","ready","blocked","failed"].includes(phase));
      setText("action-note",phase==="complete"&&s.may_have_modified===true?"已写入新 U-Boot":s.may_have_modified===true?"设备报告可能已修改":s.may_have_modified===false?"设备报告尚未写入":"写入状态待核验");
      const primary=$("install-button");primary.disabled=true;
      let caption="检查通过后才能安装",button="等待检查通过";
      if(waiting){button="重新读取状态";caption="仅读取，不重复提交操作";primary.disabled=app.pending;}
      else if(submitted){button="等待设备确认";caption=returning?"返回原厂请求只发送一次":"安装请求只发送一次";}
      else if(phase==="ready"&&s.install_ready===true){button="安装 U-Boot →";caption=app.canReturnOEM()?"备份可跳过":"备份可跳过；原厂返回尚未核验";primary.disabled=!app.canInstall();}
      else if(phase==="ready"){button="安装资格未通过";caption=app.canReturnOEM()?"安装不可用；可经核验后返回原厂":"安装不可用；查看资格失败原因";primary.disabled=true;}
      else if(phase==="installing"){button=returning?"正在返回原厂…":"正在安装…";caption="请勿断电或重复操作";}
      else if(returning&&phase==="complete"){button=rebooting?"正在返回原厂系统…":rebootFailed?"原厂返回已核验":"原厂返回已核验";caption=rebootFailed?"重启失败，不自动重试；请保存诊断":"后端已核验返回结果；这不表示 U-Boot 安装完成";}
      else if(phase==="complete"){button="重启并启动原厂系统";caption=app.canRebootOEM()?"手动重启，不会自动执行":rebooting?"重启请求已提交":rebootFailed?"重启失败，不自动重试":"后端尚未核验重启进入原厂的能力";primary.disabled=!app.canRebootOEM();}
      else if(phase==="failed"){button="保存诊断信息";caption="已停止写入，请先保存诊断";primary.disabled=false;}
      else if(phase==="blocked"){button="当前不能安装";caption=app.canReturnOEM()?"可经核验后返回原厂":"原厂返回能力尚未核验";}
      if(app.pending){primary.disabled=true;caption="请求正在提交，请勿重复操作";}
      primary.textContent=button;setText("primary-note",caption);
    }
    function actionBinding(){const s=wizard.status||{};return JSON.stringify([s.session,s.phase,s.token,s.candidate&&s.candidate.sha256,s.target&&s.target.offset]);}
    function closeDialog(){show("confirm-overlay",false);$("app").querySelector(".bk-shell").inert=false;dialogAction=null;dialogBinding=null;if(previousFocus&&previousFocus.focus)previousFocus.focus();}
    function openDialog(action){
      dialogAction=action;dialogBinding=actionBinding();previousFocus=doc.activeElement;
      setText("bk-confirm-title",action==="install"?"确认安装 U-Boot？":action==="return"?"返回原厂系统？":"重启并启动原厂系统？");
      const maintenanceEntry=wizard.maintenanceEntry();
      const rebootCopy=maintenanceEntry?"安装、原厂系统和启动策略均已核验通过。确认后将重启进入原厂系统。以后如需进入救援系统，断电后按住 RESET 再通电，红灯闪约 3 秒转绿灯常亮时松手，然后访问 192.168.10.1。":"救援系统入口尚未核验，当前不能重启并启动原厂系统。";
      setText("dialog-copy",action==="install"?"将安装当前设备已核验的内置 U-Boot。安装过程中请保持供电稳定和网线连接。":action==="return"?"原厂槽与启动记录已由后端核验。确认后只提交一次返回原厂请求，并等待设备状态确认；此操作不表示 U-Boot 安装完成。":rebootCopy);
      setText("confirm-button",action==="install"?"确认安装":action==="return"?"确认返回":"确认重启");
      $("app").querySelector(".bk-shell").inert=true;show("confirm-overlay",true);$("cancel-button").focus();
    }
    function toast(message){setText("toast",message);show("toast",true);if(toastTimer)win.clearTimeout(toastTimer);toastTimer=win.setTimeout(()=>show("toast",false),4000);}
    function diagnosticSnapshot(){
      const s=wizard.status||{},o=wizard.check&&wizard.check.observed||{};
      return {schema:"bootkit-bridge-diagnostics-v1",operation:s.operation,phase:s.phase,stage:s.stage,message:s.message,blocked_reason:s.blocked_reason,
        may_have_modified:s.may_have_modified,progress_bytes:s.progress_bytes,total_bytes:s.total_bytes,candidate:s.candidate,target:s.target,
        checks:currentChecks(wizard),observed:{device_tree_model:o.device_tree_model,kernel:o.kernel,ram_total_kib:o.ram_total_kib,partitions:o.partitions},
        actions:s.actions||null,oem:s.oem||null,oem_return:s.oem_return||null,boot_policy:s.boot_policy||null,
        root_binding_contract:s.root_binding_contract||null,
        nand_profile:s.nand_profile||null,dispatch:s.dispatch||null,source:s.source||null,connected:wizard.connected,uncertain:wizard.uncertain};
    }
    function saveDiagnostics(){
      const blob=new win.Blob([JSON.stringify(diagnosticSnapshot(),null,2)+"\n"],{type:"application/json;charset=utf-8"});
      const url=win.URL.createObjectURL(blob),a=doc.createElement("a");a.href=url;a.download="bootkit-bridge-diagnostics.json";a.click();win.setTimeout(()=>win.URL.revokeObjectURL(url),1000);
      $("technical").open=true;toast("诊断已保存到本地，请妥善保管。 ");
    }
    $("install-button").addEventListener("click",()=>{
      if(wizard.uncertain||!wizard.connected){wizard.poll();return;}
      if(wizard.status&&wizard.status.phase==="failed"){saveDiagnostics();return;}
      if(wizard.canInstall())openDialog("install");else if(wizard.canRebootOEM())openDialog("reboot");
    });
    $("return-button").addEventListener("click",()=>{if(wizard.canReturnOEM())openDialog("return");});
    $("cancel-button").addEventListener("click",closeDialog);
    $("confirm-button").addEventListener("click",async()=>{const action=dialogAction,same=dialogBinding===actionBinding();closeDialog();if(!same){toast("设备会话或安装目标已变化，请重新核验后操作。");return;}if(action==="install")await wizard.install();else if(action==="return")await wizard.returnOEM();else if(action==="reboot")await wizard.rebootOEM();});
    $("confirm-overlay").addEventListener("click",event=>{if(event.target===$("confirm-overlay"))closeDialog();});
    $("confirm-overlay").addEventListener("keydown",event=>{
      if(event.key==="Escape")closeDialog();
      if(event.key==="Tab"){const cancel=$("cancel-button"),confirm=$("confirm-button");if(event.shiftKey&&doc.activeElement===cancel){event.preventDefault();confirm.focus();}else if(!event.shiftKey&&doc.activeElement===confirm){event.preventDefault();cancel.focus();}}
    });
    $("diagnostics-button").addEventListener("click",saveDiagnostics);
    $("quick-diagnostics-button").addEventListener("click",saveDiagnostics);
    if(win.addEventListener)win.addEventListener("pagehide",()=>wizard.stop());
    render(wizard);wizard.load();return wizard;
  }
  return {Wizard,mount,latestChecks,checkSummary};
});
