// Extract the rescue page MIBIB preset generator and write both presets.
const fs = require('fs'), path = require('path'), vm = require('vm'), crypto = require('crypto'), assert = require('assert');
const [mainJs, outDir] = [process.argv[2] || path.join(__dirname, '../failsafe/fsdata/main.js'), process.argv[3]];
const src = fs.readFileSync(mainJs, 'utf8');
const begin = src.indexOf('const RT3_MIBIB_ROWS'), end = src.indexOf('// 文件上传组件类', begin);
assert(begin >= 0 && end > begin);
const ctx = {}; vm.createContext(ctx);
vm.runInContext(src.slice(begin, end) + '\nglobalThis.build = rt3BuildMibib;', ctx);
const dual = Buffer.from(ctx.build(false)), single = Buffer.from(ctx.build(true));
// The OEM MIBIB is identical on all four bench boards.
assert.equal(crypto.createHash('sha256').update(dual).digest('hex'),
             '0b403b917823b51560a52a8b84cc8922e774dbd4530c47989b935dadfbe2e44a');
assert.equal(single.length, 0x80000);
fs.writeFileSync(path.join(outDir, 'dual.bin'), dual);
fs.writeFileSync(path.join(outDir, 'single.bin'), single);
console.log('MIBIB presets: OEM dual table byte-identical to the factory MIBIB');
