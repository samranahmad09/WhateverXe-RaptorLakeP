// Raptor Lake-P GT registers read for the record and never written: the graphics interrupt controller, the forcewake
// requests and acknowledgements, the render and blitter command streamers' ring and execlist registers, and the GT
// power state (register names from Linux i915, i915_reg.h and intel_gt_regs.h, display version 13). A snapshot is
// taken at the first engine enable after boot (N97GTBoot), right after a cold power-well restore (N97GTRestore) and at
// the end of the port trace, four seconds after any restore (N97GTWake), so a wake's GT state reads against the state
// the accelerator left at boot. Round 51: the panel lit after a real power loss, the GPU then executed nothing, no
// hardware interrupt reached the accelerator, and its engine start never ran again. A read of a register in a
// power-gated domain returns zero or a stale value, never a hang; the acknowledgement words say which domains were
// awake for the reads.
#ifndef KERN_RPLP_GT_HPP
#define KERN_RPLP_GT_HPP
#include <stdint.h>
namespace RPLPGT {
constexpr uint32_t Regs[] {
    // The graphics interrupt controller: GFX_MSTR_IRQ, GT_INTR_DW0/1, RENDER_COPY / VCS_VECS / GUC_SG / GPM_WGBOXPERF
    // interrupt enables, the RCS0 / BCS / VCS0_VCS1 / VECS0_VECS1 / GUC_SG interrupt masks, DISPLAY_INT_CTL.
    0x190010,0x190018,0x19001C,0x190030,0x190034,0x190038,0x19003C,0x190090,0x1900A0,0x1900A8,0x1900D0,0x1900E8,0x44200,
    // Forcewake: the render, media and GT requests (FORCEWAKE_MT) and their acknowledgements.
    0xA278,0xD84,0xA270,0xD88,0xA188,0x130044,
    // The render command streamer: RING_TAIL, HEAD, START, CTL, ACTHD, HWS_PGA, MI_MODE, RING_IMR, GFX_MODE,
    // EXECLIST_STATUS lo/hi, RING_CONTEXT_CONTROL, the context status buffer pointer, EXECLIST_SQ_CONTENTS, EXECLIST_CONTROL.
    0x2030,0x2034,0x2038,0x203C,0x2074,0x2080,0x209C,0x20A8,0x229C,0x2234,0x2238,0x2244,0x23A0,0x2510,0x2550,
    // The blitter command streamer: RING_TAIL, HEAD, START, CTL, HWS_PGA, RING_IMR, EXECLIST_STATUS lo, the CSB pointer.
    0x22030,0x22034,0x22038,0x2203C,0x22080,0x220A8,0x22234,0x223A0,
    // GT power: RC_CONTROL, RC_STATE, RPNSWREQ, RP_CONTROL, PG_ENABLE, GT_CORE_STATUS, the RC6 residency counter.
    0xA090,0xA094,0xA008,0xA024,0xA210,0x138060,0x138108
};
constexpr unsigned Count=sizeof(Regs)/sizeof(Regs[0]);
// The record: the registers in that order, then uptime ns, when (0 the first engine enable, 1 after a cold restore,
// 2 the trace's end, 3 the accelerator's engine start returned, 4 will-sleep), cold | 2 when the three forcewake
// domains were held through the native path's counted request for the reads, and the wake generation.
constexpr unsigned Words=Count+4;
template<class IO> void snapshot(IO &io,uint64_t *w,uint32_t when,uint32_t cold,uint64_t generation,uint64_t now) {
    for(unsigned i=0;i<Count;++i)w[i]=io.read(Regs[i]);
    w[Count]=now;w[Count+1]=when;w[Count+2]=cold;w[Count+3]=generation;
}
}
#endif
