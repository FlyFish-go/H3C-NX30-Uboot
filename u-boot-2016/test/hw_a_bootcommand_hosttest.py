from pathlib import Path
import os,re,subprocess,sys
if len(sys.argv)!=3: raise SystemExit("usage: hw_a_bootcommand_hosttest.py READONLY_ELF PRODUCTION_ELF")
pairs=[(Path(sys.argv[1]),"CONFIG_H3C_HW_A_RAM_READONLY=y","httpd","readonly"),(Path(sys.argv[2]),"CONFIG_H3C_HW_A_PRODUCTION_PORT=y","httpd","production")]
base=Path(os.environ.get("HWA_BUILD_EVIDENCE_ROOT", "."))
dirs=["build-hwa-a-led-finalboot-readonly","build-hwa-a-led-finalboot-production"]
for (elf,flag,expected,name),dirname in zip(pairs,dirs):
 cfg=(base/dirname/"build.config").read_text()
 if flag not in cfg or "CONFIG_TARGET_IPQ5018_H3C_HW_A=y" not in cfg: raise SystemExit(name+" profile config mismatch")
 strings=subprocess.check_output(["strings",str(elf)],text=True,errors="replace")
 cmds=re.findall(r"(?m)^bootcmd=([^\r\n]+)",strings)
 if expected not in cmds: raise SystemExit(name+" compiled default bootcmd missing: "+repr(cmds))
 opposite="bootipq" if expected=="httpd" else "httpd"
 if opposite in cmds: raise SystemExit(name+" contains opposite compiled bootcmd: "+repr(cmds))
 print(name+" compiled HW-A bootcmd="+expected+": PASS")
