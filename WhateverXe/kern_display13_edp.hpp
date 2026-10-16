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
}
#endif
