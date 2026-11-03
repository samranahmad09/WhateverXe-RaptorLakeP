// Raptor Lake-P, system wake after a real power loss: the state of the panel's port that the display engine lost and
// the native framebuffer's link training needs again (round 48: the training's first phase failed about two seconds
// into every wake although every power well, the clock, the plane buffers and the combo PHYs had been restored).
//
// snapshot() reads the registers behind the port's bring-up in one fixed order so a wake can be compared with the boot
// (N97FBPortBoot, taken at the first engine enable while the firmware's state is intact) word for word.
//
// restorePPS() and restorePLL() recreate two pieces of that boot state when a cold wake lost them and the firmware's
// values are known: the panel power sequencer's delays, and the port's PLL with its clock selection. Both follow the
// sequence Linux's i915 uses on this display generation (pps_init_registers; icl_pll_power_enable, icl_dpll_write,
// icl_pll_enable, then the DDI clock select and ungating in two separate writes). Both verify first and write nothing
// when the state survived, when the boot reference is empty, or when any read returns all-ones.
#pragma once
#include <stdint.h>

namespace RPLPPort {
constexpr unsigned Words = 32;
// Register order of a snapshot. [31] is left to the caller (a timestamp).
constexpr uint32_t Regs[30] {
    0xC7200, // 0 PP_STATUS
    0xC7204, // 1 PP_CONTROL
    0xC7208, // 2 PP_ON_DELAYS
    0xC720C, // 3 PP_OFF_DELAYS
    0x46010, // 4 DPLL0_ENABLE
    0x46014, // 5 DPLL1_ENABLE
    0x164280, // 6 DPCLKA_CFGCR0 (DDI clock select and gating)
    0x164284, // 7 DPLL0_CFGCR0
    0x164288, // 8 DPLL0_CFGCR1
    0x16428C, // 9 DPLL1_CFGCR0
    0x164290, // 10 DPLL1_CFGCR1
    0x164B00, // 11 DPLL0_DIV0
    0x164C00, // 12 DPLL1_DIV0
    0x64000, // 13 DDI_BUF_CTL A
    0x64100, // 14 DDI_BUF_CTL B
    0x60540, // 15 DP_TP_CTL (transcoder A)
    0x60544, // 16 DP_TP_STATUS (transcoder A)
    0x60400, // 17 TRANS_DDI_FUNC_CTL A
    0x70008, // 18 TRANS_CONF A
    0x46140, // 19 TRANS_CLK_SEL A
    0x162028, // 20 PORT_CL_DW10 PHY A (lane power down)
    0x162030, // 21 PORT_CL_DW12 PHY A
    0x162014, // 22 PORT_CL_DW5 PHY A
    0x64010, // 23 DDI_AUX_CTL A
    0x64C00, // 24 PHY_MISC A
    0xC8250, // 25 PCH backlight PWM control 1
    0x60800, // 26 PSR control (transcoder A)
    0x45504, // 27 DC_STATE_EN
    0x45454, // 28 PWR_WELL_CTL_DDI
    0x45444, // 29 PWR_WELL_CTL_AUX
};
constexpr unsigned Count = sizeof(Regs) / sizeof(Regs[0]);
// Indices into a snapshot.
enum : unsigned {PPStatus, PPControl, PPOnDelays, PPOffDelays, PLL0Enable, PLL1Enable, ClockSelect, PLL0CFGCR0, PLL0CFGCR1,
    PLL1CFGCR0, PLL1CFGCR1, PLL0Div0, PLL1Div0};
// PP_CONTROL: the power cycle delay field (bits 8..4 on this generation) and the bits the native path owns.
constexpr uint32_t PPCycleDelayMask = 0x1F0, PPForceVDD = 1U << 3, PPBacklight = 1U << 2, PPReset = 1U << 1, PPOn = 1U << 0;
// DPLLn_ENABLE bits.
constexpr uint32_t PLLEnable = 1U << 31, PLLLock = 1U << 30, PLLPowerEnable = 1U << 27, PLLPowerState = 1U << 26;
// DPCLKA_CFGCR0 for combo PHY A (port A): the clock select field and the clock-off bit (i915 ICL_DPCLKA_CFGCR0_*).
constexpr uint32_t ClockSelectMaskA = 3U, ClockOffA = 1U << 10;
constexpr uint32_t Div0AFCStartupMask = 7U << 25; // TGL_DPLL0_DIV0_AFC_STARTUP_MASK

template<class IO> void snapshot(IO &io, uint64_t (&w)[Words]) {
    for (unsigned i = 0; i < Count; ++i) w[i] = io.read(Regs[i]);
    w[Count] = 1; // Schema of the trailing words: [30] = 1, [31] caller's timestamp.
}

enum class Status : uint32_t {
    Skipped,     // Nothing to do: the state survived, or the reference carries no state to recreate.
    Applied,     // Written and read back as the reference.
    NoReference, // The boot reference is empty or unreadable.
    Unreadable,  // A register read all-ones.
    Mismatch,    // Written, but a read back disagrees.
    Timeout,     // The PLL's power state or lock bit never came.
    Unsupported, // The reference selects a PLL this path does not program (only DPLL0 and DPLL1).
};

// The panel power sequencer's delays, as i915's pps_init_registers rewrites them after a resume: PP_ON_DELAYS,
// PP_OFF_DELAYS and the power cycle delay field of PP_CONTROL, from the boot reference. Only when both delay registers
// read zero (the reset value after a power loss) and the reference holds delays. The power-on, VDD, backlight and
// reset bits of PP_CONTROL are the native path's and are not written here; the unlock field (bits 31..16) is kept
// as read. r[0] reads before (PP_CONTROL), r[1] PP_ON_DELAYS before, r[2] PP_OFF_DELAYS before, r[3..5] the same after.
template<class IO> Status restorePPS(IO &io, const uint64_t (&boot)[Words], uint64_t (&r)[8]) {
    const uint32_t ctl = io.read(Regs[PPControl]), on = io.read(Regs[PPOnDelays]), off = io.read(Regs[PPOffDelays]);
    r[0] = ctl; r[1] = on; r[2] = off; r[3] = r[4] = r[5] = 0; r[6] = 0; r[7] = 0;
    if (ctl == UINT32_MAX || on == UINT32_MAX || off == UINT32_MAX) return Status::Unreadable;
    const uint32_t bootCtl = static_cast<uint32_t>(boot[PPControl]), bootOn = static_cast<uint32_t>(boot[PPOnDelays]),
        bootOff = static_cast<uint32_t>(boot[PPOffDelays]);
    if (bootCtl == UINT32_MAX || bootOn == UINT32_MAX || bootOff == UINT32_MAX || (!bootOn && !bootOff)) return Status::NoReference;
    if (on || off) return Status::Skipped; // Programmed: the firmware's or the native path's own values.
    io.write(Regs[PPOnDelays], bootOn);
    io.write(Regs[PPOffDelays], bootOff);
    io.write(Regs[PPControl], (ctl & ~PPCycleDelayMask) | (bootCtl & PPCycleDelayMask));
    r[3] = io.read(Regs[PPControl]); r[4] = io.read(Regs[PPOnDelays]); r[5] = io.read(Regs[PPOffDelays]); r[6] = 1;
    if (r[4] != bootOn || r[5] != bootOff || (r[3] & PPCycleDelayMask) != (bootCtl & PPCycleDelayMask)) return Status::Mismatch;
    return Status::Applied;
}

// The port A clock: the PLL the boot reference selects for DDI A, powered, programmed with the reference's CFGCR0/CFGCR1
// (and the AFC startup field of DIV0), enabled and locked, then selected and ungated for the port in DPCLKA_CFGCR0, as
// i915's combo_pll_enable and icl_ddi_combo_enable_clock do. Only when the reference had the PLL enabled and the port's
// clock on, and the PLL now reads neither enabled nor locked. `wait(us)` delays; the PLL is given 1000 us per bit
// (i915: 1 ms, the lock is specified at 600 us). r[0] PLL index, r[1] enable register before, r[2] after power enable,
// r[3] after enable, r[4] DPCLKA before, r[5] DPCLKA after, r[6] applied, r[7] the step that failed (1 power, 2 lock,
// 3 clock select, 4 CFGCR read back).
template<class IO, class Wait> Status restorePLL(IO &io, const uint64_t (&boot)[Words], Wait wait, uint64_t (&r)[8]) {
    for (auto &v : r) v = 0;
    const uint32_t bootSel = static_cast<uint32_t>(boot[ClockSelect]);
    if (bootSel == UINT32_MAX) return Status::NoReference;
    if (bootSel & ClockOffA) return Status::Skipped; // The firmware never clocked port A: nothing to recreate.
    const unsigned pll = bootSel & ClockSelectMaskA;
    r[0] = pll;
    if (pll > 1) return Status::Unsupported;
    const uint32_t enableReg = Regs[pll ? PLL1Enable : PLL0Enable], cfg0Reg = Regs[pll ? PLL1CFGCR0 : PLL0CFGCR0],
        cfg1Reg = Regs[pll ? PLL1CFGCR1 : PLL0CFGCR1], div0Reg = Regs[pll ? PLL1Div0 : PLL0Div0];
    const uint32_t bootEnable = static_cast<uint32_t>(boot[pll ? PLL1Enable : PLL0Enable]),
        bootCfg0 = static_cast<uint32_t>(boot[pll ? PLL1CFGCR0 : PLL0CFGCR0]), bootCfg1 = static_cast<uint32_t>(boot[pll ? PLL1CFGCR1 : PLL0CFGCR1]),
        bootDiv0 = static_cast<uint32_t>(boot[pll ? PLL1Div0 : PLL0Div0]);
    if (bootEnable == UINT32_MAX || bootCfg0 == UINT32_MAX || bootCfg1 == UINT32_MAX || !(bootEnable & PLLEnable)) return Status::NoReference;
    const uint32_t enable = io.read(enableReg), sel = io.read(Regs[ClockSelect]);
    r[1] = enable; r[4] = sel;
    if (enable == UINT32_MAX || sel == UINT32_MAX) return Status::Unreadable;
    if (enable & (PLLEnable | PLLLock)) return Status::Skipped; // Running: the firmware's state survived, or the native path's.
    // i915 icl_pll_power_enable: the power enable bit, then its state bit.
    io.write(enableReg, enable | PLLPowerEnable);
    uint32_t v = io.read(enableReg); unsigned waited = 0;
    while (v != UINT32_MAX && !(v & PLLPowerState) && waited < 1000) { wait(10); waited += 10; v = io.read(enableReg); }
    r[2] = v;
    if (v == UINT32_MAX) return Status::Unreadable;
    if (!(v & PLLPowerState)) { r[7] = 1; return Status::Timeout; }
    // i915 icl_dpll_write: CFGCR0, CFGCR1, the AFC startup field of DIV0, then a posting read of CFGCR1.
    io.write(cfg0Reg, bootCfg0);
    io.write(cfg1Reg, bootCfg1);
    if (bootDiv0 != UINT32_MAX) {
        const uint32_t div0 = io.read(div0Reg);
        if (div0 != UINT32_MAX && (div0 & Div0AFCStartupMask) != (bootDiv0 & Div0AFCStartupMask))
            io.write(div0Reg, (div0 & ~Div0AFCStartupMask) | (bootDiv0 & Div0AFCStartupMask));
    }
    if (io.read(cfg0Reg) != bootCfg0 || io.read(cfg1Reg) != bootCfg1) { r[7] = 4; return Status::Mismatch; }
    // i915 icl_pll_enable: the enable bit, then the lock.
    io.write(enableReg, v | PLLEnable);
    v = io.read(enableReg); waited = 0;
    while (v != UINT32_MAX && !(v & PLLLock) && waited < 1000) { wait(10); waited += 10; v = io.read(enableReg); }
    r[3] = v;
    if (v == UINT32_MAX) return Status::Unreadable;
    if (!(v & PLLLock)) { r[7] = 2; return Status::Timeout; }
    // i915 _icl_ddi_enable_clock: the select field, then the clock-off bit cleared, in two separate writes.
    io.write(Regs[ClockSelect], (sel & ~ClockSelectMaskA) | pll);
    io.write(Regs[ClockSelect], io.read(Regs[ClockSelect]) & ~ClockOffA);
    r[5] = io.read(Regs[ClockSelect]); r[6] = 1;
    if (r[5] == UINT32_MAX) return Status::Unreadable;
    if ((r[5] & (ClockSelectMaskA | ClockOffA)) != pll) { r[7] = 3; return Status::Mismatch; }
    return Status::Applied;
}
} // namespace RPLPPort
