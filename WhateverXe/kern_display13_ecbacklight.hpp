// Raptor Lake-P brightness through the embedded controller (R4). The display engine's PWM pin does not reach this
// panel (N97RPLPBacklightProbe, 2026-10-21); the firmware exposes the EC's level through a WMI device (on the
// reference machine \_SB.PC00.AWMI, GUID 603E9613-EF25-4338-A3D0-C46177516DB7, method WMAA) that Linux's
// nvidia-wmi-ec-backlight driver and Windows use: method 1 = level (mode 0 get, 1 set, 2 maximum), method 2 =
// source (1 GPU, 2 EC). The bridge watches the framebuffer's PWM duty writes and forwards the percentage; every
// rule is here, the ACPI calls and the deferred work are in the plugin.
#ifndef KERN_DISPLAY13_ECBACKLIGHT_HPP
#define KERN_DISPLAY13_ECBACKLIGHT_HPP
#include <stdint.h>
#include <stddef.h>
namespace Display13ECBacklight {
constexpr uint8_t GUID[16] {0x13,0x96,0x3E,0x60,0x25,0xEF,0x38,0x43,0xA3,0xD0,0xC4,0x61,0x77,0x51,0x6D,0xB7};
constexpr uint32_t MethodLevel=1, MethodSource=2, ModeGet=0, ModeSet=1, ModeGetMax=2, SourceGPU=1, SourceEC=2;
constexpr size_t ArgumentBytes=24, EntryBytes=20;
constexpr uint8_t FlagMethod=0x02;
struct Method {bool found {};char name[5] {};uint8_t instances {};uint8_t flags {};};
// _WDG is a list of 20-byte entries: the GUID, two object-ID characters (a notification ID and a reserved byte for
// event entries), the instance count and the flags. A method entry names the ACPI method "WM" + object ID.
inline Method find(const uint8_t *wdg,size_t length) {
    Method m;
    if(!wdg)return m;
    for(size_t off=0;off+EntryBytes<=length;off+=EntryBytes) {
        const uint8_t *e=wdg+off;
        bool same=true;
        for(unsigned i=0;i<16;++i)if(e[i]!=GUID[i]) {same=false;break;}
        if(!same)continue;
        m.instances=e[18];m.flags=e[19];
        if(!(m.flags&FlagMethod))continue;
        m.name[0]='W';m.name[1]='M';m.name[2]=static_cast<char>(e[16]);m.name[3]=static_cast<char>(e[17]);m.name[4]=0;
        m.found=true;return m;
    }
    return m;
}
// The 24-byte argument buffer the method reads: the mode byte at 0 (Linux passes a little-endian 32-bit word, the
// same first byte) and the value DWORD at +4; +8 is the method's return slot and the rest is padding.
inline void arguments(uint8_t out[ArgumentBytes],uint32_t mode,uint32_t value) {
    for(size_t i=0;i<ArgumentBytes;++i)out[i]=0;
    out[0]=static_cast<uint8_t>(mode);
    out[4]=static_cast<uint8_t>(value);out[5]=static_cast<uint8_t>(value>>8);
    out[6]=static_cast<uint8_t>(value>>16);out[7]=static_cast<uint8_t>(value>>24);
}
// The EC level for a PWM duty: duty over period, scaled to 0..100 and rounded; a duty at or above the period is
// full brightness; an unusable period refuses.
inline bool level(uint32_t duty,uint32_t freq,uint32_t &out) {
    if(freq==0 || freq==UINT32_MAX)return false;
    if(duty>=freq) {out=100;return true;}
    out=static_cast<uint32_t>((uint64_t(duty)*100+freq/2)/freq);
    return true;
}
// A visible self-test level: the current level raised by a step, clamped to the maximum; when there is no room
// above, a step below instead, so the change is always visible.
inline uint32_t probeLevel(uint32_t current,uint32_t max,uint32_t step=15) {
    if(max==0 || max>100)max=100;
    if(current>max)current=max;
    uint32_t up=current+step;
    if(up>max)up=max;
    if(up==current)return current>=step ? current-step : 0;
    return up;
}
}
#endif
