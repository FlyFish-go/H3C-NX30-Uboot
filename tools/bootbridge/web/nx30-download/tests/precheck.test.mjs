import { normalizeSn, checkSnFormat, groupSn, precheckSn } from '../precheck.mjs';
const sn = normalizeSn(' test 0000 0000 0000 0000 ');
console.log(sn, groupSn(sn), checkSnFormat(sn), checkSnFormat('ABC'), checkSnFormat('TEST-000000000000000'));
console.log('live synthetic:', await precheckSn(sn));
const fake = (resp) => async () => ({ ok: true, json: async () => resp });
console.log('mock ok:', await precheckSn(sn, { fetchImpl: fake({ retCode: 0, gwBestVer: 'NX30V100R012' }) }));
console.log('mock other:', await precheckSn(sn, { fetchImpl: fake({ retCode: 0, gwBestVer: 'NX30V100R013' }) }));
console.log('mock net:', await precheckSn(sn, { fetchImpl: async () => { throw new TypeError('Failed to fetch'); } }));
