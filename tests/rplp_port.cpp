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
static unsigned waited;
static void wait(unsigned us) {waited+=us;}
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
        IO io;io.lost();uint64_t r[8] {};waited=0;
        assert(restorePLL(io,boot,wait,r)==Status::Applied);
        const std::vector<std::pair<uint32_t,uint32_t>> expected {
            {0x46010,PLLPowerEnable},{0x164284,0x01C001A5},{0x164288,0x00000488},{0x164B00,0x50000000|(3U<<25)},
            {0x46010,PLLPowerEnable|PLLPowerState|PLLEnable},{0x164280,0x01000C00},{0x164280,0x01000800}};
        assert(io.writes==expected);
        assert(r[0]==0 && r[1]==0 && r[2]==(PLLPowerEnable|PLLPowerState) && r[3]==(PLLPowerEnable|PLLPowerState|PLLEnable|PLLLock) &&
            r[4]==0x01000C00 && r[5]==0x01000800 && r[6]==1 && r[7]==0 && waited==0);
        assert(io.r[0x46010]==(PLLEnable|PLLLock|PLLPowerEnable|PLLPowerState));
    }
    {   // PLL: the AFC startup field already right -> DIV0 not written; a reference without DIV0 -> DIV0 untouched.
        IO io;io.lost();io.r[0x164B00]=0x50000000|(3U<<25);uint64_t r[8] {};
        assert(restorePLL(io,boot,wait,r)==Status::Applied && io.writes.size()==6);
        for(auto &w:io.writes)assert(w.first!=0x164B00);
        uint64_t noDiv[Words];for(unsigned i=0;i<Words;++i)noDiv[i]=boot[i];noDiv[PLL0Div0]=UINT32_MAX;
        IO other;other.lost();other.r[0x164B00]=0;
        assert(restorePLL(other,noDiv,wait,r)==Status::Applied && other.r[0x164B00]==0);
    }
    {   // PLL: running (the state survived, or the native path's own enable) -> nothing written; the reference's port
        // clock gated at boot -> nothing to recreate; a reference selecting DPLL4 -> unsupported, nothing written.
        IO io;io.booted();uint64_t r[8] {};
        assert(restorePLL(io,boot,wait,r)==Status::Skipped && io.writes.empty());
        IO lockedOnly;lockedOnly.lost();lockedOnly.r[0x46010]=PLLEnable;
        assert(restorePLL(lockedOnly,boot,wait,r)==Status::Skipped && lockedOnly.writes.empty());
        uint64_t gated[Words];for(unsigned i=0;i<Words;++i)gated[i]=boot[i];gated[ClockSelect]=0x01000C00;
        IO lost;lost.lost();
        assert(restorePLL(lost,gated,wait,r)==Status::Skipped && lost.writes.empty());
        uint64_t dpll4[Words];for(unsigned i=0;i<Words;++i)dpll4[i]=boot[i];dpll4[ClockSelect]=0x01000802;
        assert(restorePLL(lost,dpll4,wait,r)==Status::Unsupported && lost.writes.empty() && r[0]==2);
        uint64_t off[Words];for(unsigned i=0;i<Words;++i)off[i]=boot[i];off[PLL0Enable]=PLLPowerEnable|PLLPowerState;
        assert(restorePLL(lost,off,wait,r)==Status::NoReference && lost.writes.empty());
        uint64_t empty[Words] {};
        assert(restorePLL(lost,empty,wait,r)==Status::NoReference && lost.writes.empty()); // An empty reference holds no PLL.
        lost.allones=0x46010;
        assert(restorePLL(lost,boot,wait,r)==Status::Unreadable && lost.writes.empty());
    }
    {   // PLL: the power state never comes -> stop after the power enable; the lock never comes -> stop before the clock select.
        IO io;io.lost();io.neverPower=true;uint64_t r[8] {};waited=0;
        assert(restorePLL(io,boot,wait,r)==Status::Timeout && r[7]==1 && io.writes.size()==1 && waited==1000);
        IO lock;lock.lost();lock.neverLock=true;waited=0;
        assert(restorePLL(lock,boot,wait,r)==Status::Timeout && r[7]==2 && lock.writes.size()==5 && waited==1000 && lock.r[0x164280]==0x01000C00);
    }
    {   // PLL: DPLL1 selected at boot -> the DPLL1 registers.
        uint64_t one[Words];for(unsigned i=0;i<Words;++i)one[i]=boot[i];
        one[ClockSelect]=0x01000801;one[PLL1Enable]=PLLEnable|PLLLock|PLLPowerEnable|PLLPowerState;one[PLL1CFGCR0]=0x1111;one[PLL1CFGCR1]=0x2222;one[PLL1Div0]=UINT32_MAX;
        IO io;io.lost();uint64_t r[8] {};
        assert(restorePLL(io,one,wait,r)==Status::Applied && r[0]==1 && io.writes[0].first==0x46014 && io.writes[1]==std::make_pair(0x16428CU,0x1111U) &&
            io.writes[2]==std::make_pair(0x164290U,0x2222U) && io.r[0x164280]==0x01000801 && io.r[0x46010]==0);
    }
    puts("rplp_port ok");
    return 0;
}
