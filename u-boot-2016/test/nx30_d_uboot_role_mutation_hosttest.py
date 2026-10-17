#!/usr/bin/env python3
"""Verify the real APPSBL role negative detects removal of the production gate.
Uses only generated files and native host binaries; no hardware access.
"""
import argparse
import json
import resource
import subprocess
import tempfile
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path)
a = parser.parse_args()
u = Path(__file__).resolve().parents[1]
out = a.output.resolve() if a.output else Path(tempfile.mkdtemp(prefix='h3c-role-mutation-'))
out.mkdir(parents=True, exist_ok=True)
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
core = u / 'lib/nx30_d_uboot.c'
text = core.read_text()
anchor = '\t\t\t    get_le32(candidate + 104) !=\n\t\t\t\tNX30_D_UBOOT_REQUIRED_SERVICE_RAM ||\n'
assert text.count(anchor) == 1, 'Review production role predicate before changing this test'
mutant = out / 'role-predicate-removed.c'
mutant.write_text(text.replace(anchor, ''))
compat = out / 'compat/openssl'
compat.mkdir(parents=True, exist_ok=True)
(compat / 'sha.h').write_text('#include <stddef.h>\nunsigned char *SHA256(const unsigned char *, size_t, unsigned char *);\n')
crypto = Path('/usr/lib/x86_64-linux-gnu/libcrypto.so.3')
assert crypto.is_file(), 'Existing system libcrypto required'
results = []
for name, implementation in [('production', core), ('role-mutant', mutant)]:
    executable = out / name
    command = ['cc', '-std=c99', '-Wall', '-Wextra', '-Werror',
        '-DNX30_D_UBOOT_HOST_TEST', '-DCONFIG_H3C_HW_D_STORAGE_PORT',
        '-I' + str(out / 'compat'), '-idirafter', str(u / 'include'),
        str(implementation), str(u / 'test/nx30_d_uboot_core_test.c'), str(crypto), '-o', str(executable)]
    build = subprocess.run(command, capture_output=True, text=True)
    (out / (name + '-compile.log')).write_text(json.dumps(command) + '\n' + build.stdout + build.stderr)
    assert build.returncode == 0, name + ' compile failure'
    run = subprocess.run([str(executable)], capture_output=True, text=True)
    (out / (name + '.log')).write_text(run.stdout + run.stderr)
    ok = run.returncode == 0 if name == 'production' else (run.returncode == -6 and 'nx30_d_uboot_parse_mbn' in run.stderr and 'Assertion' in run.stderr)
    results.append({'case': name, 'returncode': run.returncode, 'expected_outcome_observed': ok})
    assert ok, name + ' unexpected result'
(out / 'RESULT.json').write_text(json.dumps({'status': 'PASS', 'hardware': 'NOT_EXECUTED', 'cases': results}, indent=2) + '\n')
print('APPSBL role mutation: production passes; removed production role gate rejected by regression')
