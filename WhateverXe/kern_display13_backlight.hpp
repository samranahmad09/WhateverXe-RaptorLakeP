// Display 13 backlight PWM (BXT-style controller 1 in the south display engine, Linux intel_backlight_regs.h):
// CTL1 0xC8250 (bit 31 enable, bit 29 polarity), FREQ1 0xC8254 (period in rawclk cycles), DUTY1 0xC8258 (on time
// in the same unit). The probe writes DUTY1 once from a percentage; every rule is here, the hardware write is in
// the plugin.
#ifndef KERN_DISPLAY13_BACKLIGHT_HPP
#define KERN_DISPLAY13_BACKLIGHT_HPP
#include <stdint.h>
namespace Display13Backlight {
constexpr uint32_t CTL1=0xC8250, FREQ1=0xC8254, DUTY1=0xC8258, Enable=1U<<31;
constexpr uint32_t MinPercent=10, MaxPercent=100; // Never below 10 %: a dark panel hides the result.
struct Plan {bool valid {};uint32_t duty {};};
// Percent is the requested brightness. The period must be sane (the VBT says 1 kHz from 19.2 MHz: 19200 cycles)
// and the controller must already be enabled by the firmware; the probe never enables or reconfigures it.
inline Plan plan(uint32_t ctl,uint32_t freq,uint32_t percent) {
    Plan p;
    if(!(ctl&Enable) || ctl==UINT32_MAX || freq==UINT32_MAX || freq<1000 || freq>0xFFFF)return p;
    if(percent<MinPercent || percent>MaxPercent)return p;
    p.duty=static_cast<uint32_t>((uint64_t(freq)*percent+50)/100);
    if(p.duty>freq)p.duty=freq;
    p.valid=true;return p;
}
}
#endif
