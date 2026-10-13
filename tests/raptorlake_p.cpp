// Host fixtures for the read-only RPL-P GT profile and eDP evidence.
#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>
#include "kern_raptorlake_p.hpp"
struct IO {
    std::map<uint32_t,uint32_t> regs;
    std::vector<uint32_t> reads;
    uint32_t limit=0x1000000;
    unsigned powerReads=0, changePowerAt=0;
    bool readable(uint32_t offset) const { return offset <= limit-4; }
    uint32_t read(uint32_t offset) {
        // No TGL FIA or Dekel register may enter this profile.
        assert(offset < 0x163000 || offset >= 0x164000);
        reads.push_back(offset);
        if(offset==0x45404 && ++powerReads==changePowerAt)return regs[offset]^0x400;
        return regs[offset];
    }
    bool seen(uint32_t offset) const {
        for(auto r:reads)if(r==offset)return true;
        return false;
    }
};
struct GTIO : IO {
    std::vector<AlderLakeN::Register> writes;
    bool failGT=false,failReleaseGT=false;
    uint32_t read(uint32_t offset) {
        if(offset!=0xA188 && offset!=0xA278 && offset!=0x130044 && offset!=0xD84)
            assert((regs[0x130044]&1) && (regs[0xD84]&1));
        return IO::read(offset);
    }
    void write(uint32_t offset,uint32_t value) {
        assert(offset==0xA188 || offset==0xA278);writes.push_back({offset,value});
        const uint32_t mask=value>>16;
        regs[offset]=((regs[offset]&0xFFFF)&~mask)|(value&mask)|(mask<<16);
        const auto ack=offset==0xA188 ? 0x130044 : 0xD84;
        if((value&1) && !(offset==0xA188 && failGT))regs[ack]|=1;
        if(!(value&1) && !(offset==0xA188 && failReleaseGT))regs[ack]&=~1U;
    }
    void delay() {}
};
// 64-EU Xe-LP: four DSS, all EU pairs, two VDBOX and one VEBOX enabled.
static GTIO firmwareGT() {
    GTIO io;io.regs={{0x9138,1},{0x913c,0xf},{0x9134,0},{0x9118,0},{0x9140,0xe00fa}};
    return io;
}
// The measured A7A8 under OpenCore: GGC 0x02C1 (64MiB stolen, 8MiB GTT) at
// 0x4B800000, GEN6_STOLEN_RESERVED 0x4F600087 (2MiB WOPCM at the top).
static XELPMemory::Layout measuredStolen(uint16_t ggc=0x02c1) {
    auto memory=XELPMemory::decode(ggc,0x4b800001,0);
    XELPMemory::reservation(memory,memory.base+memory.bytes-0x200000+0x87);
    return memory;
}
// Firmware GGTT: every stolen page mapped 1:1, as the R1 readout implies.
struct PTEIO : IO {
    std::map<uint32_t,uint64_t> ptes;
    unsigned pteReads=0;
    uint64_t readPTE(uint32_t offset) {++pteReads;auto it=ptes.find(offset);return it==ptes.end() ? 0 : it->second;}
};
static PTEIO identityGGTT(const XELPMemory::Layout &memory) {
    PTEIO io;
    for(uint32_t i=0;i<memory.bytes/4096;++i)io.ptes[0x800000+8*i]=memory.pte(uint64_t(i)*4096);
    return io;
}
// The firmware plane the R1 readout recorded: pipe A, linear XRGB8888, FHD, stride 7680.
static AlderLakeN::DisplayReadout firmwarePlane(uint32_t ctl=0x94000008,uint32_t surface=0) {
    AlderLakeN::DisplayReadout d;d.status=AlderLakeN::ReadoutStatus::Complete;d.poweredPipes=d.activePipes=1;
    const AlderLakeN::Register regs[] {{0x45404,0xfc0f},{0x60400,0x8a000106},{0x70180,ctl},{0x70188,0x78},
        {0x70190,0x0437077f},{0x7019c,surface}};
    for(auto r:regs)d.registers[d.count++]=r;
    return d;
}
static IO eDP() {
    IO io;io.regs={{0x45404,0x401},{0x60400,0x8a000000},{0xc7200,0x80000008},
        {0xc8250,0x80000000},{0x64000,0x80000001}};
    return io;
}
int main() {
    using RaptorLakeP::EDPStatus;
    using AlderLakeN::GTStatus;
    unsigned catalogued=0;
    for(const auto &entry:XELP::DeviceTable) {
        const uint32_t pci=uint32_t(entry.device)<<16|0x8086;
        const bool rpl=entry.family==XELP::Family::RaptorLakeP;
        assert(RaptorLakeP::family(pci)==rpl);
        if(!rpl)continue;
        ++catalogued;
        // Only the measured A7A8 has a backend, admitted at revision 4 (GT C0 / display E0) alone.
        // The rest stay recognition only: no GT backend, donor, admission or probe ABI.
        const bool measured=entry.device==0xA7A8;
        for(uint32_t revision:{0U,3U,4U,5U,8U,12U}) {
            const auto p=XELP::identify(pci,revision);
            assert(p.display==XELP::Display::XeLPD && p.gt==(measured ? XELP::GT::RPLPMedium : XELP::GT::Unknown));
            assert(p.donor==(measured ? XELP::Donor::C004FC1A : XELP::Donor::Unknown));
            assert(p.admitted()==(measured && revision==4) && !AlderLakeN::matches(pci,revision));
            XELP::ProbeConfig c;c.profile=p;c.combo=c.comboBackend=1;assert(c.valid()==p.admitted());
        }
        assert(!RaptorLakeP::family((pci&0xFFFF0000U)|0x10DE));
    }
    assert(catalogued==8 && RaptorLakeP::family(0xA7A88086));
    assert(!RaptorLakeP::family(0x46D18086) && !RaptorLakeP::family(0xA7888086) && !RaptorLakeP::family(0x9A498086));
    auto step=RaptorLakeP::stepping(4);
    assert(!strcmp(step.gt,"C0") && !strcmp(step.display,"E0"));
    for(uint32_t revision:{0U,1U,3U,5U,8U,12U,255U}) {
        step=RaptorLakeP::stepping(revision);assert(!step.gt && !step.display);
    }
    std::puts("RaptorLakeP catalogue: A7A8 rev 4 admitted alone, others recognition only, Linux revision-4 steppings passed");

    RaptorLakeP::Resources res;
    assert(RaptorLakeP::resources(1,0xf,0,0,0xe00fa,res));
    assert(res.slices==1 && res.dss==4 && res.nativeSubslices==8 && res.eus==64);
    assert(res.l3BankPairs==0xf && res.vdbox==0x5 && res.vebox==0x1);
    unsigned profiles=0;
    for(unsigned dss=0;dss<0x80;++dss)for(unsigned disabled=0;disabled<0x100;++disabled) {
        const unsigned n=__builtin_popcount(dss);
        const bool expected=dss && !(dss&~0x3fU) && disabled!=0xff;
        assert(RaptorLakeP::resources(1,dss,disabled,0xc,0,res)==expected);
        if(!expected) {assert(!res.eus && !res.dss);continue;}
        ++profiles;
        assert(res.dss==n && res.nativeSubslices==2*n && res.eus==2*n*(8-__builtin_popcount(disabled)));
        assert(res.l3BankPairs==3 && res.vdbox==0xff && res.vebox==0xf);
    }
    assert(profiles==63*255);
    for(uint32_t slice:{0U,2U,3U,0x101U})assert(!RaptorLakeP::resources(slice,0xf,0,0,0xe00fa,res));
    assert(!RaptorLakeP::resources(1,0xf,0x100,0,0xe00fa,res) && !RaptorLakeP::resources(1,0xf,0,0xf,0xe00fa,res));
    assert(!RaptorLakeP::resources(1,0xf,0,0xff,0xe00fa,res) && RaptorLakeP::resources(1,0xf,0,0xf0,0xe00fa,res));
    assert(res.l3BankPairs==0xf);
    std::puts("RaptorLakeP SSEU, L3 bank-pair and media fuse decoding fixtures passed");

    // One disable bit per bank: ADL-N's 0xF0 keeps four, the measured 0x88 six, a full GT eight.
    assert(RaptorLakeP::l3Banks(0xf0)==4 && RaptorLakeP::l3Banks(0x88)==6 && RaptorLakeP::l3Banks(0)==8);
    assert(RaptorLakeP::l3Banks(0xff)==0 && RaptorLakeP::l3Banks(0xf00)==8 && RaptorLakeP::l3Banks(0x188)==6);
    AlderLakeN::Resources gtRes;
    assert(RaptorLakeP::qualifiedGT(1,0xf,0,0x88,0xe00fe,gtRes));
    assert(gtRes.slices==1 && gtRes.dss==4 && gtRes.nativeSubslices==8 && gtRes.eus==64);
    assert(gtRes.l3Banks==6 && gtRes.urbKiB==384);
    // Upper 0x9118 bits are not bank disables; the measured low byte is what qualifies.
    assert(RaptorLakeP::qualifiedGT(1,0xf,0,0x10088,0xe00fe,gtRes) && gtRes.l3Banks==6);
    unsigned qualified=0;
    for(unsigned dss=0;dss<0x80;++dss)for(unsigned disabled:{0U,1U,0x88U,0xfeU})
        for(unsigned l3:{0U,0x88U,0xf0U,0x8U})for(unsigned media:{0xe00feU,0xe00faU,0U}) {
            const bool expected=dss==0xf && !disabled && l3==0x88 && media==0xe00fe;
            assert(RaptorLakeP::qualifiedGT(1,dss,disabled,l3,media,gtRes)==expected);
            if(expected)++qualified;else assert(!gtRes.eus && !gtRes.l3Banks && !gtRes.urbKiB);
        }
    assert(qualified==1);
    for(uint32_t slice:{0U,2U})assert(!RaptorLakeP::qualifiedGT(slice,0xf,0,0x88,0xe00fe,gtRes));
    // The same four DSS in another position are a different, unmeasured part.
    assert(!RaptorLakeP::qualifiedGT(1,0x3c,0,0x88,0xe00fe,gtRes) && !RaptorLakeP::qualifiedGT(1,0x2d,0,0x88,0xe00fe,gtRes));
    std::puts("RaptorLakeP qualified GT profile: measured fuse set only, six L3 banks, 384KiB URB passed");

    // The self-test lease: 32MiB clears the 7.9MiB FHD plane and fits the 62MiB usable stolen.
    const auto stolen=measuredStolen();
    assert(stolen.valid && stolen.reservationKnown && stolen.bytes==0x4000000 && stolen.usable()==0x3e00000);
    uint64_t end=0;
    assert(RaptorLakeP::scanoutEnd(firmwarePlane(),end) && end==7680ULL*1080);
    uint64_t saved[AlderLakeN::GPUPages] {};
    auto ggtt=identityGGTT(stolen);
    auto lease=RaptorLakeP::leaseGPU(ggtt,firmwarePlane(),saved,true,stolen);
    assert(lease.status==1 && lease.slot==RaptorLakeP::GPUSlot && lease.bytes==AlderLakeN::GPUBytes);
    assert(lease.verifiedPages==stolen.bytes/4096 && lease.stolenBase==0x4b800000 && lease.gopEnd==7680ULL*1080);
    for(unsigned i=0;i<AlderLakeN::GPUPages;++i)assert(saved[i]==((0x4b800000ULL+RaptorLakeP::GPUSlot+i*4096)|1));
    assert(lease.first==saved[0] && lease.last==saved[AlderLakeN::GPUPages-1]);
    auto unadmitted=identityGGTT(stolen);uint64_t unused[AlderLakeN::GPUPages] {};
    assert(RaptorLakeP::leaseGPU(unadmitted,firmwarePlane(),unused,false,stolen).status==0 && !unadmitted.pteReads);
    // One foreign PTE anywhere in stolen memory refuses the lease and says where.
    auto foreign=identityGGTT(stolen);foreign.ptes[0x800000+8*100]=0x12345001;
    auto refused=RaptorLakeP::leaseGPU(foreign,firmwarePlane(),saved,true,stolen);
    assert(refused.status==3 && refused.verifiedPages==100);
    // Preconditions that fail before any PTE read.
    const AlderLakeN::DisplayReadout blocked[] {firmwarePlane(0x94000008,0x01ff0000),firmwarePlane(0x94001008),
        [] {auto d=firmwarePlane();d.status=AlderLakeN::ReadoutStatus::PowerChanged;return d;}(),
        [] {auto d=firmwarePlane();d.count=3;return d;}()};
    for(const auto &d:blocked) {
        auto io=identityGGTT(stolen);
        assert(RaptorLakeP::leaseGPU(io,d,saved,true,stolen).status==2 && !io.pteReads);
    }
    assert(RaptorLakeP::leaseGPU(ggtt,firmwarePlane(),nullptr,true,stolen).status==2);
    // A 32MiB stolen size leaves 30MiB usable: the slot does not fit.
    const auto smallStolen=measuredStolen(0x01c1);
    auto smallStolenIO=identityGGTT(smallStolen);
    assert(smallStolen.valid && smallStolen.usable()==0x1e00000 &&
           RaptorLakeP::leaseGPU(smallStolenIO,firmwarePlane(),saved,true,smallStolen).status==2);
    // A disabled plane, or one on an unpowered pipe, does not count.
    auto off=firmwarePlane(0x14000008,0x03000000);
    assert(RaptorLakeP::scanoutEnd(off,end) && !end);
    auto unpowered=firmwarePlane(0x94000008,0x03000000);unpowered.poweredPipes=0;
    assert(RaptorLakeP::scanoutEnd(unpowered,end) && !end);
    std::puts("RaptorLakeP BCS self-test lease: identity stolen GGTT, plane exclusion and fail-closed preconditions passed");

    auto gt=firmwareGT();gt.regs[0xA188]=8;gt.regs[0x130044]=8;
    auto ready=RaptorLakeP::readGT(gt,true);
    assert(ready.forcewake.status==GTStatus::Complete && ready.forcewake.writes==4);
    assert(ready.forcewake.requestedMask==3 && ready.forcewake.releasedMask==3 && !ready.forcewake.releaseFailedMask);
    assert(ready.resources.eus==64 && ready.resources.dss==4 && ready.resources.nativeSubslices==8);
    assert(ready.resources.l3BankPairs==0xf && ready.resources.vdbox==5 && ready.resources.vebox==1);
    assert(ready.forcewake.fuses[1]==0xf && ready.forcewake.fuses[4]==0xe00fa && ready.forcewake.count==16);
    // AlderLakeN's small-GT capacity is never published for this profile.
    assert(!ready.forcewake.resources.eus && !ready.forcewake.resources.l3Banks && !ready.forcewake.resources.urbKiB);
    assert((gt.regs[0xA188]&0xFFFF)==8 && !(gt.regs[0x130044]&1) && !(gt.regs[0xD84]&1));
    for(auto w:gt.writes)assert(w.value==0x00010001 || w.value==0x00010000);
    // A two-DSS part satisfying the ADL-N profile still yields RPL-P resources only.
    auto small=firmwareGT();small.regs[0x913c]=3;small.regs[0x9134]=0x88;small.regs[0x9118]=0xf0;small.regs[0x9140]=0xe00fe;
    auto smallReady=RaptorLakeP::readGT(small,true);
    assert(smallReady.forcewake.status==GTStatus::Complete && smallReady.resources.eus==24);
    assert(!smallReady.forcewake.resources.l3Banks && !smallReady.forcewake.resources.urbKiB);
    auto excludedGT=firmwareGT();
    assert(RaptorLakeP::readGT(excludedGT,false).forcewake.status==GTStatus::NotAdmitted && excludedGT.reads.empty());
    auto busyGT=firmwareGT();busyGT.regs[0xD84]=1;
    auto busy=RaptorLakeP::readGT(busyGT,true);
    assert(busy.forcewake.status==GTStatus::ExistingOwner && busyGT.writes.empty() && !busy.resources.eus);
    auto unavailableGT=firmwareGT();unavailableGT.regs[0x9140]=UINT32_MAX;
    auto unavailable=RaptorLakeP::readGT(unavailableGT,true);
    assert(unavailable.forcewake.status==GTStatus::Unavailable && !unavailable.resources.eus);
    assert(unavailable.forcewake.releasedMask==3);
    auto timedGT=firmwareGT();timedGT.failGT=true;
    auto timeout=RaptorLakeP::readGT(timedGT,true);
    assert(timeout.forcewake.status==GTStatus::AckTimeout && timeout.forcewake.releasedMask==3 && !timeout.resources.eus);
    auto wrongGT=firmwareGT();wrongGT.regs[0x9134]=0xff;
    auto wrong=RaptorLakeP::readGT(wrongGT,true);
    assert(wrong.forcewake.status==GTStatus::ProfileMismatch && !wrong.resources.eus && wrong.forcewake.releasedMask==3);
    auto stuckGT=firmwareGT();stuckGT.failReleaseGT=true;
    auto stuck=RaptorLakeP::readGT(stuckGT,true);
    assert(stuck.forcewake.status==GTStatus::ReleaseFailed && stuck.forcewake.releaseFailedMask==1 && !stuck.resources.eus);
    std::puts("RaptorLakeP GT forcewake ownership, release and profile re-evaluation fixtures passed");

    IO absent;
    const AlderLakeN::DisplayReadout none;
    assert(RaptorLakeP::readEDP(absent,none).status==EDPStatus::NotAdmitted && absent.reads.empty());
    auto panel=eDP();
    const auto boot=AlderLakeN::readDisplay(panel,true);
    assert(boot.status==AlderLakeN::ReadoutStatus::Complete && boot.poweredPipes==1 && boot.activePipes==1 && !boot.hdmiBPipes);
    AlderLakeN::LinearPlan plan;
    assert(AlderLakeN::linearPlan(boot,plan,0)==AlderLakeN::LinearStatus::NotSingleHDMI);
    panel.reads.clear();
    auto edp=RaptorLakeP::readEDP(panel,boot);
    assert(edp.status==EDPStatus::Complete && edp.dpPipes==1 && edp.ddiAPipes==1 && edp.count==36);
    assert(edp.registers[0].offset==0xc7200 && edp.registers[0].value==0x80000008);
    assert(edp.registers[edp.count-1].offset==0x45404);
    for(auto offset:{0x60540U,0x60544U,0x60030U,0x60410U,0x60800U,0x60940U,0x68180U,0x68280U,0xc8250U,0x45504U,0x164290U})
        assert(panel.seen(offset));
    for(auto offset:panel.reads)assert(offset<0x61000 || offset>=0x64000);
    IO hdmi;hdmi.regs={{0x45404,0x1001},{0x61400,0x90000000}};
    const auto hdmiBoot=AlderLakeN::readDisplay(hdmi,true);hdmi.reads.clear();
    auto noPanel=RaptorLakeP::readEDP(hdmi,hdmiBoot);
    assert(noPanel.status==EDPStatus::Complete && !noPanel.dpPipes && noPanel.count==23);
    assert(!hdmi.seen(0x61540) && !hdmi.seen(0x61800) && !hdmi.seen(0x68980) && !hdmi.seen(0x60540));
    IO dpB;dpB.regs={{0x45404,0x1001},{0x61400,0x92000000}};
    const auto dpBoot=AlderLakeN::readDisplay(dpB,true);dpB.reads.clear();
    auto external=RaptorLakeP::readEDP(dpB,dpBoot);
    assert(external.status==EDPStatus::Complete && external.dpPipes==2 && !external.ddiAPipes && external.count==36);
    assert(dpB.seen(0x61540) && dpB.seen(0x68980) && !dpB.seen(0x60540) && !dpB.seen(0x68180));
    // Type-C (TC1 encodes 4) and DP MST stay outside this observation.
    IO tc;tc.regs={{0x45404,0x4001},{0x62400,0xa2000000}};
    const auto tcBoot=AlderLakeN::readDisplay(tc,true);tc.reads.clear();
    assert(RaptorLakeP::readEDP(tc,tcBoot).dpPipes==0 && !tc.seen(0x62540));
    IO mst;mst.regs={{0x45404,0x401},{0x60400,0x8b000000}};
    const auto mstBoot=AlderLakeN::readDisplay(mst,true);
    assert(RaptorLakeP::readEDP(mst,mstBoot).dpPipes==0);
    auto moved=eDP();moved.changePowerAt=3;
    const auto movedBoot=AlderLakeN::readDisplay(moved,true);
    assert(movedBoot.status==AlderLakeN::ReadoutStatus::Complete);
    assert(RaptorLakeP::readEDP(moved,movedBoot).status==EDPStatus::PowerChanged);
    auto failed=eDP();failed.regs[0x45404]=UINT32_MAX;
    const auto failedBoot=AlderLakeN::readDisplay(failed,true);failed.reads.clear();
    assert(RaptorLakeP::readEDP(failed,failedBoot).status==EDPStatus::Unavailable && failed.reads.empty());
    auto truncated=eDP();truncated.limit=0x164290;
    const auto truncatedBoot=AlderLakeN::readDisplay(truncated,true);
    assert(truncatedBoot.status==AlderLakeN::ReadoutStatus::Complete);
    auto partial=RaptorLakeP::readEDP(truncated,truncatedBoot);
    assert(partial.status==EDPStatus::Unavailable && partial.dpPipes==1);
    auto allOnes=eDP();
    const auto onesBoot=AlderLakeN::readDisplay(allOnes,true);allOnes.regs[0xc8258]=UINT32_MAX;
    assert(RaptorLakeP::readEDP(allOnes,onesBoot).status==EDPStatus::Unavailable);
    std::puts("RaptorLakeP eDP readout, DP SST selection, power-change and bounded-read fixtures passed");
}
