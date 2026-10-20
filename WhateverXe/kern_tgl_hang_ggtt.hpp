// Failure-only GGTT witnesses. Matching CPU/PTE samples do not prove GPU TLB state.
#ifndef kern_tgl_hang_ggtt_hpp
#define kern_tgl_hang_ggtt_hpp
#include <stdint.h>
namespace TGLHangGGTT {
struct Capture {
 uint64_t q[16]; // version, guard, start/end ns, rows, mismatches, unreadable, unstable
 uint64_t rows[29][8]; // kind, index, CPU, GPU, physical, PTE before/after, status bits
};
template<class Physical,class PTE>
void page(Capture &c,unsigned kind,unsigned index,uint64_t cpu,uint64_t gpu,
          uint64_t expected,bool known,Physical physical,PTE pte) {
 if(c.q[4]>=29)return;
 auto r=c.rows[c.q[4]++];r[0]=kind;r[1]=index;r[2]=cpu;r[3]=gpu;r[4]=expected;
 // Validate the complete request before any MMIO read.
 if(!known || (cpu>>47)!=0x1ffff || (cpu&4095) || !gpu || (gpu&4095) ||
    gpu>0xfffff000ULL || (expected&4095) || expected>0x7ffffff000ULL) {
  r[7]=1;++c.q[6];return;
 }
 uint64_t before=0,after=0;
 if(!physical(cpu,before)) {r[7]=1;++c.q[6];return;}
 r[5]=pte(gpu);r[6]=pte(gpu);
 if(!physical(cpu,after) || before!=expected || after!=before || r[5]!=r[6]) {
  r[7]|=2;++c.q[7];
 }
 if(!(r[5]&1) || (r[5]&0x7ffffff000ULL)!=expected ||
    !(r[6]&1) || (r[6]&0x7ffffff000ULL)!=expected) {
  r[7]|=4;++c.q[5];
 }
}
}
#endif
