#include "kern_rplp_gt.hpp"
#include <assert.h>
#include <map>
#include <stdio.h>
using namespace RPLPGT;
struct IO {
    std::map<uint32_t,uint32_t> r;
    unsigned reads {};
    uint32_t read(uint32_t reg) {++reads;auto it=r.find(reg);return it==r.end() ? 0xDEAD0000U|(reg&0xFFFF) : it->second;}
};
int main() {
    // Every register is 4-byte aligned, inside the 16 MiB bar, and listed once; the record ends with the four context words.
    for(unsigned i=0;i<Count;++i) {
        assert(!(Regs[i]&3) && Regs[i]<0x1000000);
        for(unsigned j=0;j<i;++j)assert(Regs[i]!=Regs[j]);
    }
    assert(Count==49 && Words==53);
    IO io;io.r[0x190010]=0x80000000;io.r[0xD84]=0x1;io.r[0x2034]=0x1234;io.r[0x138108]=77;
    uint64_t w[Words];
    snapshot(io,w,2,1,5,123456789ULL);
    assert(io.reads==Count && w[0]==0x80000000 && w[14]==1 && w[20]==0x1234 && w[Count-1]==77);
    assert(w[Count]==123456789ULL && w[Count+1]==2 && w[Count+2]==1 && w[Count+3]==5);
    // The order is the documented one: interrupts first, forcewake, the render streamer, the blitter, power.
    assert(Regs[0]==0x190010 && Regs[12]==0x44200 && Regs[13]==0xA278 && Regs[18]==0x130044 && Regs[19]==0x2030 &&
           Regs[33]==0x2550 && Regs[34]==0x22030 && Regs[41]==0x223A0 && Regs[42]==0xA090 && Regs[48]==0x138108);
    puts("rplp_gt: ok");
    return 0;
}
