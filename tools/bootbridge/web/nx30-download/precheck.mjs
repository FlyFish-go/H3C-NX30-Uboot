export const BEST_API = 'https://magic.h3c.com/smarthomeback/rest/ihomers/getGwBestVerNew';
export const TARGET = 'NX30V100R012';
export function normalizeSn(raw) { return String(raw ?? '').replace(/\s+/g, '').toUpperCase(); }
export function checkSnFormat(sn) {
  if (!/^[0-9A-Z]*$/.test(sn)) return { ok: false, reason: 'chars' };
  if (sn.length !== 20) return { ok: false, reason: 'length', length: sn.length };
  return { ok: true };
}
export function groupSn(sn) { return sn.replace(/(.{4})(?=.)/g, '$1 '); }
export async function precheckSn(sn, { fetchImpl = fetch, timeoutMs = 15000 } = {}) {
  if (!checkSnFormat(sn).ok) return { state: 'bad-format' };
  let last = { state: 'rejected', code: null };
  for (const gwVer of ['NX30V100R011', TARGET]) {
    let res, j;
    try {
      res = await fetchImpl(BEST_API, { method: 'POST', headers: { 'Content-Type': 'application/json', 'Accept': 'application/json' },
        body: JSON.stringify({ gwVer, gwSn: sn }), credentials: 'omit', referrerPolicy: 'no-referrer', cache: 'no-store',
        signal: AbortSignal.timeout(timeoutMs) });
    } catch { return { state: 'network' }; }
    if (!res.ok) return { state: 'http', status: res.status };
    try { j = await res.json(); } catch { return { state: 'bad-response' }; }
    const code = Number(j?.retCode);
    if (code === 0 && j.gwBestVer === TARGET) return { state: 'ok' };
    if (code === 24) return { state: 'unknown-sn' };
    if (code === 20) return { state: 'bad-format' };
    last = code === 0 ? { state: 'other-version', version: String(j.gwBestVer ?? '') } : { state: 'rejected', code };
  }
  return last;
}
