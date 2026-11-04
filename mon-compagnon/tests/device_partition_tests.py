import importlib.util,struct,hashlib
from pathlib import Path
root=Path(__file__).resolve().parents[1];spec=importlib.util.spec_from_file_location('gate',root/'scripts/check-device-partitions.py');gate=importlib.util.module_from_spec(spec);spec.loader.exec_module(gate)
data=b''.join(struct.pack('<HBBII16sI',0x50aa,*e) for e in gate.EXPECTED);data+=b'\xeb\xeb'+b'\xff'*14+hashlib.md5(data).digest();data+=b'\xff'*(3072-len(data));gate.check(data,1422608)
for size in (0,0x1e0001):
 try:gate.check(data,size);raise AssertionError('Accepted invalid size')
 except ValueError:pass
for offset in (0,8,32,64,80,192,210,3060):
 corrupt=bytearray(data);corrupt[offset]^=1
 try:gate.check(corrupt,1422608);raise AssertionError('Accepted tampered table')
 except ValueError:pass
print('Device partition safety gate tests passed.')
