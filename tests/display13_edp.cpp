#include <cassert>
#include "kern_display13_edp.hpp"
using namespace Display13EDP;
int main() {
    assert(legacy(0x6F000) && legacy(0x6FFFC) && legacy(0x7F008) && legacy(0x7FFFC));
    assert(!legacy(0x6EFFC) && !legacy(0x70000) && !legacy(0x7EFFC) && !legacy(0x80000) && !legacy(0x60000) && !legacy(0x46140));
    // The legacy bank is the panel's own bank: timings, M/N, DDI function, PSR, PIPECONF, scanline, frame counter.
    assert(remap(0x6F000,0)==0x60000 && remap(0x6F040,0)==0x60040 && remap(0x6F044,0)==0x60044 && remap(0x6F400,0)==0x60400 && remap(0x6F800,0)==0x60800);
    assert(remap(0x7F008,0)==0x70008 && remap(0x7F000,0)==0x70000 && remap(0x7F040,0)==0x70040 && remap(0x7F030,0)==0x70030);
    assert(remap(0x6F008,1)==0x61008 && remap(0x7F008,2)==0x72008);
    // Ordinary banks and other registers are untouched, and so is everything without a pipe.
    assert(remap(0x60008,0)==0x60008 && remap(0x70008,0)==0x70008 && remap(0x46140,0)==0x46140 && remap(0x164280,0)==0x164280);
    assert(remap(0x6F008,3)==0x6F008 && remap(0x7F008,3)==0x7F008);
    assert(clockSelectRegister(0x46140,0) && clockSelectRegister(0x46144,1) && clockSelectRegister(0x46148,2));
    assert(!clockSelectRegister(0x46144,0) && !clockSelectRegister(0x46140,3) && !clockSelectRegister(0x4613C,0));
    // A zero select keeps the panel's DDI (TGL encoding, DDI + 1 in bits 31:28); an explicit select is kept.
    assert(clockSelect(0,0)==0x10000000 && clockSelect(0,1)==0x20000000 && clockSelect(0,2)==0x30000000);
    assert(clockSelect(0x10000000,0)==0x10000000 && clockSelect(0x20000000,0)==0x20000000 && clockSelect(0xF0000000,0)==0xF0000000);
    assert(clockSelect(0x0000000F,0)==0x1000000F);
    assert(clockSelect(0,3)==0 && clockSelect(5,4)==5);
    // Only the light-up path's write is a zero select to keep; shutdown's zero stays a disconnect.
    assert(enableSelectWrite(static_cast<uint64_t>(-0x334B)) && !enableSelectWrite(static_cast<uint64_t>(-0x60EC)) && !enableSelectWrite(0));
    return 0;
}
