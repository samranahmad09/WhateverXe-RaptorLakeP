// Raptor Lake-P combo PHY initialisation after a real power loss, as Linux i915 performs it before it requests the
// first display power well (intel_combo_phy.c, icl_combo_phys_init, display version 13): verify each combo PHY's
// state first and program only a PHY that lost it. Alder Lake-P/Raptor Lake-P have two combo PHYs, A (the internal
// panel's DDI A) and B; the Type-C ports use other PHYs. Nothing is written when every check holds, when a register
// reads all-ones, or when the PHY's process/voltage information is not one of the five known pairs.
#ifndef KERN_RPLP_COLDPHY_HPP
#define KERN_RPLP_COLDPHY_HPP
#include <stdint.h>
namespace RPLPColdPHY {
constexpr unsigned Count=2;
constexpr uint32_t Base[Count] {0x162000,0x6C000};   // _ICL_COMBOPHY_A, _ICL_COMBOPHY_B
constexpr uint32_t Misc[Count] {0x64C00,0x64C04};    // ICL_PHY_MISC(A), ICL_PHY_MISC(B)
constexpr uint32_t MiscDEIOCompPowerDown=1U<<23,CompInit=1U<<31,IRefGen=1U<<24,CLPowerDownEnable=1U<<4;
constexpr uint32_t TX8ODCCClkSel=1U<<31,TX8ODCCClkDivMask=3U<<29,TX8ODCCClkDiv2=1U<<29,DCCModeSelectMask=3U<<20;
constexpr uint32_t ProcmonDW1Mask=(0xFFU<<16)|0xFFU;
inline uint32_t compDW(unsigned phy,unsigned dw) {return Base[phy]+0x100+4*dw;}
inline uint32_t clDW(unsigned phy,unsigned dw) {return Base[phy]+4*dw;}
inline uint32_t pcsDW1Lane0(unsigned phy) {return Base[phy]+0x800+4;}
inline uint32_t pcsDW1Group(unsigned phy) {return Base[phy]+0x600+4;}
inline uint32_t txDW8Lane0(unsigned phy) {return Base[phy]+0x880+4*8;}
inline uint32_t txDW8Group(unsigned phy) {return Base[phy]+0x680+4*8;}
struct Procmon {uint32_t dw1,dw9,dw10;};
// i915 icl_procmon_values, keyed by COMP_DW3's process information (bits 28:26) and voltage information (bits 25:24).
inline bool procmon(uint32_t dw3,Procmon &p) {
    const uint32_t process=(dw3>>26)&7,voltage=(dw3>>24)&3;
    if(voltage==0 && process==0) {p={0x00000000,0x62AB67BB,0x51914F96};return true;}
    if(voltage==1 && process==0) {p={0x00000000,0x86E172C7,0x77CA5EAB};return true;}
    if(voltage==1 && process==1) {p={0x00000000,0x93F87FE1,0x8AE871C5};return true;}
    if(voltage==2 && process==0) {p={0x00000000,0x98FA82DD,0x89E46DC1};return true;}
    if(voltage==2 && process==1) {p={0x00440000,0x9A00AB25,0x8AE38FF1};return true;}
    return false;
}
// Verification bits (zero means every check held): 1 enabled (PHY_MISC's compensation power-down bit clear and
// COMP_DW0's init bit set), 2 TX_DW8 ODCC clock select and divider, 4 PCS_DW1 DCC mode, 8/16/32 the three procmon
// words, 64 IREFGEN on the master PHY (A), 128 CL_DW5 power-down enable, 256 a register read all-ones, 512 unknown
// process/voltage information, 1024 a boot word written back did not read back (init with a boot reference only).
constexpr uint32_t Unreadable=256,UnknownProcmon=512,Unverified=1024;
enum class Status : uint32_t {Verified,Initialised,Unreadable,UnknownProcmon,Mismatch};
// Per-PHY record, seventeen words: 0 checks before, 1 COMP_DW3, 2 PHY_MISC before, 3 TX_DW8 lane 0 before, 4 PCS_DW1
// lane 0 before, 5 COMP_DW0 before, 6 CL_DW5 before, 7 applied, 8 writes, 9 checks after, 10 COMP_DW0 after,
// 11 CL_DW5 after, 12 boot reference used, 13 the boot words written (bit 0 COMP_DW1, 1 COMP_DW9, 2 COMP_DW10,
// 3 CL_DW5 bits 1:0, 4 CL_DW10 bits 3:0), 14 CL_DW12 after, 15 CL_DW10 after, 16 the boot words that read back as
// the firmware had them afterwards (same bits; a written word that is not in it did not take).
constexpr unsigned Words=17;
// The PHY's words as the firmware left them, twelve per PHY (N97FBPHYFirmware): PHY_MISC, COMP_DW0, COMP_DW1,
// COMP_DW3, COMP_DW8, COMP_DW9, COMP_DW10, CL_DW5, CL_DW10, CL_DW12, TX_DW8 lane 0, PCS_DW1 lane 0.
constexpr unsigned BootWords=12;
template<class IO> void snapshot(IO &io,unsigned phy,uint64_t *w) {
    w[0]=io.read(Misc[phy]);w[1]=io.read(compDW(phy,0));w[2]=io.read(compDW(phy,1));w[3]=io.read(compDW(phy,3));
    w[4]=io.read(compDW(phy,8));w[5]=io.read(compDW(phy,9));w[6]=io.read(compDW(phy,10));w[7]=io.read(clDW(phy,5));
    w[8]=io.read(clDW(phy,10));w[9]=io.read(clDW(phy,12));w[10]=io.read(txDW8Lane0(phy));w[11]=io.read(pcsDW1Lane0(phy));
}
// A boot reference is usable when every word reads as a register (no all-ones) and the firmware had initialised the
// PHY (COMP_INIT set, the compensation not powered down).
inline bool bootUsable(const uint64_t *boot) {
    for(unsigned i=0;i<BootWords;++i)if(boot[i]>=UINT32_MAX)return false;
    return (boot[1]&CompInit) && !(boot[0]&MiscDEIOCompPowerDown);
}
template<class IO> uint32_t verify(IO &io,unsigned phy,uint64_t *r=nullptr) {
    const uint32_t misc=io.read(Misc[phy]),dw0=io.read(compDW(phy,0)),tx8=io.read(txDW8Lane0(phy)),pcs1=io.read(pcsDW1Lane0(phy)),
        dw1=io.read(compDW(phy,1)),dw3=io.read(compDW(phy,3)),dw8=io.read(compDW(phy,8)),dw9=io.read(compDW(phy,9)),dw10=io.read(compDW(phy,10)),
        cl5=io.read(clDW(phy,5));
    if(r) {r[1]=dw3;r[2]=misc;r[3]=tx8;r[4]=pcs1;r[5]=dw0;r[6]=cl5;}
    const uint32_t reads[] {misc,dw0,tx8,pcs1,dw1,dw3,dw8,dw9,dw10,cl5};
    for(auto v:reads)if(v==UINT32_MAX)return Unreadable;
    Procmon p {};
    if(!procmon(dw3,p))return UnknownProcmon;
    uint32_t checks=0;
    if((misc&MiscDEIOCompPowerDown) || !(dw0&CompInit))checks|=1;
    if((tx8&(TX8ODCCClkSel|TX8ODCCClkDivMask))!=(TX8ODCCClkSel|TX8ODCCClkDiv2))checks|=2;
    if(pcs1&DCCModeSelectMask)checks|=4;
    if((dw1&ProcmonDW1Mask)!=p.dw1)checks|=8;
    if(dw9!=p.dw9)checks|=16;
    if(dw10!=p.dw10)checks|=32;
    if(phy==0 && !(dw8&IRefGen))checks|=64;
    if(!(cl5&CLPowerDownEnable))checks|=128;
    return checks;
}
// i915's sequence for one PHY: PHY_MISC compensation power-down cleared, ODCC clock select and divider on every lane,
// DCC run once on every lane, the procmon reference values, IREFGEN on the master, COMP_INIT, CL power-down enable.
// With a usable boot reference (`boot`, BootWords, opt-in), the procmon words come from the firmware's own values
// instead of i915's table, and after the sequence the common-lane bits the firmware had set and a power loss clears
// are written back where they differ: CL_DW5 bits 1:0 (i915 sets them in its DP enable: "SUS Clock Config 11b") and
// CL_DW10 bits 3:0. CL_DW12 is recorded only: bit 0 is the AUX lane enable the native path toggles, bit 4 follows the
// AUX well request, and its upper bits read as status (round 50: written back, they kept the power-loss value, then
// returned to the boot value by themselves while the AUX well was up). Round 49: with the i915 sequence alone the
// AUX A IO well never reported its state bit after the power loss while the DDI A IO well did.
constexpr uint32_t SusClockConfigMask=3U,CLDW10LowMask=0xFU;
template<class IO> Status init(IO &io,unsigned phy,uint64_t *r,const uint64_t *boot=nullptr) {
    for(unsigned i=0;i<Words;++i)r[i]=0;
    const auto checks=verify(io,phy,r);r[0]=checks;
    if(checks&Unreadable)return Status::Unreadable;
    if(checks&UnknownProcmon)return Status::UnknownProcmon;
    const bool useBoot=boot && bootUsable(boot);r[12]=useBoot;
    if(!checks) {r[9]=0;r[10]=io.read(compDW(phy,0));r[11]=io.read(clDW(phy,5));r[14]=io.read(clDW(phy,12));r[15]=io.read(clDW(phy,10));return Status::Verified;}
    Procmon p {};procmon(uint32_t(r[1]),p);
    if(useBoot) {p.dw1=uint32_t(boot[2])&ProcmonDW1Mask;p.dw9=uint32_t(boot[5]);p.dw10=uint32_t(boot[6]);}
    uint32_t writes=0,recreated=0;
    auto write=[&](uint32_t reg,uint32_t v) {io.write(reg,v);++writes;};
    const uint32_t misc=io.read(Misc[phy]);
    if(misc&MiscDEIOCompPowerDown)write(Misc[phy],misc&~MiscDEIOCompPowerDown);
    uint32_t v=io.read(txDW8Lane0(phy));v&=~TX8ODCCClkDivMask;v|=TX8ODCCClkSel|TX8ODCCClkDiv2;write(txDW8Group(phy),v);
    v=io.read(pcsDW1Lane0(phy));v&=~DCCModeSelectMask;write(pcsDW1Group(phy),v);
    v=io.read(compDW(phy,1));write(compDW(phy,1),(v&~ProcmonDW1Mask)|p.dw1);
    write(compDW(phy,9),p.dw9);write(compDW(phy,10),p.dw10);
    if(useBoot)recreated|=1|2|4;
    if(phy==0) {v=io.read(compDW(phy,8));write(compDW(phy,8),v|IRefGen);}
    v=io.read(compDW(phy,0));write(compDW(phy,0),v|CompInit);
    v=io.read(clDW(phy,5));v|=CLPowerDownEnable;
    if(useBoot && (v&SusClockConfigMask)!=(uint32_t(boot[7])&SusClockConfigMask)) {v=(v&~SusClockConfigMask)|(uint32_t(boot[7])&SusClockConfigMask);recreated|=8;}
    write(clDW(phy,5),v);
    if(useBoot) {
        v=io.read(clDW(phy,10));
        if((v&CLDW10LowMask)!=(uint32_t(boot[8])&CLDW10LowMask)) {write(clDW(phy,10),(v&~CLDW10LowMask)|(uint32_t(boot[8])&CLDW10LowMask));recreated|=16;}
    }
    r[7]=1;r[8]=writes;r[13]=recreated;
    auto after=verify(io,phy);
    uint32_t verified=0;
    if(useBoot) {
        // The table's procmon checks do not apply to the firmware's values; the firmware's own words must read back.
        after&=~(8U|16U|32U);
        if((io.read(compDW(phy,1))&ProcmonDW1Mask)!=p.dw1)after|=8;else verified|=1;
        if(io.read(compDW(phy,9))!=p.dw9)after|=16;else verified|=2;
        if(io.read(compDW(phy,10))!=p.dw10)after|=32;else verified|=4;
        if((io.read(clDW(phy,5))&SusClockConfigMask)==(uint32_t(boot[7])&SusClockConfigMask))verified|=8;
        if((io.read(clDW(phy,10))&CLDW10LowMask)==(uint32_t(boot[8])&CLDW10LowMask))verified|=16;
        if(recreated&~verified)after|=Unverified;
    }
    r[9]=after;r[10]=io.read(compDW(phy,0));r[11]=io.read(clDW(phy,5));r[14]=io.read(clDW(phy,12));r[15]=io.read(clDW(phy,10));r[16]=verified;
    return after ? Status::Mismatch : Status::Initialised;
}
}
#endif
