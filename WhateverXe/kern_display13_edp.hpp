// Display 13 drives the internal panel from an ordinary transcoder. The donor's
// legacy dedicated-eDP register bank (index 15, 0x6F000/0x7F000) and its implicit
// eDP transcoder clock do not exist here: that bank is the panel's own
// transcoder/pipe bank, and the panel's transcoder needs an explicit port clock
// select (TRANS_CLK_SEL bits 31:28 = DDI + 1) like every other Display 12+ one.
#ifndef KERN_DISPLAY13_EDP_HPP
#define KERN_DISPLAY13_EDP_HPP
#include <stdint.h>
namespace Display13EDP {
constexpr unsigned LegacyBank=15;
// Transcoder (0x6F000) and pipe (0x7F000) registers of the legacy eDP bank.
inline bool legacy(uint64_t reg) {return (reg>=0x6F000 && reg<0x70000) || (reg>=0x7F000 && reg<0x80000);}
// The same register in the panel's own transcoder/pipe bank.
inline uint64_t remap(uint64_t reg,unsigned pipe) {return pipe<3 && legacy(reg) ? reg-0x1000*LegacyBank+0x1000*pipe : reg;}
// TRANS_CLK_SEL of the panel's transcoder.
inline bool clockSelectRegister(uint64_t reg,unsigned pipe) {return pipe<3 && reg==0x46140+4*pipe;}
// A zero port select (the legacy eDP convention: the dedicated transcoder was
// clocked implicitly) leaves this transcoder without a clock; keep the panel's DDI.
// An explicit select is left alone.
inline uint32_t clockSelect(uint32_t value,unsigned ddi) {return (value>>28) || ddi>=3 ? value : (value&0x0FFFFFFFU)|((ddi+1U)<<28);}
// The light-up path's own clock-select write (the only zero select to keep on the DDI;
// the shutdown path's zero is a real clock disconnect): its return address relative to
// the hooked GMBUS request function in this exact donor image (0x50731 - 0x53A7C).
constexpr int64_t EnableSelectDelta=-0x334B;
inline bool enableSelectWrite(uint64_t rva) {return static_cast<int64_t>(rva)==EnableSelectDelta;}
// Combo PHY A transmitter tuning: PORT_TX DW2 (swing select, RCOMP), DW4 (cursor and
// post-cursor coefficients), DW5 (scaling, termination, training enable) and DW7
// (N_SCALAR), group writes at +0x600 and per-lane writes at +0x800 + 0x100 x lane. The
// firmware trained this panel with these values and leaves them in the PHY; the donor's
// per-level values are for another PHY generation (it never writes DW7 at all), so the
// firmware's are inherited: DW2 whole, DW4 bits 17:0, DW5 all but TX_TRAINING_EN
// (bit 31), DW7 bits 30:24. Lane power, loadgen and the training toggle stay the driver's.
constexpr uint32_t PHYA=0x162000;
struct PHYTuning {uint32_t dw2[4] {},dw4[4] {},dw5[4] {},dw7[4] {};bool valid {};};
// The tuning word a write addresses: dword 2, 4, 5 or 7, and lane 0..3 (4 for a group write).
inline bool phyTuningRegister(uint64_t reg,unsigned &dword,unsigned &lane) {
    if(reg<PHYA+0x600 || reg>=PHYA+0xC00)return false;
    const auto off=static_cast<uint32_t>(reg-PHYA);const auto low=off&0xFF,block=off>>8;
    if(low==0x88)dword=2;else if(low==0x90)dword=4;else if(low==0x94)dword=5;else if(low==0x9C)dword=7;else return false;
    if(block==6)lane=4;else if(block>=8 && block<=0xB)lane=block-8;else return false;
    return true;
}
inline uint32_t phyInherit(const PHYTuning &fw,unsigned dword,unsigned lane,uint32_t value) {
    const auto l=lane<4 ? lane : 0;
    switch(dword) {
    case 2: return fw.dw2[l];
    case 4: return (value&~0x3FFFFU)|(fw.dw4[l]&0x3FFFFU);
    case 5: return (fw.dw5[l]&0x7FFFFFFFU)|(value&0x80000000U);
    case 7: return (value&~0x7F000000U)|(fw.dw7[l]&0x7F000000U);
    }
    return value;
}
}
#endif
