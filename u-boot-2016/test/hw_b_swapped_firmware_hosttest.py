#!/usr/bin/env python3
"""Production firmware callbacks preserve exchanged logical/physical mappings."""
import ctypes,importlib.util,json,tempfile
from pathlib import Path
here=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('fw',here/'firmware_rt3000_b_handler_hosttest.py')
f=importlib.util.module_from_spec(spec);spec.loader.exec_module(f)
f.WRAPPERS+=r"void host_swap(void){u64 o=host_layout.parts[0].offset;host_layout.parts[0].offset=host_layout.parts[1].offset;host_layout.parts[1].offset=o;host_layout.fingerprint^=0x10203040;}"
spec=importlib.util.spec_from_file_location('ubi',f.UBI_TEST)
u=importlib.util.module_from_spec(spec);spec.loader.exec_module(u)
e=next(e for e in json.loads(f.CATALOG.read_text())['entries'] if e['model']=='RT3000' and e['release']=='R012')
ubi=bytes(u.extract_ubi(e));results=[];f.map_upload_window()
with tempfile.TemporaryDirectory() as td:
 l=f.build_test_lib(Path(td));l.host_clear_target.argtypes=[ctypes.c_char_p]
 l.host_touched_min_get.restype=l.host_touched_max_get.restype=ctypes.c_uint64
 for swap in (False,True):
  expected=[('rootfs',0x3100000 if swap else 0x900000),('rootfs_1',0x900000 if swap else 0x3100000)]
  l.host_reset(1)
  if swap:l.host_swap()
  code,body,_=f.dispatch(l,'/firmware/layout',1);obj=json.loads(body)
  assert code==200 and [(p['name'],p['offset']) for p in obj['partitions']]==expected
  assert l.host_erase_count_get()==0
  for name,off in expected:
   l.host_reset(1)
   if swap:l.host_swap()
   l.host_clear_target(name.encode())
   code,body,keep=f.dispatch(l,'/firmware/validate',2,[('firmware',ubi),('target',name),('mode','dual')]);assert code==200
   token=f.token_from(body)
   code,body,_=f.dispatch(l,'/firmware/write',2,[('token',token),('target',name),('mode','dual')]);obj=json.loads(body)
   assert code==200 and obj['verified'] and not obj['boot_select_changed'] and not obj['env_changed']
   assert l.host_touched_min_get()==off and l.host_touched_max_get()==off+0x2800000
   assert l.host_erase_count_get()==320 and l.host_mtd_flags_get()==0x80 and not l.host_window_is_open()
   results.append({'exchanged':swap,'target':name,'offset':off,'verified':True})
 l.host_reset(1)
 code,body,keep=f.dispatch(l,'/firmware/validate',2,[('firmware',ubi),('target','rootfs'),('mode','dual')]);assert code==200
 token=f.token_from(body);l.host_swap()
 assert f.dispatch(l,'/firmware/write',2,[('token',token),('target','rootfs'),('mode','dual')])[0]==409
 assert l.host_erase_count_get()==0 and l.host_write_count_get()==0
print(json.dumps({'result':'PASS','device_io':False,'layout_http_status':200,'mapping_changed_rejected_before_io':True,'mock_writes':results},indent=2))
