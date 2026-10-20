#include "kern_display13_backlight.hpp"
#include <assert.h>
#include <stdio.h>
using namespace Display13Backlight;
int main() {
    // The firmware's state on the reference machine: controller enabled, 19200-cycle period (1 kHz), 35 % duty.
    assert(plan(0x80000000,0x4B00,70).valid && plan(0x80000000,0x4B00,70).duty==13440);
    assert(plan(0x80000000,0x4B00,100).duty==0x4B00 && plan(0x80000000,0x4B00,10).duty==1920);
    assert(plan(0x80000000,0x4B00,35).duty==6720);
    // Refusals: controller off, unreadable registers, implausible period, out-of-range percent.
    assert(!plan(0,0x4B00,70).valid && !plan(UINT32_MAX,0x4B00,70).valid && !plan(0x80000000,UINT32_MAX,70).valid);
    assert(!plan(0x80000000,999,70).valid && !plan(0x80000000,0x10000,70).valid);
    assert(!plan(0x80000000,0x4B00,9).valid && !plan(0x80000000,0x4B00,101).valid && !plan(0x80000000,0x4B00,0).valid);
    puts("Display13 backlight PWM probe plan: scaled duty, clamped percent, firmware-enabled controller only");
}
