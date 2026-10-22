"""Source-bound ENV identity and bootargs policy validator host tests."""
from pathlib import Path
import subprocess
import tempfile
import unittest

PROJECT = Path(__file__).resolve().parents[1]

class InstallIdentityTests(unittest.TestCase):
    def test_source_bound_identity_and_bootargs_rules(self):
        with tempfile.TemporaryDirectory() as d:
            exe=Path(d)/"identity-harness"
            subprocess.run(["cc","-std=c99","-O2","-Wall","-Wextra","-Werror",
                "-I",str(PROJECT/"src"),str(PROJECT/"src/bridge_install_identity.c"),
                str(PROJECT/"tests/identity_harness.c"),"-o",str(exe)],check=True)
            result=subprocess.run([str(exe)],check=True,capture_output=True,text=True)
            self.assertIn("PASS 63 source-bound identity cases",result.stdout)


RC_SOURCE = "69d3e558e770f4dc26f0ff9ddd6a5a10f541b3fdc826aacf3cbeaf1f93469c44"
R4_HW_C_EFBA = "ca630ffee7230c2cd802e783fd4254677a8f066a6978748df94279f70981ba80"
PRE1_HW_C = "57d13d1dad2a7b7854799b7cc69bddec638dd61df70f1b75a4fd94650fc96d8e"

# Drives the runtime admission entry point with a stock RC3000 R013L01 HW-C
# ENV that carries no machid= key, exactly like the reviewed OEM sample.
RUNTIME_MATRIX_C = r'''
#include "bridge_install_identity.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static void crc_env(uint8_t *e){
    uint32_t c=0xffffffffu;
    for(size_t i=4;i<0x40000;i++){c^=e[i];for(unsigned j=0;j<8;j++)c=(c>>1)^(0xedb88320u&-(int32_t)(c&1));}
    c=~c;for(unsigned i=0;i<4;i++)e[i]=(uint8_t)(c>>(8*i));
}
static void build_env(uint8_t *e,const char *extra){
    memset(e,0xff,0x80000);memset(e+4,0,2);
    size_t pos=4;
    static const char *base[]={"productname=RC3000","bootcmd=bootipq",
        "ipaddr=192.168.1.1","netmask=255.255.255.0"};
    for(unsigned i=0;i<4;i++){size_t n=strlen(base[i]);memcpy(e+pos,base[i],n);e[pos+n]=0;pos+=n+1;}
    if(extra){size_t n=strlen(extra);memcpy(e+pos,extra,n);e[pos+n]=0;pos+=n+1;}
    e[pos]=0;crc_env(e);
}

int main(void){
    static uint8_t env[0x80000];
    static const uint8_t route[]="qcom,ipq5018-mp03.1-c2\0qcom,ipq5018";
    static const uint8_t other[]="qcom,ipq5018-mp03.1\0qcom,ipq5018";
    const char *src=SRC_SHA;
    const char *r4=R4_SHA, *p1=PRE1_SHA;
    int fail=0;

    /* Exact C2 route + no machid: both R4 and pre.1 C candidates must admit. */
    build_env(env,NULL);
    if(bii_check_public_candidate_runtime("C","RC3000",src,"08040101",r4,
            env,sizeof env,NULL,route,sizeof route,NULL)!=BII_OK){
        printf("FAIL r4-c exact-route no-machid\n");fail=1;
    }
    if(bii_check_public_candidate_runtime("C","RC3000",src,"08040101",p1,
            env,sizeof env,NULL,route,sizeof route,NULL)!=BII_OK){
        printf("FAIL pre1-c exact-route no-machid\n");fail=1;
    }

    /* A different DT route must not inherit the missing-machid proof. */
    if(bii_check_public_candidate_runtime("C","RC3000",src,"08040101",r4,
            env,sizeof env,NULL,other,sizeof other,NULL)!=BII_ENV_MACHID){
        printf("FAIL r4-c wrong-route admitted\n");fail=1;
    }

    /* A present-but-wrong machid must still be refused, not excused. */
    build_env(env,"machid=08040001");
    if(bii_check_public_candidate_runtime("C","RC3000",src,"08040101",r4,
            env,sizeof env,NULL,route,sizeof route,NULL)!=BII_ENV_MACHID){
        printf("FAIL r4-c wrong-env-machid admitted\n");fail=1;
    }
    build_env(env,"machid=08040101");
    if(bii_check_public_candidate_runtime("C","RC3000",src,"08040101",r4,
            env,sizeof env,NULL,route,sizeof route,NULL)!=BII_OK){
        printf("FAIL r4-c correct-env-machid refused\n");fail=1;
    }

    /* Unknown digests stay BII_POLICY regardless of route or ENV shape. */
    build_env(env,NULL);
    if(bii_check_public_candidate_runtime("C","RC3000",src,"08040101",
            "0000000000000000000000000000000000000000000000000000000000000000",
            env,sizeof env,NULL,route,sizeof route,NULL)!=BII_POLICY){
        printf("FAIL unknown digest admitted\n");fail=1;
    }

    /* The proof is C/RC3000 only: board A must not accept a C candidate. */
    if(bii_check_public_candidate_runtime("A","RC3000",src,"08040001",r4,
            env,sizeof env,NULL,route,sizeof route,NULL)!=BII_POLICY){
        printf("FAIL board-a accepted c candidate\n");fail=1;
    }
    /* Nor may another source product borrow the RC3000 proof. The exact
     * refusal code is an implementation detail (this returns BII_PRODUCT
     * because RT3000's own ENV product name is absent), so assert only that
     * the candidate is not admitted. */
    if(bii_check_public_candidate_runtime("C","RT3000",
            "cb2966e54ae1e143c6c7dd55acdb6ed22bb011a0e421c8860d5724c419d8f28b",
            "08040101",r4,env,sizeof env,NULL,route,sizeof route,NULL)==BII_OK){
        printf("FAIL rt3000 accepted rc3000 candidate\n");fail=1;
    }

    if(!fail)printf("PASS runtime C admission matrix\n");
    return fail;
}
'''


