"""Execute the real inspector and the unmodified production runtime identity body."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

PROJECT = Path(__file__).resolve().parents[1]


class InspectRuntimeContextTests(unittest.TestCase):
    def test_production_runtime_identity_survives_sanitized_inspector_exec(self):
        install = (PROJECT / "src/bridge_install.c").read_text()
        start = install.index("static int verify_bridge_identity(void){")
        end = install.index("static int verify_qfprom_authenticate", start)
        function = install[start:end]
        self.assertEqual(function.count("#else"), 1)
        # Compile exactly the production branch, with fixture path adapters.
        body = function.split("#else\n", 1)[1].rsplit("#endif", 1)[0]
        self.assertTrue(body.endswith("\n"))
        with tempfile.TemporaryDirectory(prefix="inspect-production-runtime-") as td:
            root = Path(td)
            fixture = root / "fixture"
            files = {
                "etc/bridge/profile": b"H3C-BOOTBRIDGE-RUNTIME-v1\n",
                "etc/bridge/rootfs-id": b"H3C-BOOTBRIDGE-ROOTFS-v1\n",
                "proc/device-tree/compatible": b"qcom,ipq5018-mp03.1-c2\0qcom,ipq5018\0",
                "proc/device-tree/h3c,bridge-profile": b"H3C-BOOTBRIDGE-MULTICONFIG-V1\0",
                "proc/sys/kernel/osrelease": b"4.4.60\n",
            }
            for name, data in files.items():
                p = fixture / name
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_bytes(data)
            dispatch = Path(os.environ.get("BRIDGE_TEST_DISPATCH_SOURCE", str(PROJECT / "src/bridge_dispatch.c")))
            code = r'''
#define BDI_TEST 1
#define BDI_INSPECT_TEST 1
#include DISPATCH_SOURCE
#include <assert.h>
static int path_join(char *out,size_t cap,const char *rel){
    int n=snprintf(out,cap,"%s/%s",FIXTURE_ROOT,rel);return n<0||(size_t)n>=cap?-1:0;
}
static int text_file(const char *rel,char *out,size_t cap){
    char p[PATH_MAX];if(path_join(p,sizeof(p),rel))return -1;
    int fd=open(p,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return -1;
    ssize_t n=read(fd,out,cap-1);close(fd);if(n<=0||(size_t)n>=cap-1)return -1;out[n]=0;return 0;
}
static int return_source_compatible(const uint8_t *p,size_t n){
    static const char expected[]="qcom,ipq5018-mp03.1-c2\0qcom,ipq5018";
    return n==sizeof(expected)&&!memcmp(p,expected,n);
}
static int production_runtime_identity(void){
PRODUCTION_BODY
}
static const bdi_provider_record row={
    .candidate_id="candidate-test",.hardware_class="C",
    .source_sha256="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
    .source_revision_sha256="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
    .product="TEST",.route="route.test",.provider_path="/fixture/runtime"};
int main(int argc,char **argv){
    if(argc==2&&!strcmp(argv[1],"--direct"))return production_runtime_identity()?1:0;
    if(argc==2&&!strcmp(argv[1],"--bridge-dispatch-inspect")){
        int ok=!production_runtime_identity();
        /* Caller-controlled context must never leak through the clean env. */
        if(getenv("BRIDGE_TEST_POISON")||getenv("BRIDGE_SOURCE_SHA256"))ok=0;
        const char *mode=getenv("BRIDGE_RUNTIME_MODE");if(!mode||strcmp(mode,"RUNTIME_GATED"))ok=0;
        printf("{\"install_ready\":%s,\"source_sha256\":\"%s\",\"product\":\"TEST\",\"route\":\"route.test\",\"hardware_class\":\"C\",\"candidate_id\":\"candidate-test\",\"rejected_check\":\"bridge_runtime\",\"reason\":\"runtime identity\"}\n",ok?"true":"false",row.source_sha256);
        return ok?0:1;
    }
    int fd=open("/proc/self/exe",O_RDONLY|O_CLOEXEC);assert(fd>=0);
    int result=inspect_provider(fd,&row);close(fd);printf("inspect_result=%d\n",result);
    return result==1?0:1;
}
'''
            code = code.replace("DISPATCH_SOURCE", json.dumps(str(dispatch)))
            code = code.replace("FIXTURE_ROOT", json.dumps(str(fixture)))
            code = code.replace("PRODUCTION_BODY", body)
            harness = root / "harness.c"
            harness.write_text(code)
            binary = root / "inspect-runtime"
            subprocess.run([os.environ.get("CC", "cc"), "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror",
                "-ffunction-sections", "-fdata-sections", str(harness), "-Wl,--gc-sections", "-o", str(binary)],
                check=True, capture_output=True, text=True, timeout=60)
            clean = dict(os.environ)
            clean.pop("BRIDGE_INIT_VERIFIED", None)
            for marker in (None, "wrong", "H3C-BOOTBRIDGE-RUNTIME-v1"):
                env = dict(clean)
                if marker is not None:
                    env["BRIDGE_INIT_VERIFIED"] = marker
                result = subprocess.run([str(binary), "--direct"], env=env, capture_output=True, text=True, timeout=5)
                self.assertEqual(result.returncode == 0, marker == "H3C-BOOTBRIDGE-RUNTIME-v1", marker)
            poisoned = dict(clean, BRIDGE_TEST_POISON="must-not-leak", BRIDGE_SOURCE_SHA256="spoofed")
            result = subprocess.run([str(binary)], env=poisoned, capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            mutations = [(name, b"wrong\0") for name in files]
            mutations += [("proc/device-tree/compatible", b"qcom,ipq5018"),
                          ("proc/device-tree/h3c,bridge-profile", b"H3C-BOOTBRIDGE-MULTICONFIG-V1")]
            for name, data in mutations:
                with self.subTest(rejected=name, data=data):
                    path = fixture / name
                    path.write_bytes(data)
                    result = subprocess.run([str(binary)], env=poisoned, capture_output=True, text=True, timeout=5)
                    self.assertNotEqual(result.returncode, 0, result.stdout)
                    self.assertIn("inspect_result=0", result.stdout)
                    path.write_bytes(files[name])
            for name in files:
                with self.subTest(missing=name):
                    path = fixture / name
                    path.unlink()
                    result = subprocess.run([str(binary)], env=poisoned, capture_output=True, text=True, timeout=5)
                    self.assertNotEqual(result.returncode, 0, result.stdout)
                    self.assertIn("inspect_result=0", result.stdout)
                    path.write_bytes(files[name])


if __name__ == "__main__":
    unittest.main()
