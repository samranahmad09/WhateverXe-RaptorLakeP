#include "kern_rplp_port.hpp"
#include <assert.h>
#include <map>
#include <stdio.h>
#include <vector>
using namespace RPLPPort;
// Register model of the port's state. The PLL's power state follows its power enable, the lock follows its enable,
// as the hardware does within the specified times; `lazyPower`/`neverLock` model a PLL that never answers.
struct IO {
    std::map<uint32_t,uint32_t> r;
    std::vector<std::pair<uint32_t,uint32_t>> writes;
    uint32_t allones {~0U};
    bool neverLock {},neverPower {};
    IO() {for(auto reg:Regs)r[reg]=0;}
    // The firmware's boot state: delays programmed, DPLL0 running, port A clocked from it.
    void booted() {
        r[0xC7204]=0xABCD0060|PPOn;r[0xC7208]=0x07D00001;r[0xC720C]=0x01F40001;r[0xC7200]=0x80000008;
        r[0x46010]=PLLEnable|PLLLock|PLLPowerEnable|PLLPowerState;r[0x164284]=0x01C001A5;r[0x164288]=0x00000488;r[0x164B00]=0x50000000|(3U<<25);
        r[0x164280]=0x01000800; // DDI B (bit 11) and C (bit 24) clocks off, A on (bit 10 clear) and selecting DPLL0.
    }
    // The same state after a power loss: delays zero, PLL off, every port clock gated.
    void lost() {
        booted();
        r[0xC7204]=0xABCD0000;r[0xC7208]=0;r[0xC720C]=0;r[0xC7200]=0;
        r[0x46010]=0;r[0x164284]=0;r[0x164288]=0;r[0x164B00]=0x50000000;r[0x164280]=0x01000C00;
    }
    uint32_t read(uint32_t reg) {assert(r.count(reg));return reg==allones ? UINT32_MAX : r[reg];}
    void write(uint32_t reg,uint32_t value) {
        assert(r.count(reg));writes.emplace_back(reg,value);r[reg]=value;
        if(reg==0x46010 || reg==0x46014) {
            if((value&PLLPowerEnable) && !neverPower)r[reg]|=PLLPowerState;
            if((value&PLLEnable) && !neverLock)r[reg]|=PLLLock;
        }
    }
};
static unsigned delayed; // `wait` would collide with the C library's wait() that macOS's stdlib declares.
static void delay(unsigned us) {delayed+=us;}
int main() {
    {   // A snapshot reads every register in order and marks its schema.
        IO io;io.booted();uint64_t w[Words] {};
        snapshot(io,w);
        assert(w[PPControl]==(0xABCD0060|PPOn) && w[PLL0Enable]==io.r[0x46010] && w[ClockSelect]==io.r[0x164280] && w[Count]==1 && w[31]==0);
        for(unsigned i=0;i<Count;++i)assert(w[i]==io.r[Regs[i]]);
    }
    uint64_t boot[Words] {};
    {IO io;io.booted();snapshot(io,boot);}
    {   // PPS: delays lost -> rewritten from the reference, the cycle delay field only; the native path's bits untouched.
        IO io;io.lost();io.r[0xC7204]=0xABCD0000|PPForceVDD;uint64_t r[8] {};
        assert(restorePPS(io,boot,r)==Status::Applied);
        assert(io.writes.size()==3 && io.writes[0].first==0xC7208 && io.writes[0].second==0x07D00001 && io.writes[1].first==0xC720C &&
            io.writes[1].second==0x01F40001 && io.writes[2].first==0xC7204 && io.writes[2].second==(0xABCD0060|PPForceVDD));
        assert(r[0]==(0xABCD0000|PPForceVDD) && r[1]==0 && r[2]==0 && r[3]==(0xABCD0060|PPForceVDD) && r[4]==0x07D00001 && r[5]==0x01F40001 && r[6]==1);
    }
    {   // PPS: the delays survived (or the native path programmed them) -> nothing written.
        IO io;io.booted();io.r[0xC7204]=0xABCD0060;uint64_t r[8] {};
        assert(restorePPS(io,boot,r)==Status::Skipped && io.writes.empty());
        IO half;half.lost();half.r[0xC7208]=0x00010001;
        assert(restorePPS(half,boot,r)==Status::Skipped && half.writes.empty());
    }
    {   // PPS: an empty reference, or a register reading all-ones -> nothing written.
        IO io;io.lost();uint64_t r[8] {},empty[Words] {};
        assert(restorePPS(io,empty,r)==Status::NoReference && io.writes.empty());
        uint64_t bad[Words];for(unsigned i=0;i<Words;++i)bad[i]=boot[i];bad[PPOnDelays]=UINT32_MAX;
        assert(restorePPS(io,bad,r)==Status::NoReference && io.writes.empty());
        io.allones=0xC720C;
        assert(restorePPS(io,boot,r)==Status::Unreadable && io.writes.empty());
    }
    {   // PPS: a write that does not take.
        struct Stuck : IO {void write(uint32_t reg,uint32_t value) {writes.emplace_back(reg,value);if(reg!=0xC7208)r[reg]=value;}} io;
        io.lost();uint64_t r[8] {};
        assert(restorePPS(io,boot,r)==Status::Mismatch && r[6]==1 && r[4]==0);
    }
    {   // PLL: DPLL0 lost -> powered, programmed, enabled and locked, then selected and ungated for port A, in i915's order.
        IO io;io.lost();uint64_t r[8] {};delayed=0;
        assert(restorePLL(io,boot,delay,r)==Status::Applied);
        const std::vector<std::pair<uint32_t,uint32_t>> expected {
            {0x46010,PLLPowerEnable},{0x164284,0x01C001A5},{0x164288,0x00000488},{0x164B00,0x50000000|(3U<<25)},
            {0x46010,PLLPowerEnable|PLLPowerState|PLLEnable},{0x164280,0x01000C00},{0x164280,0x01000800}};
        assert(io.writes==expected);
        assert(r[0]==0 && r[1]==0 && r[2]==(PLLPowerEnable|PLLPowerState) && r[3]==(PLLPowerEnable|PLLPowerState|PLLEnable|PLLLock) &&
            r[4]==0x01000C00 && r[5]==0x01000800 && r[6]==1 && r[7]==0 && delayed==0);
        assert(io.r[0x46010]==(PLLEnable|PLLLock|PLLPowerEnable|PLLPowerState));
    }
    {   // PLL: the AFC startup field already right -> DIV0 not written; a reference without DIV0 -> DIV0 untouched.
        IO io;io.lost();io.r[0x164B00]=0x50000000|(3U<<25);uint64_t r[8] {};
        assert(restorePLL(io,boot,delay,r)==Status::Applied && io.writes.size()==6);
        for(auto &w:io.writes)assert(w.first!=0x164B00);
        uint64_t noDiv[Words];for(unsigned i=0;i<Words;++i)noDiv[i]=boot[i];noDiv[PLL0Div0]=UINT32_MAX;
        IO other;other.lost();other.r[0x164B00]=0;
        assert(restorePLL(other,noDiv,delay,r)==Status::Applied && other.r[0x164B00]==0);
    }
    {   // PLL: running (the state survived, or the native path's own enable) -> nothing written; the reference's port
        // clock gated at boot -> nothing to recreate; a reference selecting DPLL4 -> unsupported, nothing written.
        IO io;io.booted();uint64_t r[8] {};
        assert(restorePLL(io,boot,delay,r)==Status::Skipped && io.writes.empty());
        IO lockedOnly;lockedOnly.lost();lockedOnly.r[0x46010]=PLLEnable;
        assert(restorePLL(lockedOnly,boot,delay,r)==Status::Skipped && lockedOnly.writes.empty());
        uint64_t gated[Words];for(unsigned i=0;i<Words;++i)gated[i]=boot[i];gated[ClockSelect]=0x01000C00;
        IO lost;lost.lost();
        assert(restorePLL(lost,gated,delay,r)==Status::Skipped && lost.writes.empty());
        uint64_t dpll4[Words];for(unsigned i=0;i<Words;++i)dpll4[i]=boot[i];dpll4[ClockSelect]=0x01000802;
        assert(restorePLL(lost,dpll4,delay,r)==Status::Unsupported && lost.writes.empty() && r[0]==2);
        uint64_t off[Words];for(unsigned i=0;i<Words;++i)off[i]=boot[i];off[PLL0Enable]=PLLPowerEnable|PLLPowerState;
        assert(restorePLL(lost,off,delay,r)==Status::NoReference && lost.writes.empty());
        uint64_t empty[Words] {};
        assert(restorePLL(lost,empty,delay,r)==Status::NoReference && lost.writes.empty()); // An empty reference holds no PLL.
        lost.allones=0x46010;
        assert(restorePLL(lost,boot,delay,r)==Status::Unreadable && lost.writes.empty());
    }
    {   // PLL: the power state never comes -> stop after the power enable; the lock never comes -> stop before the clock select.
        IO io;io.lost();io.neverPower=true;uint64_t r[8] {};delayed=0;
        assert(restorePLL(io,boot,delay,r)==Status::Timeout && r[7]==1 && io.writes.size()==1 && delayed==1000);
        IO lock;lock.lost();lock.neverLock=true;delayed=0;
        assert(restorePLL(lock,boot,delay,r)==Status::Timeout && r[7]==2 && lock.writes.size()==5 && delayed==1000 && lock.r[0x164280]==0x01000C00);
    }
    {   // PLL: DPLL1 selected at boot -> the DPLL1 registers.
        uint64_t one[Words];for(unsigned i=0;i<Words;++i)one[i]=boot[i];
        one[ClockSelect]=0x01000801;one[PLL1Enable]=PLLEnable|PLLLock|PLLPowerEnable|PLLPowerState;one[PLL1CFGCR0]=0x1111;one[PLL1CFGCR1]=0x2222;one[PLL1Div0]=UINT32_MAX;
        IO io;io.lost();uint64_t r[8] {};
        assert(restorePLL(io,one,delay,r)==Status::Applied && r[0]==1 && io.writes[0].first==0x46014 && io.writes[1]==std::make_pair(0x16428CU,0x1111U) &&
            io.writes[2]==std::make_pair(0x164290U,0x2222U) && io.r[0x164280]==0x01000801 && io.r[0x46010]==0);
    }
    {   // MBUS credits: a cold wake reset three of five words -> the boot values written back, the others left alone.
        struct CIO {
            std::map<uint32_t,uint32_t> r;std::vector<std::pair<uint32_t,uint32_t>> writes;uint32_t allones {~0U};
            uint32_t read(uint32_t reg) {assert(r.count(reg));return reg==allones ? UINT32_MAX : r[reg];}
            void write(uint32_t reg,uint32_t v) {assert(r.count(reg));writes.emplace_back(reg,v);r[reg]=v;}
        } io;
        for(auto reg:CreditRegs)io.r[reg]=0;
        io.r[0x45038]=0x111010;io.r[0x45048]=0x111010;io.r[0x7003C]=0x4802;io.r[0x7103C]=0x4802;io.r[0x7203C]=0x4802;
        uint64_t boot[Credits] {};snapshotCredits(io,boot);
        assert(boot[0]==0x111010 && boot[2]==0x4802 && boot[4]==0x4802);
        io.r[0x45038]=0;io.r[0x7003C]=0;io.r[0x7203C]=0;uint64_t r[12] {};
        assert(restoreCredits(io,boot,r)==Status::Applied && r[10]==0x15 && r[11]==0 && io.writes.size()==3);
        assert(io.writes[0]==std::make_pair(0x45038U,0x111010U) && io.writes[1]==std::make_pair(0x7003CU,0x4802U) && io.writes[2]==std::make_pair(0x7203CU,0x4802U));
        assert(r[0]==0 && r[1]==0x111010 && r[5]==0x111010 && r[7]==0x4802 && r[9]==0x4802);
        io.writes.clear();
        assert(restoreCredits(io,boot,r)==Status::Skipped && io.writes.empty() && r[10]==0);
        // Only a powered pipe's DBOX word is written; the others are reported as skipped.
        io.r[0x45038]=0;io.r[0x7003C]=0;io.r[0x7103C]=0;io.writes.clear();
        assert(restoreCredits(io,boot,r,1)==Status::Applied && r[10]==0x5 && r[11]==0x18 && io.writes.size()==2);
        io.writes.clear();
        // An unreadable word is skipped and reported; an empty boot reference writes nothing; a write that does not take.
        io.allones=0x45048;io.r[0x7103C]=0;
        assert(restoreCredits(io,boot,r)==Status::Applied && r[10]==(1U<<3) && r[11]==(1U<<1) && io.writes.size()==1);
        io.allones=~0U;io.writes.clear();
        uint64_t none[Credits];for(auto &v:none)v=UINT32_MAX;
        assert(restoreCredits(io,none,r)==Status::NoReference && io.writes.empty() && r[11]==0x1F);
        struct Stuck : CIO {void write(uint32_t reg,uint32_t v) {writes.emplace_back(reg,v);if(reg!=0x7003C)r[reg]=v;}} stuck;
        stuck.r=io.r;stuck.r[0x7003C]=0;
        assert(restoreCredits(stuck,boot,r)==Status::Mismatch && r[10]==(1U<<2));
    }
    {   // Display core registers: a snapshot reads a pipe's bank only while its well is powered; the restore writes
        // back the bandwidth buddy registers, CHICKEN_MISC_2 with its DPT-disable bit and the powered pipes' chicken
        // registers, and reports the rest as skipped.
        struct CIO {
            std::map<uint32_t,uint32_t> r;std::vector<std::pair<uint32_t,uint32_t>> writes;uint32_t allones {~0U};
            uint32_t read(uint32_t reg) {assert(r.count(reg));return reg==allones ? UINT32_MAX : r[reg];}
            void write(uint32_t reg,uint32_t v) {assert(r.count(reg));writes.emplace_back(reg,v);r[reg]=v;}
        } io;
        for(auto reg:CoreRegs)io.r[reg]=0;
        io.r[0x45038]=0x111010;io.r[0x45130]=0x8080000;io.r[0x45134]=0x1E;io.r[0x45140]=0x8080000;io.r[0x45144]=0x1E;
        io.r[0x42084]=0x40000008;io.r[0x70038]=0x80000040;io.r[0x71038]=0x40;io.r[0x7003C]=0x4802;io.r[0x51004]=0x12345;
        uint64_t boot[CoreWords] {};snapshotCore(io,boot,1);
        assert(boot[0]==1 && boot[2]==1 && boot[3]==0x3FFFF && boot[4]==0x111010 && boot[15]==0x40000008 && boot[19]==0x12345 &&
            boot[21]==0x80000040 && boot[22]==UINT32_MAX && boot[23]==UINT32_MAX);
        // The power loss: every register zero. Pipe A powered only: pipe B's chicken register is skipped.
        for(auto reg:CoreRegs)io.r[reg]=0;
        uint64_t r[16] {};
        assert(restoreCore(io,boot,r,1)==Status::Applied && r[14]==0x20878 && r[15]==0x80000 && io.writes.size()==6);
        assert(io.r[0x42084]==0x40000008 && io.r[0x45130]==0x8080000 && io.r[0x45134]==0x1E && io.r[0x45140]==0x8080000 &&
            io.r[0x45144]==0x1E && io.r[0x70038]==0x80000040 && io.r[0x71038]==0 && io.r[0x45038]==0 && io.r[0x7003C]==0);
        assert(r[0]==0 && r[4]==0 && r[6]==UINT32_MAX && r[7]==0x8080000 && r[11]==0x40000008 && r[12]==0x80000040 && r[13]==UINT32_MAX);
        io.writes.clear();
        assert(restoreCore(io,boot,r,1)==Status::Skipped && io.writes.empty() && r[14]==0 && r[15]==0x80000);
        // A boot value without the DPT-disable bit still gets it.
        uint64_t noDPT[CoreWords];for(unsigned i=0;i<CoreWords;++i)noDPT[i]=boot[i];noDPT[15]=0x8;
        io.r[0x42084]=0x8;io.writes.clear();
        assert(restoreCore(io,noDPT,r,1)==Status::Applied && io.writes.size()==1 && io.writes[0]==std::make_pair(0x42084U,0x40000008U));
        // With both pipes powered the second chicken register is written too, but only from a reference that read it.
        io.r[0x71038]=0;io.writes.clear();
        assert(restoreCore(io,boot,r,3)==Status::Skipped && r[14]==0 && r[15]==(1U<<19) && io.writes.empty() && io.r[0x71038]==0);
        uint64_t both[CoreWords] {};io.r[0x71038]=0x40;snapshotCore(io,both,3);io.r[0x71038]=0;io.writes.clear();
        assert(both[23]==0x40 && restoreCore(io,both,r,3)==Status::Applied && r[14]==(1U<<19) && r[15]==0 && io.r[0x71038]==0x40);
        // An unreadable register is skipped and reported; an empty reference writes nothing; a write that does not take.
        io.allones=0x45140;io.r[0x45130]=0;io.writes.clear();
        assert(restoreCore(io,boot,r,1)==Status::Applied && r[14]==(1U<<3) && (r[15]&(1U<<5)) && io.writes.size()==1);
        io.allones=~0U;io.writes.clear();
        uint64_t none[CoreWords] {};
        assert(restoreCore(io,none,r,7)==Status::NoReference && io.writes.empty());
        struct Stuck : CIO {void write(uint32_t reg,uint32_t v) {writes.emplace_back(reg,v);if(reg!=0x42084)r[reg]=v;}} stuck;
        stuck.r=io.r;stuck.r[0x42084]=0;
        assert(restoreCore(stuck,boot,r,1)==Status::Mismatch && (r[14]&(1U<<11)));
    }
    puts("rplp_port ok");
    return 0;
}