class RuntimeCandidateAdmissionTests(unittest.TestCase):
    """The runtime path must admit exactly the candidates policies[] admits.

    Regression guard for the release that shipped providers whose own payload
    the runtime identity check then refused as BII_ENV_MACHID, because the
    missing-ENV-machid proof named a single hard-coded digest that was not even
    present in policies[].
    """

    def test_runtime_admits_policy_known_c_candidates_on_no_machid_env(self):
        cc = "cc"
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            (root / "bii_candidate_policy.generated.h").write_text(
                "/* synthetic generated row for the runtime admission matrix */\n"
                "#define BII_EXTRA_CANDIDATE_POLICIES \\\n"
                '    {"C", "' + R4_HW_C_EFBA + '", "08040101", NULL, 1u, 0u},\n')
            harness = root / "matrix.c"
            harness.write_text(
                RUNTIME_MATRIX_C
                .replace("SRC_SHA", '"' + RC_SOURCE + '"')
                .replace("R4_SHA", '"' + R4_HW_C_EFBA + '"')
                .replace("PRE1_SHA", '"' + PRE1_HW_C + '"'))
            exe = root / "matrix"
            subprocess.run([cc, "-std=c99", "-O1", "-Wall", "-Wextra", "-Werror",
                "-I", str(root), "-I", str(PROJECT / "src"),
                '-DBII_CANDIDATE_POLICY_HEADER="bii_candidate_policy.generated.h"',
                str(harness), str(PROJECT / "src/bridge_install_identity.c"),
                "-o", str(exe)], check=True)
            result = subprocess.run([str(exe)], check=True, capture_output=True, text=True)
            self.assertIn("PASS runtime C admission matrix", result.stdout)


if __name__ == "__main__":
    unittest.main()
