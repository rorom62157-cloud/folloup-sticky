#!/usr/bin/env python3
"""Exact check of v0.3 min_spiffs table and application bound; no device I/O."""
import hashlib,struct,sys
from pathlib import Path
EXPECTED=[(1,2,0x9000,0x5000,b'nvs',0),(1,0,0xe000,0x2000,b'otadata',0),(0,16,0x10000,0x1e0000,b'app0',0),(0,17,0x1f0000,0x1e0000,b'app1',0),(1,130,0x3d0000,0x20000,b'spiffs',0),(1,3,0x3f0000,0x10000,b'coredump',0)]
def check(data,application_size):
 if len(data)!=3072:raise ValueError('Unexpected partition table size')
 if not 0<application_size<=0x1e0000:raise ValueError('Application exceeds app0')
 for i,wanted in enumerate(EXPECTED):
  magic,kind,sub,offset,size,name,flags=struct.unpack_from('<HBBII16sI',data,i*32)
  if magic!=0x50aa or (kind,sub,offset,size,name.rstrip(b'\0'),flags)!=wanted:raise ValueError('Unexpected partition layout')
 end=32*len(EXPECTED)
 if data[end:end+16]!=b'\xeb\xeb'+b'\xff'*14 or data[end+16:end+32]!=hashlib.md5(data[:end]).digest():raise ValueError('Partition checksum invalid')
 if data[end+32:]!=b'\xff'*(len(data)-end-32):raise ValueError('Unexpected trailing partition entries')
if __name__=='__main__':
 try:
  if len(sys.argv)!=3:raise ValueError('Usage: check-device-partitions.py table.bin app.bin')
  check(Path(sys.argv[1]).read_bytes(),Path(sys.argv[2]).stat().st_size)
 except (ValueError,OSError) as e:print('STOP:',e,file=sys.stderr);raise SystemExit(1)
 print('Exact min_spiffs layout and application size verified.')
