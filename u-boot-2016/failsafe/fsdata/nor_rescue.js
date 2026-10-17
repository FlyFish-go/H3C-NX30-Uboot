(() => {
  const file = document.getElementById('image');
  const validate = document.getElementById('validate');
  const review = document.getElementById('review');
  const candidate = document.getElementById('candidate');
  const write = document.getElementById('write');
  const status = document.getElementById('status');
  let token = null;
  const probe = document.getElementById('nand-probe');
  const retryProbe = document.getElementById('nand-retry');
  const probeStatus = document.getElementById('nand-status');
  const parseJsonResponse = async response => {
    const body = await response.text();
    try {
      return JSON.parse(body);
    } catch (_) {
      const detail = body.trim().slice(0, 120) || '空响应';
      throw new Error(`HTTP ${response.status} 返回非 JSON：${detail}`);
    }
  };
  const setBusy = busy => { validate.disabled = busy; write.disabled = busy; };
  const showProbeResult = result => {
    const statusText = {
      ecc4: '抽样的 UBI 头部支持 ECC4；本次运行读取设置已选为 ECC4。此结论只表示抽样结果，不代表整片 NAND 格式一致。',
      ecc8: '抽样的 UBI 头部支持 ECC8；本次运行读取设置已选为 ECC8。此结论只表示抽样结果，不代表整片 NAND 格式一致。',
      mixed: '不同物理样本分别通过 ECC4 与 ECC8 校验，检测到混合观察；没有自动选择读取模式。',
      unknown: '样本不足以确定读取模式；没有选择 ECC，默认 ECC4 设置已恢复。'
    };
    const offsets = values => values && values.length ? values.join(', ') : '无';
    const lines = [
      statusText[result.status] || statusText.unknown,
      `抽样位置：${result.scan_total_offsets} 个物理偏移，从 ${`0x${Number(result.scan_first_offset).toString(16).padStart(8, '0')}`} 到 ${`0x${Number(result.scan_last_offset).toString(16).padStart(8, '0')}`}，间隔 ${`0x${Number(result.scan_stride).toString(16)}`}。`,
      `完成页数：ECC4 ${result.samples_ecc4}/128，ECC8 ${result.samples_ecc8}/128。有效 UBI 块：ECC4 ${result.valid_ecc4}，ECC8 ${result.valid_ecc8}；同一块双模式有效 ${result.ambiguous_blocks}。`,
      `ECC4 样本偏移：${offsets(result.evidence_ecc4)}`,
      `ECC8 样本偏移：${offsets(result.evidence_ecc8)}`,
      `检测说明：${result.reason || 'unknown'}${result.error ? `（错误 ${result.error}）` : ''}`
    ];
    probeStatus.textContent = lines.join('\n');
    retryProbe.hidden = false;
    retryProbe.disabled = !!result.retry_limit_reached;
    if (result.retry_limit_reached) probeStatus.textContent += '\n已达到本次启动允许的重试次数。';
  };
  const runProbe = async retry => {
    probe.disabled = true; retryProbe.disabled = true;
    probeStatus.textContent = retry ? '正在重新进行 NAND 只读抽样…' : '正在进行 NAND 只读抽样…';
    try {
      const options = {method:'POST'};
      if (retry) options.headers = {'X-NX30-NAND-Probe-Retry':'1'};
      const response = await fetch('/nand/probe', options);
      const result = await parseJsonResponse(response);
      if (!response.ok) throw new Error(result.reason || `HTTP ${response.status}`);
      showProbeResult(result);
    } catch (e) {
      probeStatus.textContent = `检测失败：${e.message}。未进行写入。`;
      retryProbe.hidden = false;
      retryProbe.disabled = false;
    } finally {
      probe.disabled = false;
      if (!retryProbe.disabled) retryProbe.disabled = false;
    }
  };
  probe.addEventListener('click', () => runProbe(false));
  retryProbe.addEventListener('click', () => runProbe(true));
  validate.addEventListener('click', async () => {
    token = null; review.hidden = true;
    if (!file.files.length) { status.textContent = '请先选择 NOR 救援校验包。'; return; }
    setBusy(true); status.textContent = '正在上传并检查救援包和本机 NOR 布局…';
    try {
      const form = new FormData(); form.append('nor_image', file.files[0]);
      const response = await fetch('/nor/validate', {method:'POST', body:form});
      const result = await response.json();
      if (!response.ok || result.status !== 'success') throw new Error(`${result.error || '校验失败'}${result.code === undefined ? '' : `（代码 ${result.code}，阶段 ${result.stage || '校验'}）`}`);
      token = result.token;
      candidate.textContent = '校验通过。确认后将先更新并验证副本，再更新主本；整个过程中请保持供电。';
      review.hidden = false; status.textContent = '救援包校验通过，请确认后开始更新。';
    } catch (e) { status.textContent = `校验失败：${e.message}`; }
    finally { setBusy(false); }
  });
  write.addEventListener('click', async () => {
    if (!token) return;
    const commit = token; token = null; setBusy(true); status.textContent = '正在更新 NOR U-Boot，请保持供电…';
    try {
      const form = new FormData(); form.append('token', commit);
      const response = await fetch('/nor/write', {method:'POST', body:form});
      const result = await response.json();
      if (!response.ok || result.status !== 'success') throw new Error(`${result.error || '更新已停止'}${result.code === undefined ? '' : `（代码 ${result.code}，阶段 ${result.stage || '写入'}）`}`);
      review.hidden = true; status.textContent = '更新完成。请重启设备使新 U-Boot 生效。';
    } catch (e) { review.hidden = true; status.textContent = `更新已停止：${e.message}`; }
    finally { setBusy(false); }
  });
})();
