"""Fresh candidate policy is compile-pinned, while ENV/source gates persist."""
from pathlib import Path
import subprocess,tempfile,unittest
P=Path(__file__).resolve().parents[1]
class CandidateIdentityRegistry(unittest.TestCase):
 def test_compile_pinned_candidate_and_source_identity(self):
  with tempfile.TemporaryDirectory() as td:
   r=Path(td);h='a'*64
   (r/'policy.h').write_text('#define BII_EXTRA_CANDIDATE_POLICIES {"C", "'+h+'", "08040101", NULL, 1, 0},\n')
   (r/'probe.c').write_text('#define main legacy_identity_main\n#include "identity_harness.c"\n#undef main\n'+r'''
int main(void){
 const char *fresh="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
 uint8_t *e=ENV("productname=RC3000\0machid=8040101\0bootargs=console=ttyMSM0\0");
 assert(check("C","RC3000","08040101",fresh,e,NULL,NULL)==BII_OK);
 assert(check("B","RC3000","08040000",fresh,e,NULL,NULL)==BII_POLICY);
 assert(check_source("C","RC3000",source_sha("RT3000"),"08040101",fresh,e,NULL,NULL)==BII_POLICY);
 assert(check("C","RC3000","08040101","bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",e,NULL,NULL)==BII_POLICY);
 assert(check("C","RC3000","08040101",fresh,e,"machid=8040000",NULL)==BII_CMDLINE_MACHID);
 e[1]^=1;assert(check("C","RC3000","08040101",fresh,e,NULL,NULL)==BII_CRC);free(e);
 return legacy_identity_main();
}
''')
   cmd=['cc','-std=c99','-O2','-Wall','-Wextra','-Werror','-DBII_CANDIDATE_POLICY_HEADER="policy.h"','-I'+str(r),'-I'+str(P/'src'),'-I'+str(P/'tests'),str(P/'src/bridge_install_identity.c'),str(r/'probe.c'),'-o',str(r/'probe')]
   subprocess.run(cmd,check=True);subprocess.run([str(r/'probe')],check=True,capture_output=True)
