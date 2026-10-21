#include "kern_display13_ecbacklight.hpp"
#include <assert.h>
#include <stdio.h>
#include <string.h>
using namespace Display13ECBacklight;
int main() {
    // The reference machine's _WDG: the EC backlight GUID with object ID "AA", one instance, a method (flags 2),
    // followed by an unrelated event entry (the second GUID, notification ID 0x30, flags 0).
    const uint8_t wdg[40] {
        0x13,0x96,0x3E,0x60,0x25,0xEF,0x38,0x43,0xA3,0xD0,0xC4,0x61,0x77,0x51,0x6D,0xB7,0x41,0x41,0x01,0x02,
        0x21,0x12,0x90,0x05,0x66,0xD5,0xD1,0x11,0xB2,0xF0,0x00,0xA0,0xC9,0x06,0x29,0x10,0x30,0x30,0x01,0x00};
    const auto m=find(wdg,sizeof(wdg));
    assert(m.found && !strcmp(m.name,"WMAA") && m.instances==1 && m.flags==FlagMethod);
    // The entry must be a method, the GUID must match, and partial entries never match.
    uint8_t event[40];memcpy(event,wdg,sizeof(wdg));event[19]=0;
    assert(!find(event,sizeof(event)).found);
    uint8_t other[40];memcpy(other,wdg,sizeof(wdg));other[3]=0x61;
    assert(!find(other,sizeof(other)).found);
    assert(!find(wdg,19).found && !find(nullptr,40).found && !find(wdg,0).found);
    // A later entry is found too, and the object ID is read from that entry.
    uint8_t second[40];memcpy(second,wdg+20,20);memcpy(second+20,wdg,20);second[36]='B';second[37]='C';
    const auto s=find(second,sizeof(second));
    assert(s.found && !strcmp(s.name,"WMBC"));
    // The argument buffer: mode byte first, the value DWORD at +4, everything else zero.
    uint8_t args[ArgumentBytes];arguments(args,ModeSet,0x01020304);
    assert(args[0]==1 && args[1]==0 && args[2]==0 && args[3]==0);
    assert(args[4]==4 && args[5]==3 && args[6]==2 && args[7]==1);
    for(unsigned i=8;i<ArgumentBytes;++i)assert(args[i]==0);
    arguments(args,ModeGetMax,0);assert(args[0]==2 && args[4]==0);
    // Duty to level: the firmware's 6701 of 17777 is 38 %, the probe's 12444 is 70 %, full and beyond is 100 %.
    uint32_t l=0;
    assert(level(6701,17777,l) && l==38);
    assert(level(12444,17777,l) && l==70);
    assert(level(17777,17777,l) && l==100 && level(20000,17777,l) && l==100);
    assert(level(0,17777,l) && l==0 && level(89,17777,l) && l==1);
    assert(!level(100,0,l) && !level(100,UINT32_MAX,l));
    // The self-test level: a visible step up, clamped, or a step down at the top.
    assert(probeLevel(35,100)==50 && probeLevel(90,100)==100 && probeLevel(100,100)==85);
    assert(probeLevel(35,0)==50 && probeLevel(120,100)==85 && probeLevel(5,100)==20 && probeLevel(10,10)==0);
    puts("Display13 EC backlight: _WDG method lookup, argument layout, duty-to-level scaling and self-test level fixtures passed");
}
