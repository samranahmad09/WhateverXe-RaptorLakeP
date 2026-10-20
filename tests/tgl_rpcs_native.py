"""Replay the exact constructor's one RPCS store. No GPU or whole constructor."""
import ctypes as C
import hashlib
import json
import mmap
import platform
import re
import struct
import sys
from pathlib import Path

if len(sys.argv) != 2 or platform.machine().lower() not in ('x86_64', 'amd64'):
    raise SystemExit('usage: x86_64 python3 tgl_rpcs_native.py EXACT_ACCELERATOR')
data = Path(sys.argv[1]).read_bytes()
assert hashlib.sha256(data).hexdigest() == 'bd1a21f5e622aadabdab6441f6b5c38ed73fa3053c783fdf0fb7e4b309f273d0'
header = (Path(__file__).resolve().parents[1]/'WhateverXe/kern_tgl_rpcs.hpp').read_text()
def array(name):
    body = re.search(r'constexpr uint8_t '+name+r'\[\]\s*\{([^}]+)\}', header)[1]
    return bytes(int(x.strip(), 0) for x in body.split(',') if x.strip())
old, new = array('Find'), array('Replace')
assert data[0x7c313:0x7c31e] == old
assert old[:7] == new[:7] == bytes.fromhex('41c7870c110000')
assert struct.unpack_from('<I', old, 7)[0] == 0x80000000
expected = (1 << 31) | (1 << 18) | (1 << 12)
assert struct.unpack_from('<I', new, 7)[0] == expected
results = []
for name, instruction, value in [('baseline', old, 0x80000000), ('full_slice', new, expected)]:
    # Preserve callee-saved R15 and bind the one store's destination to our buffer.
    program = bytes.fromhex('41574989ff') + instruction + bytes.fromhex('415fc3')
    mem = mmap.mmap(-1, mmap.PAGESIZE, prot=mmap.PROT_READ|mmap.PROT_WRITE|mmap.PROT_EXEC)
    mem[:len(program)] = program
    image = C.create_string_buffer(b'\xa5'*81920)
    address = C.addressof(C.c_char.from_buffer(mem))
    C.CFUNCTYPE(None, C.c_void_p)(address)(C.addressof(image))
    actual = image.raw[:81920]
    assert struct.unpack_from('<I', actual, 0x110c)[0] == value
    changed = [i for i, v in enumerate(actual) if v != 0xa5]
    assert changed == list(range(0x110c, 0x1110)), changed
    results.append(dict(case=name, value=hex(value), changed_offsets=[hex(i) for i in changed]))
    mem.close()
print(json.dumps(dict(result='PASS', scope='exact native store only; no GPU execution or power-state validation', cases=results)))
