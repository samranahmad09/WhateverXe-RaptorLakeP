"""Run the actual BAR admission helper with distinct DMA and physical addresses."""
from pathlib import Path
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[1]
source = (repo / 'WhateverXe/kern_xe.cpp').read_text()
start = source.index('static bool tglPrepareMMIOBAR(')
body = source[start:source.index('\n}', start) + 2]
fixture = r'''
#include <stdint.h>
#include <cassert>
#include "kern_xelp_memory_layout.hpp"
using IOByteCount = uint64_t;
constexpr unsigned kIOMemoryMapperNone = 1;
struct IOMemoryDescriptor {
 uint64_t physical, dma, length=0x1000000, span=0x1000000;
 uint64_t getPhysicalSegment(uint64_t, IOByteCount *n, unsigned options=0) {
  *n=span; return options==kIOMemoryMapperNone ? physical : dma;
 }
 uint64_t getLength() { return length; }
};
struct { bool enabled=true; uint64_t pciBAR; } tglVM;
uint32_t tglPmapBARFirstPage, tglPmapBAREndPage;
HELPER
int main() {
 for (uint64_t base : {0x60000000ULL,0x4010000000ULL,0x800000000ULL}) {
  tglVM.pciBAR=base;
  IOMemoryDescriptor bar {base,0x10000000};
  assert(tglPrepareMMIOBAR(&bar));
  assert(tglPmapBARFirstPage==base/4096 && tglPmapBAREndPage==(base+0x1000000)/4096);
  bar.physical+=4096; assert(!tglPrepareMMIOBAR(&bar));
  bar.physical=base; bar.span=4096; assert(!tglPrepareMMIOBAR(&bar));
 }
 assert(!tglPrepareMMIOBAR(nullptr));
 tglVM.enabled=false; IOMemoryDescriptor bar {0x800000000ULL,0x10000000};
 assert(!tglPrepareMMIOBAR(&bar));
}
'''.replace('HELPER', body).replace('#include <cassert>', '#include <cassert>\n#include <initializer_list>')
with tempfile.TemporaryDirectory(prefix='xelp-bar-') as d:
    root = Path(d)
    (root / 'test.cpp').write_text(fixture)
    subprocess.run(['clang++', '-std=c++17', '-fsanitize=address,undefined',
                    '-I'+str(repo / 'WhateverXe'), str(root / 'test.cpp'),
                    '-o', str(root / 'test')], check=True)
    subprocess.run([str(root / 'test')], check=True)
print('PASS: physical BAR admission with mapped DMA aliases and invalid spans')
