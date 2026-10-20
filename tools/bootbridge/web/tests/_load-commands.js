// Loads the NX30 command templates the same way build_site.py does.
const fs = require('fs'), path = require('path');
const dir = process.env.BB_COMMANDS_DIR || path.join(__dirname, '..', 'nx30-download');
module.exports = { ps51: fs.readFileSync(path.join(dir, 'ps51.tpl'), 'utf8').replace(/\n+$/, ''), sh: fs.readFileSync(path.join(dir, 'sh.tpl'), 'utf8').replace(/\n+$/, '') };
