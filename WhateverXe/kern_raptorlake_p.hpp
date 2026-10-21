// RPL-P/RPL-U identity, read-only GT/eDP evidence and the opt-in BCS self-test.
// RaptorLakeP stays catalogued without a GT backend, so XELP admission, DVMT,
// donor start and every Display 13 write path remain off. Observation writes
// only AlderLakeN::readGT's balanced forcewake request/release; the self-test
// (qualifiedGT, leaseGPU, AlderLakeN::testGPU) also writes its leased PTEs and
// BCS engine registers.
// Linux intel_step.c adlp_rplp_revids and intel_display_device.c
// adl_p_rpl_pu_steppings cover both RPL-P and RPL-U: revision 4 is GT C0 /
// display E0. Other revisions have no stepping and no WA review.
#ifndef kern_raptorlake_p_hpp
#define kern_raptorlake_p_hpp
#include <stdint.h>
#include "kern_xelp_platform.hpp"
#include "kern_alderlake_n.hpp"
#include "kern_alderlake_n_gpu.hpp"

namespace RaptorLakeP {
constexpr uint32_t DisplayVersion = 13;

inline bool family(uint32_t identity) {
    return XELP::identify(identity,0).family==XELP::Family::RaptorLakeP;
}
struct Stepping { const char *gt {}, *display {}; };
inline Stepping stepping(uint32_t revision) {
    return revision==4 ? Stepping {"C0","E0"} : Stepping {};
}

// Gen12 SSEU (Linux gen12_sseu_info_init): one slice, up to six DSS, one
// disable bit per EU pair. Native subslices are twice the DSS population, as
// for AlderLakeN. 0x9118 bits3:0 disable L3 bank pairs (Linux l3bank_mask);
// the donor's bank count for a four-DSS GT is not inferred here. 0x9140
// disables VDBOX (bits7:0) and VEBOX (bits19:16) instances.
struct Resources {
    uint32_t slices {}, dss {}, nativeSubslices {}, eus {}, l3BankPairs {}, vdbox {}, vebox {};
};
inline bool resources(uint32_t slice, uint32_t dss, uint32_t euDisable,
                      uint32_t l3Disable, uint32_t media, Resources &out) {
    out = {};
    if (slice != 1 || !dss || (dss&~0x3FU) || (euDisable&~0xFFU) || euDisable==0xFF ||
        !(~l3Disable&0xFU))
        return false;
    const auto count=static_cast<uint32_t>(__builtin_popcount(dss));
    const auto eus=2*count*(8-static_cast<uint32_t>(__builtin_popcount(euDisable)));
    out = {1, count, 2*count, eus, ~l3Disable&0xFU, ~media&0xFFU, (~media>>16)&0xFU};
    return true;
}

// L3: 0x9118 bits7:0 disable one bank each of the eight a full Xe-LP GT has.
// That reads AlderLakeN's 0xF0 as its four banks and this part's 0x88 as six,
// matching Mesa's intel_device_info_update_l3_banks for three to five DSS.
inline uint32_t l3Banks(uint32_t l3Disable) {
    return 8-static_cast<uint32_t>(__builtin_popcount(l3Disable&0xFFU));
}

// The GT profile the backend may use: only the fuse set measured on hardware
// (R1, i7-13620H A7A8 rev 4) qualifies. URB follows the TGL PRM's configurations,
// 64KiB per L3 bank: 256KiB at four banks (GT1), 384KiB at six (GT1.xf).
inline bool qualifiedGT(uint32_t slice, uint32_t dss, uint32_t euDisable,
                        uint32_t l3Disable, uint32_t media, AlderLakeN::Resources &out) {
    out = {};
    Resources r;
    if (!resources(slice, dss, euDisable, l3Disable, media, r) || dss != 0xF || euDisable ||
        (l3Disable&0xFFU) != 0x88 || media != 0x000E00FE)
        return false;
    out = {r.slices, r.dss, r.nativeSubslices, r.eus, l3Banks(l3Disable), 64*l3Banks(l3Disable)};
    return true;
}

// AlderLakeN::readGT judges fuses against the ADL-N profile. A readout whose reads
// and release all succeeded (Complete or ProfileMismatch) is re-judged here
// against qualifiedGT, so the shared Display 13 path gets this part's values.
inline void qualify(AlderLakeN::GTReadout &gt) {
    if (gt.status != AlderLakeN::GTStatus::Complete && gt.status != AlderLakeN::GTStatus::ProfileMismatch) return;
    gt.status = qualifiedGT(gt.fuses[0], gt.fuses[1], gt.fuses[2], gt.fuses[3], gt.fuses[4], gt.resources) ?
        AlderLakeN::GTStatus::Complete : AlderLakeN::GTStatus::ProfileMismatch;
}

// The Gen12 forcewake/fuse/engine protocol is AlderLakeN::readGT's. Its final
// Complete or ProfileMismatch status means every read succeeded and both
// domains were released; only the profile verdict is re-evaluated here.
struct GTReadout {
    AlderLakeN::GTReadout forcewake;
    Resources resources;
};
static_assert(sizeof(GTReadout)==67*4,"RaptorLakeP GT readout layout");

template<class IO> GTReadout readGT(IO &io, bool admitted) {
    GTReadout out;
    out.forcewake=AlderLakeN::readGT(io,admitted);
    auto &gt=out.forcewake;
    if (gt.status==AlderLakeN::GTStatus::Complete || gt.status==AlderLakeN::GTStatus::ProfileMismatch) {
        gt.resources={};
        gt.status=resources(gt.fuses[0],gt.fuses[1],gt.fuses[2],gt.fuses[3],gt.fuses[4],out.resources) ?
            AlderLakeN::GTStatus::Complete : AlderLakeN::GTStatus::ProfileMismatch;
    }
    return out;
}

// End of every enabled primary plane on a powered pipe, from the display
// readout: linear only (PLANE_CTL bits12:10), stride in 64-byte units (bits11:0),
// height from PLANE_SIZE bits28:16. A tiled or incompletely read plane fails closed.
inline bool scanoutEnd(const AlderLakeN::DisplayReadout &display, uint64_t &end) {
    end = 0;
    for (unsigned pipe = 0; pipe < 3; ++pipe) {
        if (!(display.poweredPipes & (1U << pipe))) continue;
        const uint32_t o = 0x1000 * pipe;
        uint32_t ctl {}, stride {}, size {}, surface {};
        if (!AlderLakeN::value(display, 0x70180 + o, ctl) || !AlderLakeN::value(display, 0x70188 + o, stride) ||
            !AlderLakeN::value(display, 0x70190 + o, size) || !AlderLakeN::value(display, 0x7019C + o, surface))
            return false;
        if (!(ctl & 0x80000000)) continue;
        if (ctl & (7U << 10)) return false;
        const uint64_t last = uint64_t(surface & ~4095U) + uint64_t(stride & 0xFFF) * 64 * (((size >> 16) & 0x1FFF) + 1);
        if (last > end) end = last;
    }
    return true;
}

// The BCS self-test's sixteen pages, leased at 32MiB: inside the measured part's
// 62MiB usable stolen memory, clear of an FHD firmware plane. Like AlderLakeN's
// firmware lease this is not an allocator: the whole stolen range must carry
// the firmware's identity mapping, and every leased PTE is saved for restore.
constexpr uint32_t GPUSlot = 0x02000000;
struct GPULease {
    uint64_t version {1}, status {}, slot {GPUSlot}, bytes {AlderLakeN::GPUBytes};
    uint64_t stolenBase {}, verifiedPages {}, gopEnd {}, first {}, last {};
};
static_assert(sizeof(GPULease) == 9 * 8, "RaptorLakeP GPU lease layout");

// status: 1 leased, 2 a display or memory precondition failed, 3 the GGTT is
// not the identity stolen mapping (verifiedPages says where it stopped).
template<class IO> GPULease leaseGPU(IO &io, const AlderLakeN::DisplayReadout &display,
                                     uint64_t *predecessor, bool admitted, const XELPMemory::Layout &memory) {
    GPULease out;
    if (!admitted) return out;
    out.status = 2;
    if (!predecessor || display.status != AlderLakeN::ReadoutStatus::Complete ||
        !scanoutEnd(display, out.gopEnd) || out.gopEnd > GPUSlot ||
        !memory.range(GPUSlot, AlderLakeN::GPUBytes, true) || !io.readable(0x800000 + (memory.bytes / 4096) * 8 - 1))
        return out;
    out.stolenBase = memory.base;
    out.status = 3;
    for (uint32_t i = 0; i < memory.bytes / 4096; ++i) {
        const auto expected = memory.pte(uint64_t(i) * 4096);
        if (io.readPTE(0x800000 + 8 * i) != expected) return out;
        ++out.verifiedPages;
        if (i >= GPUSlot / 4096 && i < (GPUSlot + AlderLakeN::GPUBytes) / 4096)
            predecessor[i - GPUSlot / 4096] = expected;
    }
    out.first = predecessor[0];
    out.last = predecessor[AlderLakeN::GPUPages - 1];
    out.status = 1;
    return out;
}

enum class EDPStatus : uint32_t { NotAdmitted, Complete, Unavailable, PowerChanged };
struct EDPReadout {
    uint32_t version {1};
    EDPStatus status {EDPStatus::NotAdmitted};
    uint32_t count {}, dpPipes {}, ddiAPipes {};
    AlderLakeN::Register registers[64] {};
};

// Read only, after AlderLakeN::readDisplay. South PPS/backlight and the PW0/PW1
// clock, DC-state and AUX registers are always present. Transcoder DP/PSR and
// pipe scaler registers are read only for DP SST pipes on combo A/B that the
// display readout found powered and active. Linux intel_pps_regs.h,
// intel_backlight_regs.h, intel_psr_regs.h, intel_dp_aux_regs.h and
// intel_display_regs.h; scalers use a 0x800 pipe stride. No AUX transaction,
// panel power, backlight, PSR or link write.
template<class IO> EDPReadout readEDP(IO &io, const AlderLakeN::DisplayReadout &display) {
    EDPReadout r;
    if (display.status==AlderLakeN::ReadoutStatus::NotAdmitted) return r;
    r.status = EDPStatus::Unavailable;
    uint32_t power {};
    if (display.status!=AlderLakeN::ReadoutStatus::Complete || !AlderLakeN::value(display,0x45404,power))
        return r;
    const auto add = [&](uint32_t offset) {
        if (r.count >= sizeof(r.registers) / sizeof(r.registers[0]) || !io.readable(offset)) {
            r.status = EDPStatus::Unavailable;
            return UINT32_MAX;
        }
        const uint32_t value = io.read(offset);
        r.registers[r.count++] = {offset, value};
        if (value == UINT32_MAX) r.status = EDPStatus::Unavailable;
        return value;
    };
    r.status = EDPStatus::Complete;
    // PCH PPS 0/1, BXT_BLC_PWM 1/2, DC_STATE_EN, PWR_WELL_CTL_AUX1/2,
    // DP_AUX_CH_CTL A/B, DPLL0/1_ENABLE and TGL_DPLL1_CFGCR1.
    const uint32_t global[] {0xC7200, 0xC7204, 0xC7208, 0xC720C, 0xC7300, 0xC7304, 0xC7308, 0xC730C,
        0xC8250, 0xC8254, 0xC8258, 0xC8350, 0xC8354, 0xC8358, 0x45504, 0x45440, 0x45444,
        0x64010, 0x64110, 0x46010, 0x46014, 0x164290};
    for (auto offset : global) add(offset);
    for (unsigned pipe = 0; pipe < 3; ++pipe) {
        uint32_t func {};
        if (!(display.activePipes & (1U << pipe)) || !AlderLakeN::value(display, 0x60400 + 0x1000 * pipe, func))
            continue;
        // TGL+ encodes port + 1 in bits30:27; mode 2 is DP SST.
        const auto port = (func >> 27) & 15;
        if (((func >> 24) & 7) != 2 || (port != 1 && port != 2)) continue;
        r.dpPipes |= 1U << pipe;
        if (port == 1) r.ddiAPipes |= 1U << pipe;
        // DP_TP_CTL/STATUS, DATA_M1/N1, LINK_M1/N1, MSA_MISC, PSR and PSR2 CTL/STATUS.
        const uint32_t transcoder[] {0x60540, 0x60544, 0x60030, 0x60034, 0x60040, 0x60044,
            0x60410, 0x60800, 0x60840, 0x60900, 0x60940};
        for (auto offset : transcoder) add(offset + 0x1000 * pipe);
        add(0x68180 + 0x800 * pipe); add(0x68280 + 0x800 * pipe);
    }
    const auto after = add(0x45404);
    if (r.status == EDPStatus::Complete && after != power) r.status = EDPStatus::PowerChanged;
    return r;
}

// The accelerator's device user client answers selector 0xB with a 0x7C-byte hardware-caps record copied
// from the accelerator object (revision at +0, packed vendor/device at +8, resources from +0x10). The
// prepatched user-mode GL and VA drivers admit a device by that packed identity and list only Tiger Lake
// and Alder Lake-N: on A7A8 OpenGL aborts in its compiler constructor and the video driver returns an
// error. The returned copy, and only it, presents 9A49 (the Tiger Lake GT2 those drivers already route
// Alder Lake-N to): the device halfword at +0xA changes, vendor 8086 and every other byte stay as read.
// The stored caps, selector 0x18 (Metal) and the IORegistry identity are untouched.
namespace CapsReply {
enum class Status : uint32_t {Untouched, Rewritten, Disabled, Error, BadSize, NotA7A8};
constexpr uint64_t Size = 0x7C;
constexpr uint32_t Physical = 0xA7A88086, Presented = 0x9A498086;
inline uint32_t identity(const uint8_t *reply) {
    return reply[8] | uint32_t(reply[9]) << 8 | uint32_t(reply[10]) << 16 | uint32_t(reply[11]) << 24;
}
// result is the donor method's return; size the reply length it reported into a buffer that had room for it.
inline Status adapt(uint8_t *reply, uint64_t size, int result, bool enabled) {
    if (!enabled) return Status::Disabled;
    if (result != 0) return Status::Error;
    if (!reply || size != Size) return Status::BadSize;
    if (identity(reply) != Physical) return Status::NotA7A8;
    reply[10] = uint8_t(Presented >> 16); reply[11] = uint8_t(Presented >> 24);
    return Status::Rewritten;
}
}
}
#endif
