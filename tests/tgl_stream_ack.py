#!/usr/bin/env python3
"""Exercise the actual interrupt observer with inaccessible register memory."""
from pathlib import Path
import argparse
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', type=Path, default=root / 'WhateverXe/kern_xe.cpp')
args = parser.parse_args()
source = args.source.read_text()
body = re.search(r'void tglStreamAck\(.*?\n\}', source, re.S).group()
fixture = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sys/mman.h>
struct IOSimpleLock {};
IOSimpleLock lock, *tglStreamLock = &lock;
uint64_t tglStreamErrors[64][16] {};
uint32_t tglStreamErrorCount {}, tglStreamAckCounts[8] {};
bool held = false, irqEnabled = true;
unsigned IOSimpleLockLockDisableInterrupt(IOSimpleLock *) {
 assert(!held); held = true; bool prior = irqEnabled; irqEnabled = false; return prior;
}
void IOSimpleLockUnlockEnableInterrupt(IOSimpleLock *, unsigned prior) {
 assert(held && !irqEnabled); held = false; irqEnabled = prior;
}
void clock_get_uptime(uint64_t *t) { static uint64_t now; *t = ++now; }
void absolutetime_to_nanoseconds(uint64_t t, uint64_t *n) { *n = t; }
void *current_thread() { return reinterpret_cast<void *>(1); }
// BODY
int main() {
 auto mmio = mmap(nullptr, 0x100000, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
 assert(mmio != MAP_FAILED);
 alignas(8) uint8_t access[0x80] {};
 *reinterpret_cast<void **>(access + 0x50) = mmio;
 *reinterpret_cast<uint32_t *>(access + 0x60) = 0x100000;
 auto caller = reinterpret_cast<void *>(0x1234);
 // A native clear-all ACK also contains bit31; it is not proof of an error
 // or of a powered pipe. The old observer faults on its first MMIO read.
 tglStreamAck(access, 0x44418, 0xffffffff, 0, caller);
 assert(tglStreamErrorCount == 1 && tglStreamErrors[0][15] == 0x4D454D31);
 // Even the manager is opaque: capture only arguments on an IRQ boundary.
 for (unsigned pipe = 0; pipe < 4; ++pipe) for (unsigned fast = 0; fast < 2; ++fast)
  for (unsigned i = 0; i < 16; ++i) {
   irqEnabled = i & 1;
   tglStreamAck(mmio, 0x44408 + pipe * 0x10, 0x80000000, fast, caller);
   assert(irqEnabled == bool(i & 1) && !held);
   auto q = tglStreamErrors[(tglStreamErrorCount - 1) % 64];
   assert(q[3] == 0x44408 + pipe * 0x10 && q[4] == 0x80000000);
   assert(q[12] == 0x1234 && q[13] == fast && q[15] == 0x4D454D31);
   for (unsigned j = 5; j < 12; ++j) assert(q[j] == 0);
   assert(q[14] == 0);
  }
 const auto count = tglStreamErrorCount;
 tglStreamAck(mmio, 0x44408, 1, 0, caller); // no bit31: count ACK only
 assert(tglStreamErrorCount == count && tglStreamAckCounts[0] == 17);
 for (auto reg : {0x44404ul, 0x44409ul, 0x44448ul})
  tglStreamAck(mmio, reg, 0xffffffff, 0, caller);
 tglStreamAck(mmio, 0x44408, 0xffffffff, 2, caller);
 tglStreamLock = nullptr;
 tglStreamAck(mmio, 0x44408, 0xffffffff, 0, caller);
 assert(tglStreamErrorCount == count);
 munmap(mmio, 0x100000);
 puts("PASS: 129 ACK records, clear-all/4 pipes/normal+fast writes, ring wrap, IRQ restoration; inaccessible MMIO and manager untouched");
}
'''.replace('// BODY', body).replace('#include <cstdio>', '#include <cstdio>\n#include <initializer_list>')
with tempfile.TemporaryDirectory(prefix='tgl-stream-ack-') as tmp:
    tmp = Path(tmp)
    cpp, binary = tmp / 'check.cpp', tmp / 'check'
    cpp.write_text(fixture)
    subprocess.run(['clang++', '-std=c++14', '-O1', '-g', '-fsanitize=address,undefined',
                    str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
