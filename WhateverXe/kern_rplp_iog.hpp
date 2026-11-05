// Memory-only records of the graphics family's sleep bookkeeping and of the accelerator's display pipes, read for the
// record and never written (opt-in with -wex-rplp-wakelog, Raptor Lake-P only). Rounds 51 to 54: the panel lights
// after a real power loss, the GPU executes nothing afterwards, the accelerator's own sleep and wake entries are never
// called, and the WindowServer crash report of round 54 shows the panel's display pipe with a fresh did-wake stamp and
// a will-sleep stamp as old as the boot: the family's sleep notice never reached the pipe, so its wake notice found the
// pipe not asleep. Public IOGraphics (IOGraphics-585.1, IOFramebuffer.cpp, systemWork) sends that sleep notice to every
// framebuffer only once its aggregate state allows the pending root reply: no framebuffer still acknowledged awake by
// the server, no display change outstanding, no MUX switch outstanding; the same wait is the two-minute sleep entry.
// The sampler reads that state every few seconds through the entry and the first minute of the wake: the pending reply
// and its token, the MUX byte, each controller's wait/work bytes and cached state, each framebuffer's flag word, pending
// power nibble and sent/acknowledged server message ids, and each display pipe's sleep/wake stamps. Field offsets are
// those of the installed IOGraphicsFamily 585.2 and IOAcceleratorFamily2 442.10 objects; the plugin takes them only
// after the installed image's function anchors match, and follows a pointer only when it looks like one.
#ifndef KERN_RPLP_IOG_HPP
#define KERN_RPLP_IOG_HPP
#include <stdint.h>
namespace RPLPIOG {
// IOFBController
constexpr uint32_t ControllerNext=0x10,ControllerFBs=0x18,ControllerPowerThread=0x188,ControllerWsWait=0x1B4,
    ControllerNeedsWork=0x1B5,ControllerDidWork=0x1B6,ControllerAsyncWork=0x1B7,ControllerComputed=0x1BC;
// IOFramebuffer and its private part
constexpr uint32_t FBFlags=0x170,FBPendingPower=0x199,FBPrivate=0x1C8,PrivateController=0x0,PrivateIndex=0x24,
    PrivateSent=0x4F0,PrivateAcked=0x4F4;
// the accelerator, its display machine, a display pipe, its notifier and its timing block
constexpr uint32_t AccelMachine=0x378,AccelFlags=0xC78,AccelCached=0x1280,MachineCount=0x108,MachineNesting=0x10C,
    MachinePipes=0x88,PipeAccel=0x88,PipeMachine=0x90,PipeFB=0x98,PipeNotifier=0xA0,PipeIndex=0x284,PipeTiming=0x2C8,
    NotifierFB=0x10,NotifierCallback=0x18,NotifierTarget=0x20,NotifierRef=0x28,NotifierEnabled=0x30,NotifierGroup=0x34,
    NotifierMask=0x3C,NotifierLastEvent=0x40,NotifierStart=0x88,NotifierEnd=0x90;
constexpr uint32_t VTableWillSleep=0x9D8,VTableDidWake=0x9E0;
constexpr unsigned MaxControllers=4,MaxFBs=6,MaxFBsPerController=32,MaxPipes=4,SamplePipes=3,TimingWords=6;
// RPLPWakeIOG: a header of sixteen words, then samples of SampleWords each. Header: schema, binding flags (1 the
// family's kext seen, 2 its anchors matched, 4 the globals resolved from symbols, 8 the globals resolved from the
// worker's own references, 16 the observers routed, 32 the two resolutions disagree), the kext's base, its size,
// the pending-reply address, the controller list's address, the MUX byte's address, samples taken, words per sample,
// the sample capacity, the first sample's sanity word (system power byte | server power-on byte << 8 | MUX << 16 |
// verbose-boot byte << 24 | first controller valid << 32), the timebase (numerator | denominator << 32), reserved.
constexpr unsigned HeaderWords=16,SampleWords=44,Samples=72;
constexpr uint32_t Schema=1;
// A sample: 0 uptime ns; 1 phase | controllers << 8 | framebuffers << 16 | pipes << 24 | when << 32 | generation << 40;
// 2 the pending reply; 3 its token; 4 MUX | system power << 8 | server power-on << 16 | the accelerator's flags << 32;
// 5 mach_absolute_time; 6..13 four controllers, two words each (the pointer; wait | needs << 8 | did << 16 | async << 24
// | cached state << 32); 14..31 six framebuffers, three words each (the pointer; flags | pending nibble << 32 | index
// << 40 | controller slot << 48 | position << 56; sent id | acknowledged id << 32); 32..40 three pipes, three words each
// (the will-sleep, did-wake and power-on stamps); 41 the pipes' last event | enabled << 8, sixteen bits per pipe, then
// the machine's count << 48 and nesting << 56; 42 the accelerator; 43 the display machine.
// When: 1 will-sleep, 2 an entry tick, 3 will-power-on, 4 has-powered-on, 5 the trace's end, 6 a wake tick, 7 the boot.
struct Globals {uint64_t ackTo {},ackRef {},mux {},systemPower {},wsPowerOn {},controllers {},verboseBoot {};};
inline bool pointer(uint64_t p) {return p>=0xFFFFFF8000000000ULL && p<0xFFFFFFFFFFFFF000ULL && !(p&7);}
inline bool object(uint64_t p) {return p>=0xFFFFFF8000000000ULL && p<0xFFFFFFFFFFFFF000ULL;}
template<class Mem> unsigned pipesOf(Mem &m,uint64_t owner,uint64_t &machine,uint32_t &count,uint32_t &nesting,uint64_t *pipes,unsigned max) {
    machine=0;count=nesting=0;
    if(!pointer(owner))return 0;
    machine=m.read64(owner+AccelMachine);
    if(!pointer(machine)) {machine=0;return 0;}
    count=m.read32(machine+MachineCount);nesting=m.read32(machine+MachineNesting);
    unsigned n=0;
    for(unsigned i=0;i<max && i<count;++i) {
        const auto p=m.read64(machine+MachinePipes+8*i);
        if(!pointer(p))break;
        pipes[n++]=p;
    }
    return n;
}
template<class Mem> void sample(Mem &m,const Globals &g,uint64_t owner,uint64_t *w,uint64_t now,uint64_t mach,uint32_t phase,uint32_t when,uint32_t generation) {
    for(unsigned i=0;i<SampleWords;++i)w[i]=0;
    w[0]=now;w[5]=mach;w[42]=owner;
    if(g.ackTo)w[2]=m.read64(g.ackTo);
    if(g.ackRef)w[3]=m.read64(g.ackRef);
    uint64_t misc=0;
    if(g.mux)misc|=m.read8(g.mux);
    if(g.systemPower)misc|=uint64_t(m.read8(g.systemPower))<<8;
    if(g.wsPowerOn)misc|=uint64_t(m.read8(g.wsPowerOn))<<16;
    if(pointer(owner))misc|=uint64_t(m.read32(owner+AccelFlags))<<32;
    w[4]=misc;
    unsigned controllers=0,fbs=0;
    uint64_t c=g.controllers ? m.read64(g.controllers) : 0;
    for(unsigned i=0;i<MaxControllers && pointer(c);++i) {
        auto e=w+6+2*i;
        e[0]=c;
        e[1]=uint64_t(m.read8(c+ControllerWsWait))|uint64_t(m.read8(c+ControllerNeedsWork))<<8|uint64_t(m.read8(c+ControllerDidWork))<<16|
            uint64_t(m.read8(c+ControllerAsyncWork))<<24|uint64_t(m.read32(c+ControllerComputed))<<32;
        ++controllers;
        const auto array=m.read64(c+ControllerFBs);
        for(unsigned j=0;j<MaxFBsPerController && fbs<MaxFBs && pointer(array);++j) {
            const auto fb=m.read64(array+8*j);
            if(!pointer(fb))break;
            auto f=w+14+3*fbs;
            f[0]=fb;
            f[1]=uint64_t(m.read32(fb+FBFlags))|uint64_t(m.read8(fb+FBPendingPower)&0xFU)<<32|uint64_t(i)<<48|uint64_t(j)<<56;
            const auto priv=m.read64(fb+FBPrivate);
            if(pointer(priv)) {
                f[1]|=uint64_t(m.read32(priv+PrivateIndex)&0xFFU)<<40;
                f[2]=uint64_t(m.read32(priv+PrivateSent))|uint64_t(m.read32(priv+PrivateAcked))<<32;
            } else f[1]|=0xFFULL<<40;
            ++fbs;
        }
        c=m.read64(c+ControllerNext);
    }
    uint64_t machine=0,pipes[MaxPipes];uint32_t count=0,nesting=0;
    const unsigned n=pipesOf(m,owner,machine,count,nesting,pipes,SamplePipes);
    uint64_t last=uint64_t(count&0xFFU)<<48|uint64_t(nesting&0xFFU)<<56;
    for(unsigned i=0;i<n;++i) {
        auto p=w+32+3*i;
        const auto timing=m.read64(pipes[i]+PipeTiming);
        if(pointer(timing)) {p[0]=m.read64(timing+0x10);p[1]=m.read64(timing+0x18);p[2]=m.read64(timing+0x8);}
        const auto notifier=m.read64(pipes[i]+PipeNotifier);
        if(pointer(notifier))last|=(uint64_t(m.read32(notifier+NotifierLastEvent)&0xFFU)|uint64_t(m.read8(notifier+NotifierEnabled))<<8)<<(16*i);
    }
    w[41]=last;w[43]=machine;
    w[1]=uint64_t(phase&0xFFU)|uint64_t(controllers)<<8|uint64_t(fbs)<<16|uint64_t(n)<<24|uint64_t(when&0xFFU)<<32|uint64_t(generation&0xFFFFU)<<40;
}
// RPLPWakePipes: eight slots of PipeSlotWords. A slot: 0 schema; 1 when | valid << 8 | pipes << 16; 2 uptime ns; 3
// mach_absolute_time; 4 the accelerator; 5 the display machine; 6 count | nesting << 32; 7 the accelerator's flags |
// its cached power state << 32; 8 its vtable; 9 the vtable's will-sleep slot; 10 the first eight bytes there; 11 the
// did-wake slot; 12 the first eight bytes there; 13 the plugin's will-sleep observer; 14 its did-wake observer; 15 the
// timebase. Then four pipes of sixteen words: the pipe; its framebuffer; its notifier; (accelerator matches) |
// (machine matches) << 1 | (the notifier's framebuffer matches) << 2 | index << 8 | enabled << 16 | group << 32; mask |
// last event << 32; the callback; the target; the reference; the six timing stamps (framebuffer power off, power on,
// will-sleep, did-wake, mode will change, mode did change); the notifier's start and end times.
constexpr unsigned PipeWords=16,PipeSlotHeader=16,PipeSlotWords=PipeSlotHeader+MaxPipes*PipeWords,PipeSlots=8;
template<class Mem> void pipeSlot(Mem &m,uint64_t owner,uint64_t *w,uint32_t when,uint64_t now,uint64_t mach,uint64_t timebase,uint64_t wrapSleep,uint64_t wrapWake) {
    for(unsigned i=0;i<PipeSlotWords;++i)w[i]=0;
    w[0]=Schema;w[2]=now;w[3]=mach;w[4]=owner;w[13]=wrapSleep;w[14]=wrapWake;w[15]=timebase;
    uint64_t machine=0,pipes[MaxPipes];uint32_t count=0,nesting=0;
    const unsigned n=pipesOf(m,owner,machine,count,nesting,pipes,MaxPipes);
    w[1]=uint64_t(when&0xFFU)|uint64_t(pointer(owner))<<8|uint64_t(n)<<16;
    if(!pointer(owner))return;
    w[5]=machine;w[6]=uint64_t(count)|uint64_t(nesting)<<32;
    w[7]=uint64_t(m.read32(owner+AccelFlags))|uint64_t(m.read32(owner+AccelCached))<<32;
    const auto vtable=m.read64(owner);
    w[8]=vtable;
    if(pointer(vtable)) {
        w[9]=m.read64(vtable+VTableWillSleep);w[11]=m.read64(vtable+VTableDidWake);
        if(object(w[9]))w[10]=m.read64(w[9]);
        if(object(w[11]))w[12]=m.read64(w[11]);
    }
    for(unsigned i=0;i<n;++i) {
        auto p=w+PipeSlotHeader+PipeWords*i;
        const auto pipe=pipes[i];
        p[0]=pipe;p[1]=m.read64(pipe+PipeFB);p[2]=m.read64(pipe+PipeNotifier);
        uint64_t checks=uint64_t(m.read64(pipe+PipeAccel)==owner)|uint64_t(m.read64(pipe+PipeMachine)==machine)<<1;
        checks|=uint64_t(m.read32(pipe+PipeIndex)&0xFFU)<<8;
        if(pointer(p[2])) {
            const auto nfb=m.read64(p[2]+NotifierFB);
            checks|=uint64_t(nfb==p[1])<<2|uint64_t(m.read8(p[2]+NotifierEnabled))<<16|uint64_t(m.read32(p[2]+NotifierGroup))<<32;
            p[4]=uint64_t(m.read32(p[2]+NotifierMask))|uint64_t(m.read32(p[2]+NotifierLastEvent))<<32;
            p[5]=m.read64(p[2]+NotifierCallback);p[6]=m.read64(p[2]+NotifierTarget);p[7]=m.read64(p[2]+NotifierRef);
            p[14]=m.read64(p[2]+NotifierStart);p[15]=m.read64(p[2]+NotifierEnd);
        }
        p[3]=checks;
        const auto timing=m.read64(pipe+PipeTiming);
        if(pointer(timing))for(unsigned k=0;k<TimingWords;++k)p[8+k]=m.read64(timing+8*k);
    }
}
// The power thread the family's event path compares with the caller: framebuffer -> private -> controller -> +0x188.
template<class Mem> uint64_t powerThread(Mem &m,uint64_t fb) {
    if(!pointer(fb))return 0;
    const auto priv=m.read64(fb+FBPrivate);if(!pointer(priv))return 0;
    const auto c=m.read64(priv+PrivateController);if(!pointer(c))return 0;
    return m.read64(c+ControllerPowerThread);
}
// RPLPWakeFBEvents: a header of sixteen words (schema, calls since boot, kept, calls in the cycle, the notification
// counts per event 1..10 since boot, other events, the observers routed), then a ring of events, eight words each:
// 0 uptime ns; 1 kind | event << 8 | framebuffer index << 24 | phase << 32 | info non-null << 40 | generation << 48;
// 2 the framebuffer; 3 the info or argument; 4 the calling thread; 5 the controller's power thread; 6 the accelerator's
// flags | the controller's cached state << 32; 7 the result | wait << 32 | did << 40 | the framebuffer's flag word
// << 48 (sixteen bits). Kinds: 1/2 deliverFramebufferNotification entry/exit, 3/4 handleEvent entry/exit, 5/6
// notifyServer entry/exit, 7 serverAcknowledgeNotification, 8 serverPowerTimeout, 9/10 systemPowerChange entry/exit,
// 11/12 muxPowerMessage entry/exit.
constexpr unsigned EventHeader=16,EventWords=8,Events=96;
template<class Mem> uint64_t fbWord(Mem &m,uint64_t fb,uint32_t result) {
    uint64_t v=result;
    if(!pointer(fb))return v;
    v|=uint64_t(m.read32(fb+FBFlags)&0xFFFFU)<<48;
    const auto priv=m.read64(fb+FBPrivate);if(!pointer(priv))return v;
    const auto c=m.read64(priv+PrivateController);if(!pointer(c))return v;
    v|=uint64_t(m.read8(c+ControllerWsWait))<<32|uint64_t(m.read8(c+ControllerDidWork))<<40;
    return v;
}
template<class Mem> uint64_t fbIndex(Mem &m,uint64_t fb) {
    if(!pointer(fb))return 0xFF;
    const auto priv=m.read64(fb+FBPrivate);if(!pointer(priv))return 0xFF;
    return m.read32(priv+PrivateIndex)&0xFFU;
}
template<class Mem> uint64_t controllerComputed(Mem &m,uint64_t fb) {
    if(!pointer(fb))return 0;
    const auto priv=m.read64(fb+FBPrivate);if(!pointer(priv))return 0;
    const auto c=m.read64(priv+PrivateController);if(!pointer(c))return 0;
    return m.read32(c+ControllerComputed);
}
// The family's worker references its globals with RIP-relative operands at fixed distances from its entry; when the
// local symbols do not resolve, the plugin derives the addresses from those instructions after checking their bytes.
// Each entry: the distance from the worker's entry, the instruction's length, the operand's position, the opcode bytes.
struct Reference {uint32_t distance,length,operand;uint8_t opcode[3];uint32_t opcodeLength;};
constexpr Reference WorkerReferences[] {
    {0x11,7,3,{0x48,0x8B,0x1D},3},    // mov rbx, [rip+disp32]      the controller list
    {0x29B,8,3,{0x48,0x83,0x3D},3},   // cmp qword [rip+disp32], 0  the pending reply
    {0x2A9,7,2,{0x80,0x3D},2},        // cmp byte [rip+disp32], 0   the MUX byte
    {0x365,7,3,{0x4C,0x8B,0x35},3}    // mov r14, [rip+disp32]      the reply token
};
template<class Mem> uint64_t resolveReference(Mem &m,uint64_t worker,const Reference &r) {
    const auto at=worker+r.distance;
    for(unsigned i=0;i<r.opcodeLength;++i)if(m.read8(at+i)!=r.opcode[i])return 0;
    const int32_t disp=static_cast<int32_t>(m.read32(at+r.operand));
    return at+r.length+disp;
}
}
#endif
