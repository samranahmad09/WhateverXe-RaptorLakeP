#include "kern_rplp_iog.hpp"
#include <assert.h>
#include <map>
#include <stdio.h>
using namespace RPLPIOG;
struct Mem {
    std::map<uint64_t,uint8_t> b;
    unsigned reads {};
    void put8(uint64_t a,uint8_t v) {b[a]=v;}
    void put32(uint64_t a,uint32_t v) {for(unsigned i=0;i<4;++i)b[a+i]=uint8_t(v>>(8*i));}
    void put64(uint64_t a,uint64_t v) {for(unsigned i=0;i<8;++i)b[a+i]=uint8_t(v>>(8*i));}
    uint8_t read8(uint64_t a) {++reads;auto it=b.find(a);return it==b.end() ? 0 : it->second;}
    uint32_t read32(uint64_t a) {uint32_t v=0;for(unsigned i=0;i<4;++i)v|=uint32_t(read8(a+i))<<(8*i);return v;}
    uint64_t read64(uint64_t a) {uint64_t v=0;for(unsigned i=0;i<8;++i)v|=uint64_t(read8(a+i))<<(8*i);return v;}
};
constexpr uint64_t K=0xFFFFFF8010000000ULL;
int main() {
    // Pointers: kernel addresses, eight-byte aligned, nothing else.
    assert(pointer(K) && pointer(K+0x1000) && !pointer(0) && !pointer(K+4) && !pointer(0x7FFF00001000ULL) && !pointer(0xFFFFFFFFFFFFFFF8ULL));
    assert(object(K+4) && !object(0x1000));
    Mem m;
    const uint64_t owner=K+0x10000,machine=K+0x20000,pipe0=K+0x30000,pipe1=K+0x31000,notifier0=K+0x40000,notifier1=K+0x41000,
        timing0=K+0x50000,timing1=K+0x51000,fb0=K+0x60000,fb1=K+0x61000,fb2=K+0x62000,fb3=K+0x63000,priv0=K+0x70000,priv1=K+0x71000,
        priv2=K+0x72000,priv3=K+0x73000,c0=K+0x80000,c1=K+0x81000,array0=K+0x90000,array1=K+0x91000,vtable=K+0xA0000,
        fnSleep=K+0xB0000,fnWake=K+0xB1000;
    // The accelerator: its display machine with two pipes, flags, cached state, the vtable's two slots.
    m.put64(owner,vtable);m.put64(owner+AccelMachine,machine);m.put32(owner+AccelFlags,0x1C2C6DC4);m.put32(owner+AccelCached,2);
    m.put32(machine+MachineCount,2);m.put32(machine+MachineNesting,1);m.put64(machine+MachinePipes,pipe0);m.put64(machine+MachinePipes+8,pipe1);
    m.put64(machine+MachinePipes+16,K+0x32000);   // beyond the count: never read
    m.put64(vtable+VTableWillSleep,fnSleep);m.put64(vtable+VTableDidWake,fnWake);m.put64(fnSleep,0x00000000E9A5E58948ULL);m.put64(fnWake,0x5641574155E58948ULL);
    for(unsigned i=0;i<2;++i) {
        const auto pipe=i ? pipe1 : pipe0,notifier=i ? notifier1 : notifier0,timing=i ? timing1 : timing0,fb=i ? fb1 : fb0;
        m.put64(pipe+PipeAccel,owner);m.put64(pipe+PipeMachine,i ? K+0x21000 : machine);m.put64(pipe+PipeFB,fb);m.put64(pipe+PipeNotifier,notifier);
        m.put32(pipe+PipeIndex,i);m.put64(pipe+PipeTiming,timing);
        for(unsigned k=0;k<TimingWords;++k)m.put64(timing+8*k,1000*(i+1)+k);
        m.put64(notifier+NotifierFB,i ? fb2 : fb);m.put64(notifier+NotifierCallback,K+0xC0000);m.put64(notifier+NotifierTarget,pipe);m.put64(notifier+NotifierRef,i);
        m.put8(notifier+NotifierEnabled,1);m.put32(notifier+NotifierGroup,0x170);m.put32(notifier+NotifierMask,0x184F);m.put32(notifier+NotifierLastEvent,4-i);
        m.put64(notifier+NotifierStart,77+i);m.put64(notifier+NotifierEnd,88+i);
    }
    // Two controllers in the family's list: the first with three framebuffers, the second with one; the family's globals.
    const uint64_t gAckTo=K+0xD0000,gAckRef=K+0xD0008,gMux=K+0xD0010,gPower=K+0xD0011,gWS=K+0xD0012,gList=K+0xD0020;
    m.put64(gAckTo,K+0xE0000);m.put64(gAckRef,0x1234);m.put8(gMux,0);m.put8(gPower,1);m.put8(gWS,1);m.put64(gList,c0);
    m.put64(c0+ControllerNext,c1);m.put64(c1+ControllerNext,0);
    m.put64(c0+ControllerFBs,array0);m.put64(c1+ControllerFBs,array1);
    m.put64(c0+ControllerPowerThread,K+0xF0000);m.put8(c0+ControllerWsWait,0x6);m.put8(c0+ControllerNeedsWork,1);m.put8(c0+ControllerDidWork,2);m.put8(c0+ControllerAsyncWork,0);m.put32(c0+ControllerComputed,0x1048);
    m.put8(c1+ControllerWsWait,0x1);m.put32(c1+ControllerComputed,0x8);
    m.put64(array0,fb0);m.put64(array0+8,fb1);m.put64(array0+16,fb2);m.put64(array0+24,0);
    m.put64(array1,fb3);m.put64(array1+8,0);
    const uint64_t fbs[] {fb0,fb1,fb2,fb3},privs[] {priv0,priv1,priv2,priv3};
    for(unsigned i=0;i<4;++i) {
        m.put32(fbs[i]+FBFlags,0x8|i<<8);m.put8(fbs[i]+FBPendingPower,uint8_t(0x10|i));m.put64(fbs[i]+FBPrivate,i==3 ? 0 : privs[i]);
        m.put64(privs[i]+PrivateController,i==3 ? c1 : c0);m.put32(privs[i]+PrivateIndex,i);m.put32(privs[i]+PrivateSent,0x100+i);m.put32(privs[i]+PrivateAcked,0x200+i);
    }
    // pipesOf
    uint64_t mach=0,pipes[MaxPipes];uint32_t count=0,nesting=0;
    assert(pipesOf(m,owner,mach,count,nesting,pipes,MaxPipes)==2 && mach==machine && count==2 && nesting==1 && pipes[0]==pipe0 && pipes[1]==pipe1);
    assert(pipesOf(m,0,mach,count,nesting,pipes,MaxPipes)==0 && mach==0);
    assert(pipesOf(m,K+0x11000,mach,count,nesting,pipes,MaxPipes)==0 && mach==0);   // no machine pointer there
    // sample
    Globals g;g.ackTo=gAckTo;g.ackRef=gAckRef;g.mux=gMux;g.systemPower=gPower;g.wsPowerOn=gWS;g.controllers=gList;
    uint64_t w[SampleWords];
    sample(m,g,owner,w,5000,6000,1,2,9);
    assert(w[0]==5000 && w[5]==6000 && w[42]==owner && w[43]==machine);
    assert((w[1]&0xFF)==1 && ((w[1]>>8)&0xFF)==2 && ((w[1]>>16)&0xFF)==4 && ((w[1]>>24)&0xFF)==2 && ((w[1]>>32)&0xFF)==2 && (w[1]>>40)==9);
    assert(w[2]==K+0xE0000 && w[3]==0x1234 && (w[4]&0xFF)==0 && ((w[4]>>8)&0xFF)==1 && ((w[4]>>16)&0xFF)==1 && (w[4]>>32)==0x1C2C6DC4);
    assert(w[6]==c0 && w[7]==(0x6ULL|1ULL<<8|2ULL<<16|0x1048ULL<<32) && w[8]==c1 && w[9]==(0x1ULL|0x8ULL<<32) && w[10]==0 && w[11]==0);
    assert(w[14]==fb0 && (w[15]&0xFFFFFFFF)==0x8 && ((w[15]>>32)&0xF)==0 && ((w[15]>>40)&0xFF)==0 && ((w[15]>>48)&0xFF)==0 && (w[15]>>56)==0 && w[16]==(0x100ULL|0x200ULL<<32));
    assert(w[17]==fb1 && ((w[18]>>40)&0xFF)==1 && (w[18]>>56)==1 && w[20]==fb2 && ((w[21]>>32)&0xF)==2);
    assert(w[23]==fb3 && ((w[24]>>40)&0xFF)==0xFF && ((w[24]>>48)&0xFF)==1 && (w[24]>>56)==0 && w[25]==0);   // no private part: index unknown, no ids
    assert(w[26]==0 && w[29]==0);
    assert(w[32]==1002 && w[33]==1003 && w[34]==1001 && w[35]==2002 && w[36]==2003 && w[37]==2001 && w[38]==0);
    assert((w[41]&0xFFFF)==(4|1<<8) && ((w[41]>>16)&0xFFFF)==(3|1<<8) && ((w[41]>>48)&0xFF)==2 && (w[41]>>56)==1);
    // Seven framebuffers on the first controller: six kept, the second controller still counted, the seventh and later never read.
    m.put64(array0+24,fb3);m.put64(array0+32,fb0);m.put64(array0+40,fb1);m.put64(array0+48,fb2);m.put64(array0+56,0);
    sample(m,g,owner,w,1,2,1,2,1);
    assert(((w[1]>>8)&0xFF)==2 && ((w[1]>>16)&0xFF)==6 && w[29]==fb1 && (w[30]>>56)==5 && ((w[30]>>48)&0xFF)==0);
    m.put64(array0+24,0);
    // Without globals or an accelerator the sample is zeros but the context.
    Globals none;
    sample(m,none,0,w,7,8,2,3,4);
    assert(w[0]==7 && w[5]==8 && w[2]==0 && w[6]==0 && w[14]==0 && w[32]==0 && (w[1]&0xFF)==2 && ((w[1]>>8)&0xFFFF)==0 && w[42]==0 && w[43]==0);
    // pipeSlot
    uint64_t s[PipeSlotWords];
    pipeSlot(m,owner,s,3,111,222,0x3E80000000001ULL,K+0x100,K+0x200);
    assert(s[0]==Schema && s[1]==(3ULL|1ULL<<8|2ULL<<16) && s[2]==111 && s[3]==222 && s[4]==owner && s[5]==machine && s[6]==(2ULL|1ULL<<32));
    assert(s[7]==(0x1C2C6DC4ULL|2ULL<<32) && s[8]==vtable && s[9]==fnSleep && s[10]==0x00000000E9A5E58948ULL && s[11]==fnWake && s[12]==0x5641574155E58948ULL);
    assert(s[13]==K+0x100 && s[14]==K+0x200 && s[15]==0x3E80000000001ULL);
    auto p=s+PipeSlotHeader;
    assert(p[0]==pipe0 && p[1]==fb0 && p[2]==notifier0 && p[3]==(0x7ULL|0ULL<<8|1ULL<<16|0x170ULL<<32) && p[4]==(0x184FULL|4ULL<<32));
    assert(p[5]==K+0xC0000 && p[6]==pipe0 && p[7]==0 && p[8]==1000 && p[10]==1002 && p[11]==1003 && p[13]==1005 && p[14]==77 && p[15]==88);
    p=s+PipeSlotHeader+PipeWords;
    assert(p[0]==pipe1 && p[3]==(0x1ULL|1ULL<<8|1ULL<<16|0x170ULL<<32) && p[4]==(0x184FULL|3ULL<<32) && p[8]==2000 && p[15]==89);   // machine and notifier framebuffer differ
    assert(s[PipeSlotHeader+2*PipeWords]==0);
    pipeSlot(m,0,s,1,1,2,3,4,5);
    assert(s[1]==1 && s[4]==0 && s[5]==0 && s[8]==0 && s[PipeSlotHeader]==0);
    // The event helpers.
    assert(powerThread(m,fb0)==K+0xF0000 && powerThread(m,fb3)==0 && powerThread(m,0)==0);
    assert(fbIndex(m,fb2)==2 && fbIndex(m,fb3)==0xFF && fbIndex(m,0)==0xFF);
    assert(controllerComputed(m,fb1)==0x1048 && controllerComputed(m,fb3)==0);
    assert(fbWord(m,fb1,0xE00002C0)==(0xE00002C0ULL|0x6ULL<<32|2ULL<<40|0x108ULL<<48) && fbWord(m,fb3,5)==(5ULL|0x308ULL<<48) && fbWord(m,0,5)==5);
    // The worker's references, with the installed image's bytes: each resolves to the known global, a changed byte to nothing.
    const uint64_t worker=K+0x12328;
    const uint8_t bytes[4][8] {{0x48,0x8B,0x1D,0x10,0xBB,0x02,0x00,0x45},{0x48,0x83,0x3D,0xDD,0xC2,0x02,0x00,0x00},
                               {0x80,0x3D,0x98,0xC2,0x02,0x00,0x00,0x0F},{0x4C,0x8B,0x35,0xBC,0xC2,0x02,0x00,0x48}};
    const uint64_t expected[4] {K+0x3DE50,K+0x3E8A8,K+0x3E870,K+0x3E950};
    for(unsigned i=0;i<4;++i) {
        for(unsigned k=0;k<8;++k)m.put8(worker+WorkerReferences[i].distance+k,bytes[i][k]);
        assert(resolveReference(m,worker,WorkerReferences[i])==expected[i]);
    }
    m.put8(worker+0x11,0x49);
    assert(resolveReference(m,worker,WorkerReferences[0])==0);
    assert(sizeof(WorkerReferences)/sizeof(WorkerReferences[0])==4 && HeaderWords==16 && SampleWords==44 && Samples==72 && PipeSlotWords==80 && PipeSlots==8 && EventWords==8 && Events==96);
    puts("rplp_iog: ok");
    return 0;
}
