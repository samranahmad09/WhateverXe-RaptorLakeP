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
    // Combo PHY A tuning words: group and per-lane DW2/DW4/DW5/DW7 only; the firmware's values are
    // inherited field by field, keeping the driver's loadgen and training-enable bits.
    unsigned dword=0,lane=9;
    assert(phyTuningRegister(0x162688,dword,lane) && dword==2 && lane==4);
    assert(phyTuningRegister(0x162690,dword,lane) && dword==4 && lane==4);
    assert(phyTuningRegister(0x162694,dword,lane) && dword==5 && lane==4);
    assert(phyTuningRegister(0x16269C,dword,lane) && dword==7 && lane==4);
    assert(phyTuningRegister(0x162888,dword,lane) && dword==2 && lane==0);
    assert(phyTuningRegister(0x162990,dword,lane) && dword==4 && lane==1);
    assert(phyTuningRegister(0x162A94,dword,lane) && dword==5 && lane==2);
    assert(phyTuningRegister(0x162B9C,dword,lane) && dword==7 && lane==3);
    assert(!phyTuningRegister(0x162884,dword,lane) && !phyTuningRegister(0x162804,dword,lane) && !phyTuningRegister(0x162028,dword,lane));
    assert(!phyTuningRegister(0x16210C,dword,lane) && !phyTuningRegister(0x162C88,dword,lane) && !phyTuningRegister(0x6C888,dword,lane));
    PHYTuning fw;for(unsigned l=0;l<4;++l) {fw.dw2[l]=0x2098+l;fw.dw4[l]=0x80120345+(l<<4);fw.dw5[l]=0xA0080030+l;fw.dw7[l]=0x7A001111+l;}
    assert(phyInherit(fw,2,1,0x3098)==0x2099 && phyInherit(fw,2,4,0x3098)==0x2098);
    assert(phyInherit(fw,4,2,0x00007038)==(0x00020000|0x00000365) && phyInherit(fw,4,1,0x80287038)==(0x80280000|0x00020355));
    assert(phyInherit(fw,5,0,0x00390030)==0x20080030 && phyInherit(fw,5,4,0x80390030)==0xA0080030 && phyInherit(fw,5,3,0x80390030)==0xA0080033);
    assert(phyInherit(fw,7,0,0x00FFFFFF)==0x7AFFFFFF && phyInherit(fw,7,2,0x81000000)==0xFA000000);
    return 0;
}
