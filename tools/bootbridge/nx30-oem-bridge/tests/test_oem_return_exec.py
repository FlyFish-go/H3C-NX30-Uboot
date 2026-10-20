#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the production verified return-helper fexecve boundary on host."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

PROJECT = Path(__file__).resolve().parents[1]
RETURN_EXEC = PROJECT / "src/bridge_oem_return_exec.c"

STUB = r'''#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern char **environ;
static const char *expected[] = {
  "/usr/sbin/bridge-oem-return", "--dispatch-reason", "candidate_ambiguous",
  "two contracts passed", "--dispatch-candidate-source-revision",
  "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
  "-b", "192.168.10.1", "-p", "80", "-w", "www"
};
static const char *detail = "tool_revision=R6-preflight-v1;outcome=timeout;last_stage=nand";
int main(int argc, char **argv) {
  unsigned envc=0;
  for(char **p=environ; *p; ++p) ++envc;
  if((argc!=12 && argc!=14) || envc!=3) return 41;
  for(int i=0;i<6;i++) if(strcmp(argv[i],expected[i])) return 42+i;
  if(argc==14 && (strcmp(argv[6],"--dispatch-execution-detail") || strcmp(argv[7],detail))) return 48;
  for(int i=6;i<12;i++) if(strcmp(argv[i+(argc==14?2:0)],expected[i])) return 50+i;
  if(strcmp(environ[0],"PATH=/bin:/sbin:/usr/bin:/usr/sbin") ||
     strcmp(environ[1],"BRIDGE_RETURN_FALLBACK=1") ||
     strcmp(environ[2],"BRIDGE_INIT_VERIFIED=H3C-BOOTBRIDGE-RUNTIME-v1")) return 60;
  puts("RETURN_EXEC_MARKER");
  printf("argv_count=%d\n",argc);
  puts("environment_count=3");
  for(int i=0;i<argc;i++) printf("argv[%d]=%s\n",i,argv[i]);
  for(unsigned i=0;i<envc;i++) printf("env[%u]=%s\n",i,environ[i]);
  return 0;
}'''

HARNESS = r'''#define _GNU_SOURCE
#include "bridge_oem_return_exec.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
int main(int argc,char **argv) {
  if(argc!=3) return 2;
  int root=open(argv[1],O_RDONLY|O_DIRECTORY|O_CLOEXEC);
  if(root<0) return 3;
  const char *code=!strcmp(argv[2],"invalid-reason") ? "invalid code!" : "candidate_ambiguous";
  const char *reason="two contracts passed";
  errno=0;
  const char *revision=!strcmp(argv[2],"invalid-revision") ? "not-a-digest" :
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
  const char *detail=!strcmp(argv[2],"invalid-detail") ? "bad\nline" :
    "tool_revision=R6-preflight-v1;outcome=timeout;last_stage=nand";
  int rc=!strcmp(argv[2],"detail") || !strcmp(argv[2],"invalid-detail") ?
    bi_oem_return_exec_verified_detail(root,code,reason,revision,detail) :
    bi_oem_return_exec_verified(root,code,reason,revision);
  int e=errno;
  close(root);
  if(rc<0) { printf("BLOCKED errno=%d\n",e); return 0; }
  return 4;
}'''


def compile_static(cc: str, output: Path, stub_path: Path, digest: str, size: int) -> None:
    subprocess.run([cc, "-std=c99", "-Wall", "-Wextra", "-Werror", "-static",
        f'-DBR_RETURN_EXEC_SHA256="{digest}"', f"-DBR_RETURN_EXEC_BYTES={size}",
        "-I", str(PROJECT / "src"), str(RETURN_EXEC), str(stub_path),
        "-o", str(output)], check=True, capture_output=True, timeout=45)


class ReturnExecTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cc = shutil.which(os.environ.get("HOSTCC", "cc"))
        namespace = shutil.which("unshare")
        if not cc or not namespace:
            raise unittest.SkipTest("host compiler and unshare user namespace are required")
        cls.cc, cls.namespace = cc, namespace
        cls.tmp = tempfile.TemporaryDirectory(prefix="bridge-return-exec-test-")
        cls.work = Path(cls.tmp.name)
        cls.stub_source = cls.work / "return_stub.c"
        cls.stub_source.write_text(STUB, encoding="ascii")
        cls.stub = cls.work / "return_stub"
        subprocess.run([cc, "-std=c99", "-Wall", "-Wextra", "-Werror", "-static",
            str(cls.stub_source), "-o", str(cls.stub)], check=True,
            capture_output=True, timeout=45)
        cls.stub_bytes = cls.stub.read_bytes()
        cls.stub_sha = hashlib.sha256(cls.stub_bytes).hexdigest()
        cls.stub_size = len(cls.stub_bytes)
        cls.harness_src = cls.work / "harness.c"
        cls.harness_src.write_text(HARNESS, encoding="ascii")
        cls.valid_harness = cls.work / "harness-valid"
        compile_static(cc, cls.valid_harness, cls.harness_src, cls.stub_sha, cls.stub_size)
        cls.bad_hash_harness = cls.work / "harness-bad-hash"
        compile_static(cc, cls.bad_hash_harness, cls.harness_src, "0" * 64, cls.stub_size)
        cls.bad_size_harness = cls.work / "harness-bad-size"
        compile_static(cc, cls.bad_size_harness, cls.harness_src, cls.stub_sha, cls.stub_size + 1)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def root_with_helper(self, mode=0o555, symlink=False):
        root = Path(tempfile.mkdtemp(prefix="root-", dir=self.work))
        dest = root / "usr/sbin/bridge-oem-return"
        dest.parent.mkdir(parents=True)
        if symlink:
            dest.symlink_to(self.stub)
        else:
            dest.write_bytes(self.stub_bytes)
            dest.chmod(mode)
        return root

    def invoke(self, harness, root, arg="valid"):
        return subprocess.run([self.namespace, "-Ur", str(harness), str(root), arg],
            capture_output=True, text=True, timeout=20)

    def test_exact_pinned_helper_is_really_fexecve_executed_with_fixed_argv_env(self):
        root = self.root_with_helper()
        try:
            st = (root / "usr/sbin/bridge-oem-return").stat()
            self.assertEqual(st.st_mode & 0o777, 0o555)
            self.assertEqual(st.st_size, self.stub_size)
            self.assertEqual(hashlib.sha256((root / "usr/sbin/bridge-oem-return").read_bytes()).hexdigest(),
                             self.stub_sha)
            result = self.invoke(self.valid_harness, root)
            self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
            self.assertEqual(result.stdout, """RETURN_EXEC_MARKER
argv_count=12
environment_count=3
argv[0]=/usr/sbin/bridge-oem-return
argv[1]=--dispatch-reason
argv[2]=candidate_ambiguous
argv[3]=two contracts passed
argv[4]=--dispatch-candidate-source-revision
argv[5]=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef
argv[6]=-b
argv[7]=192.168.10.1
argv[8]=-p
argv[9]=80
argv[10]=-w
argv[11]=www
env[0]=PATH=/bin:/sbin:/usr/bin:/usr/sbin
env[1]=BRIDGE_RETURN_FALLBACK=1
env[2]=BRIDGE_INIT_VERIFIED=H3C-BOOTBRIDGE-RUNTIME-v1
""")
        finally:
            shutil.rmtree(root)

    def assert_blocked(self, harness, root, arg="valid"):
        result = self.invoke(harness, root, arg)
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        self.assertTrue(result.stdout.startswith("BLOCKED errno="), result.stdout)
        self.assertNotIn("RETURN_EXEC_MARKER", result.stdout)

    def test_detail_is_passed_only_through_fixed_extra_arguments(self):
        root = self.root_with_helper()
        try:
            result = self.invoke(self.valid_harness, root, "detail")
            self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
            self.assertIn("RETURN_EXEC_MARKER\nargv_count=14\nenvironment_count=3\n", result.stdout)
            self.assertIn("argv[6]=--dispatch-execution-detail\n", result.stdout)
            self.assertIn("argv[7]=tool_revision=R6-preflight-v1;outcome=timeout;last_stage=nand\n", result.stdout)
            self.assertIn("argv[8]=-b\n", result.stdout)
            self.assertNotIn("bad\\nline", result.stdout)
        finally:
            shutil.rmtree(root)

    def test_control_character_in_detail_is_rejected_before_exec(self):
        root = self.root_with_helper()
        try:
            self.assert_blocked(self.valid_harness, root, "invalid-detail")
        finally:
            shutil.rmtree(root)

    def test_bad_hash_compile_pin_does_not_execute_helper(self):
        root = self.root_with_helper()
        try:
            self.assert_blocked(self.bad_hash_harness, root)
        finally:
            shutil.rmtree(root)

    def test_bad_size_compile_pin_does_not_execute_helper(self):
        root = self.root_with_helper()
        try:
            self.assert_blocked(self.bad_size_harness, root)
        finally:
            shutil.rmtree(root)

    def test_invalid_source_revision_binding_never_executes_helper(self):
        root = self.root_with_helper()
        try:
            self.assert_blocked(self.valid_harness, root, "invalid-revision")
        finally:
            shutil.rmtree(root)

    def test_group_world_writable_helper_is_rejected(self):
        root = self.root_with_helper(0o775)
        try:
            self.assert_blocked(self.valid_harness, root)
        finally:
            shutil.rmtree(root)

    def test_symlink_helper_is_rejected(self):
        root = self.root_with_helper(symlink=True)
        try:
            self.assert_blocked(self.valid_harness, root)
        finally:
            shutil.rmtree(root)

    def test_invalid_reason_is_rejected_before_exec(self):
        root = self.root_with_helper()
        try:
            self.assert_blocked(self.valid_harness, root, "invalid-reason")
        finally:
            shutil.rmtree(root)


if __name__ == "__main__":
    unittest.main()
