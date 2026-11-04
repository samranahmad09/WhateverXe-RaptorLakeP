#include "kern_rplp_coldphy.hpp"
#include <assert.h>
#include <map>
#include <stdio.h>
#include <vector>
using namespace RPLPColdPHY;
// Register model of a combo PHY pair. Lane 0 reads reflect group writes, as the hardware broadcasts them.
struct IO {
    std::map<uint32_t,uint32_t> r;
    std::vector<std::pair<uint32_t,uint32_t>> writes;
    uint32_t allones {~0U};
    IO() {
        for(unsigned phy=0;phy<Count;++phy) {
            r[Misc[phy]]=0;r[compDW(phy,0)]=0;r[compDW(phy,1)]=0;r[compDW(phy,3)]=0;r[compDW(phy,8)]=0;r[compDW(phy,9)]=0;r[compDW(phy,10)]=0;
            r[clDW(phy,5)]=0;r[clDW(phy,10)]=0;r[clDW(phy,12)]=0;r[txDW8Lane0(phy)]=0;r[txDW8Group(phy)]=0;r[pcsDW1Lane0(phy)]=0;r[pcsDW1Group(phy)]=0;
        }
    }
    // A PHY as the firmware leaves it after boot: 0.95 V dot0 silicon, everything programmed.
    void programmed(unsigned phy) {
        r[Misc[phy]]=0;r[compDW(phy,0)]=CompInit|0x1234;r[compDW(phy,3)]=(1U<<24);r[compDW(phy,1)]=0x0000AA00;
        r[compDW(phy,9)]=0x86E172C7;r[compDW(phy,10)]=0x77CA5EAB;r[compDW(phy,8)]=phy==0 ? IRefGen : 0;
        r[clDW(phy,5)]=CLPowerDownEnable|2;r[txDW8Lane0(phy)]=TX8ODCCClkSel|TX8ODCCClkDiv2|0x5;r[pcsDW1Lane0(phy)]=0x100;
    }
    // The same PHY after a power loss: compensation powered down, init clear, the reference words gone, the lane
    // bits at their reset values. The silicon information survives.
    void lost(unsigned phy) {
        programmed(phy);
        r[Misc[phy]]=MiscDEIOCompPowerDown;r[compDW(phy,0)]=0x1234;r[compDW(phy,1)]=0x0000AAFF;r[compDW(phy,9)]=r[compDW(phy,10)]=0;
        r[compDW(phy,8)]=0;r[clDW(phy,5)]=2;r[txDW8Lane0(phy)]=0x5|(2U<<29);r[pcsDW1Lane0(phy)]=0x100|(1U<<20);
    }
    uint32_t read(uint32_t reg) {assert(r.count(reg));return reg==allones ? UINT32_MAX : r[reg];}
    void write(uint32_t reg,uint32_t value) {
        assert(r.count(reg));writes.emplace_back(reg,value);r[reg]=value;
        for(unsigned phy=0;phy<Count;++phy) {
            if(reg==txDW8Group(phy))r[txDW8Lane0(phy)]=value;
            if(reg==pcsDW1Group(phy))r[pcsDW1Lane0(phy)]=value;
        }
    }
};
int main() {
    // Every procmon pair of i915's table decodes; the sixth combination does not.
    Procmon p;
    assert(procmon(0,p) && p.dw9==0x62AB67BB);
    assert(procmon(1U<<24,p) && p.dw9==0x86E172C7 && !p.dw1);
    assert(procmon((1U<<24)|(1U<<26),p) && p.dw10==0x8AE871C5);
    assert(procmon(2U<<24,p) && p.dw9==0x98FA82DD);
    assert(procmon((2U<<24)|(1U<<26),p) && p.dw1==0x00440000);
    assert(!procmon(3U<<24,p) && !procmon(2U<<26,p));
    // A programmed PHY verifies clean and is left alone.
    {IO io;io.programmed(0);io.programmed(1);uint64_t r[Words];
     for(unsigned phy=0;phy<Count;++phy) {
         assert(verify(io,phy)==0);
         assert(init(io,phy,r)==Status::Verified && io.writes.empty() && r[0]==0 && !r[7] && !r[8] && r[9]==0 && r[1]==(1U<<24));
     }}
    // A PHY that lost its state is re-initialised in i915's order and verifies afterwards; the master gets IREFGEN.
    for(unsigned phy=0;phy<Count;++phy) {
        IO io;io.lost(phy);uint64_t r[Words];
        const auto before=verify(io,phy);
        assert(before==(1|2|4|8|16|32|(phy==0 ? 64U : 0U)|128));
        assert(init(io,phy,r)==Status::Initialised && r[0]==before && r[7]==1 && r[9]==0 && verify(io,phy)==0);
        const auto &w=io.writes;
        const unsigned expected=phy==0 ? 9 : 8;
        assert(w.size()==expected && r[8]==expected);
        assert(w[0].first==Misc[phy] && !(w[0].second&MiscDEIOCompPowerDown));
        assert(w[1].first==txDW8Group(phy) && (w[1].second&(TX8ODCCClkSel|TX8ODCCClkDivMask))==(TX8ODCCClkSel|TX8ODCCClkDiv2) && (w[1].second&0x5)==0x5);
        assert(w[2].first==pcsDW1Group(phy) && !(w[2].second&DCCModeSelectMask) && (w[2].second&0x100));
        assert(w[3].first==compDW(phy,1) && w[3].second==0x0000AA00); // Bits outside the procmon bytes kept, the bytes replaced.
        assert(w[4].first==compDW(phy,9) && w[4].second==0x86E172C7);
        assert(w[5].first==compDW(phy,10) && w[5].second==0x77CA5EAB);
        unsigned i=6;
        if(phy==0) {assert(w[i].first==compDW(phy,8) && (w[i].second&IRefGen));++i;}
        assert(w[i].first==compDW(phy,0) && w[i].second==(CompInit|0x1234));++i;
        assert(w[i].first==clDW(phy,5) && w[i].second==(CLPowerDownEnable|2));
        assert(r[10]==(CompInit|0x1234) && r[11]==(CLPowerDownEnable|2));
        // The other PHY was not touched.
        for(auto &e:w)assert(e.first/0x1000!=Base[phy ^ 1]/0x1000 || e.first==Misc[phy]);
    }
    // One lost check is enough to re-run the whole sequence; a PHY whose only loss is the power-down bit is re-done too.
    {IO io;io.programmed(0);io.r[Misc[0]]=MiscDEIOCompPowerDown;uint64_t r[Words];
     assert(verify(io,0)==1 && init(io,0,r)==Status::Initialised && io.writes.size()==9 && verify(io,0)==0);}
    // Nothing is written when a register reads all-ones or the silicon information is unknown.
    {IO io;io.lost(0);io.allones=compDW(0,9);uint64_t r[Words];
     assert(init(io,0,r)==Status::Unreadable && io.writes.empty() && (r[0]&Unreadable));}
    {IO io;io.lost(0);io.r[compDW(0,3)]=3U<<24;uint64_t r[Words];
     assert(init(io,0,r)==Status::UnknownProcmon && io.writes.empty() && (r[0]&UnknownProcmon));}
    // A write that does not take (a stuck init bit) is reported as a mismatch, with the writes still counted.
    {struct Stuck : IO {void write(uint32_t reg,uint32_t value) {IO::write(reg,reg==compDW(0,0) ? (value&~CompInit) : value);}} io;
     io.lost(0);uint64_t r[Words];
     assert(init(io,0,r)==Status::Mismatch && r[7]==1 && r[8]==9 && (r[9]&1) && !(r[9]&~1U));}
    // With a boot reference the firmware's procmon words replace the table and the common-lane bits it had set come
    // back; a reference without COMP_INIT or with an all-ones word is ignored; bits already right are not written.
    {IO io;io.programmed(0);io.r[compDW(0,9)]=0x11112222;io.r[compDW(0,10)]=0x33334444;io.r[compDW(0,1)]=0x00AA00BB;
     io.r[clDW(0,5)]=CLPowerDownEnable|3|0x40;io.r[clDW(0,10)]=0x30000;io.r[clDW(0,12)]=0x90;
     uint64_t boot[BootWords];snapshot(io,0,boot);
     assert(bootUsable(boot) && boot[5]==0x11112222 && boot[7]==(CLPowerDownEnable|3|0x40) && boot[8]==0x30000 && boot[9]==0x90);
     IO cold;cold.lost(0);cold.r[clDW(0,5)]=0x40;cold.r[clDW(0,10)]=0x3000a;cold.r[clDW(0,12)]=0xc0;
     uint64_t r[Words];
     assert(init(cold,0,r,boot)==Status::Initialised && r[12]==1 && r[13]==(1|2|4|8|16|32) && r[9]==0 && r[8]==11);
     assert(cold.r[compDW(0,9)]==0x11112222 && cold.r[compDW(0,10)]==0x33334444 && (cold.r[compDW(0,1)]&ProcmonDW1Mask)==0x00AA00BB);
     assert((cold.r[clDW(0,5)]&3)==3 && (cold.r[clDW(0,5)]&CLPowerDownEnable) && cold.r[clDW(0,10)]==0x30000 && cold.r[clDW(0,12)]==0x80);
     assert(r[14]==0x80 && r[15]==0x30000 && verify(cold,0)==(8|16|32));
     uint64_t bad[BootWords];for(unsigned i=0;i<BootWords;++i)bad[i]=boot[i];bad[1]&=~CompInit;
     IO again;again.lost(0);uint64_t r2[Words];
     assert(init(again,0,r2,bad)==Status::Initialised && r2[12]==0 && r2[13]==0 && r2[8]==9 && verify(again,0)==0);
     for(unsigned i=0;i<BootWords;++i)bad[i]=boot[i];bad[5]=UINT32_MAX;
     IO third;third.lost(0);
     assert(init(third,0,r2,bad)==Status::Initialised && r2[12]==0 && verify(third,0)==0);
     IO same;same.lost(0);same.r[clDW(0,5)]=3;same.r[clDW(0,10)]=0x30000;same.r[clDW(0,12)]=0x90;
     assert(init(same,0,r2,boot)==Status::Initialised && r2[13]==(1|2|4) && r2[8]==9 && same.r[clDW(0,12)]==0x90);
     IO fine;fine.programmed(0);fine.r[compDW(0,9)]=0x11112222;fine.r[compDW(0,10)]=0x33334444;fine.r[compDW(0,1)]=0x00AA00BB;
     fine.r[clDW(0,5)]=CLPowerDownEnable|3|0x40;fine.r[clDW(0,10)]=0x30000;fine.r[clDW(0,12)]=0x90;
     assert(verify(fine,0)==(8|16|32) && init(fine,0,r2,boot)==Status::Initialised && r2[13]==(1|2|4) && verify(fine,0)==(8|16|32));
    }
    // Register addresses are the i915 ones.
    assert(compDW(0,3)==0x16210C && compDW(1,9)==0x6C124 && clDW(0,5)==0x162014 && txDW8Lane0(0)==0x1628A0 &&
           txDW8Group(1)==0x6C6A0 && pcsDW1Lane0(0)==0x162804 && pcsDW1Group(0)==0x162604 && Misc[1]==0x64C04);
    puts("rplp_coldphy: ok");
    return 0;
}
