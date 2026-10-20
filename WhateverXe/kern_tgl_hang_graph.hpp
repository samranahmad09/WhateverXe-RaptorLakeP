// Bounded CPU-visible command/reference graph at native failure. Recorded paths
// are candidates, never proof of GPU execution, memory lifetime or visibility.
#ifndef kern_tgl_hang_graph_hpp
#define kern_tgl_hang_graph_hpp
#include "kern_tgl_hang_pending.hpp"
#include "kern_tgl_ppgtt.hpp"
namespace TGLHangGraph {
constexpr unsigned MaxPages=96, MaxDraws=64, MaxCalls=32, MaxTargets=32;
constexpr unsigned MappedRootBytes=32768;
enum Kind : uint64_t {Command=1,Table=2,Surface=4,Sampler=8,Constant=16,Shader=32,Vertex=64,Index=128,Pixels=256,Auxiliary=512,ClearValue=1024,Scratch=2048};
enum Error : uint64_t {ReadHole=1,UnknownCommand=2,Truncated=4,CallLimit=8,Cycle=16,Depth=32,
 MissingState=64,PageLimit=128,DrawLimit=256,NoEnd=512,UnsupportedCall=1024};
enum Known : uint64_t {SurfaceBase=1,DynamicBase=2,InstructionBase=4,VS=8,PS=16,BT=32,SP=64};
struct Page {
 uint64_t q[8]; // gpu, kind, copied, CPU mapping stable, status, chain stable, mismatch, stream provenance bits
 TGLPPGTT::Mapping before,after;
 TGLPPGTT::Chain chainBefore,chainAfter;
 uint8_t bytes[4096];
};
struct Draw {uint64_t q[32];uint32_t raw[40];};
struct Call {uint64_t q[8];}; // candidate, from, target, depth, result, bytes walked, header, branch kind
struct State {
 uint64_t known,surface,dynamic,instruction,programs[4],binding,sampler,vsConstant,psConstant;
 uint64_t vertices[64],vertexMask,index;uint32_t strides[64],indexBytes,indexSize;
 uint32_t vs[9],ps[12],psExtra[2];
};
struct Capture {
 uint64_t q[16]; // schema, pages, draws, calls, targets, errors, drops, steps, reserved
 Page pages[MaxPages];Draw draws[MaxDraws];Call calls[MaxCalls];uint64_t targets[MaxTargets];
 // Scratch is embedded to avoid large or recursive kernel-stack allocations.
 State state[3];uint32_t packet[3][257];uint64_t path[3];
};
static_assert(sizeof(Capture)<600000,"bounded graph storage");
inline uint32_t word(const uint8_t *p){return TGLHangPending::u32(p);}
inline uint64_t pair(const uint32_t *p){return uint64_t(p[0])|(uint64_t(p[1])<<32);}
inline uint64_t address(uint64_t base,uint64_t off) {
 return base && off<(1ULL<<47) && base<=(1ULL<<47)-1-off ? base+off : 0;
}
inline unsigned length(uint32_t w) {
 if((w>>29)==3)return ((w>>27)&3)==1?1:(w&255)+2;
 if(w>>29)return 0;
 const unsigned op=(w>>23)&63;
 if(op==0 || op==5 || op==7 || op==10)return 1;
 if(op==0x1c || op==0x20 || op==0x22 || op==0x24 || op==0x31)return (w&255)+2;
 return 0;
}
template<class Reader> struct Walker {
 Capture &c;Reader &read;uint64_t provenance {1};
 Page *page(uint64_t gpu,uint64_t kind) {
  if(!gpu || gpu>>47)return nullptr;
  gpu&=~4095ULL;
  for(unsigned i=0;i<c.q[1];++i)if(c.pages[i].q[0]==gpu){c.pages[i].q[1]|=kind;c.pages[i].q[7]|=provenance;return c.pages+i;}
  if(c.q[1]>=MaxPages){c.q[5]|=PageLimit;++c.q[6];return nullptr;}
  auto &p=c.pages[c.q[1]++];p.q[0]=gpu;p.q[1]=kind;p.q[7]=provenance;read(p);if(!readable(&p))++c.q[8];if(p.q[6])++c.q[9];return &p;
 }
 bool readable(const Page *p) {return p && p->q[2]==4096 && p->q[3]==1 && !p->q[4] && p->q[5]==1 && !p->q[6];}
 void span(uint64_t gpu,uint64_t size,uint64_t kind) {
  if(!gpu || !size || size>16384 || gpu>>47 || size>(1ULL<<47)-gpu)return;
  const uint64_t last=(gpu+size-1)&~4095ULL;
  for(uint64_t at=gpu&~4095ULL;at<=last;at+=4096)page(at,kind);
 }
 bool fetch(uint64_t gpu,uint32_t &value) {
  auto p=page(gpu,Command);if(!readable(p)){c.q[5]|=ReadHole;return false;}
  if(gpu&3){c.q[5]|=Truncated;return false;}
  value=word(p->bytes+(gpu&4095));return true;
 }
 void draw(unsigned candidate,uint64_t gpu,const uint32_t *p,unsigned n,unsigned depth) {
  if(c.q[2]>=MaxDraws){c.q[5]|=DrawLimit;return;}
  const auto &s=c.state[depth];auto &d=c.draws[c.q[2]++];auto q=d.q;
  q[0]=candidate;q[1]=gpu;q[2]=s.known;q[3]=s.surface;q[4]=s.dynamic;q[5]=s.instruction;
  for(unsigned i=0;i<4;++i)q[6+i]=s.programs[i];
  q[10]=s.binding;q[11]=s.sampler;q[12]=s.vsConstant;q[13]=s.psConstant;
  q[14]=s.vertexMask;q[15]=s.index;q[16]=s.indexBytes;q[17]=s.indexSize;q[18]=n;
  q[19]=c.q[5];q[20]=provenance;memcpy(d.raw,p,(n<10?n:10)*4);memcpy(d.raw+10,s.vs,36);
  memcpy(d.raw+19,s.ps,48);memcpy(d.raw+31,s.psExtra,8);
  const uint64_t required=SurfaceBase|InstructionBase|VS|PS|BT;
  if((s.known&required)!=required)c.q[5]|=MissingState;
  // Snapshot all variants as candidate code. raw enable bits remain available;
  // a nonzero entry is not a claim that the corresponding shader dispatched.
  if(s.known&InstructionBase)for(unsigned i=0;i<4;++i) {
   if(!(s.known&(i?PS:VS)))continue;
   span(address(s.instruction,s.programs[i]),8192,Shader);
  }
  if((s.known&(DynamicBase|SP))==(DynamicBase|SP))span(address(s.dynamic,s.sampler),256,Sampler);
  if((s.known&DynamicBase) && (s.vsConstant&31))span(address(s.dynamic,s.vsConstant&~31ULL),(s.vsConstant&31)*32,Constant);
  if(s.psConstant&31)span(s.psConstant&~31ULL,(s.psConstant&31)*32,Constant);
  for(unsigned i=0;i<64;++i)if(s.vertexMask&(1ULL<<i)) {
   span(s.vertices[i],256,Vertex);
   if(n>=7 && !(p[1]&256) && !(p[0]&1024) && s.strides[i]) {
    const uint64_t bytes=uint64_t(p[2])*s.strides[i],offset=uint64_t(p[3])*s.strides[i];
    if(bytes<=16384)span(address(s.vertices[i],offset),bytes,Vertex);else ++c.q[11];
   }
  }
  if(s.known&VS)span(pair(s.vs+4)&~1023ULL,4096,Scratch);
  if(s.known&PS)span(pair(s.ps+4)&~1023ULL,4096,Scratch);
  if(n>=7 && (p[1]&256) && !(p[0]&1024) && s.indexSize && s.indexSize<=4) {
   const uint64_t offset=uint64_t(p[3])*s.indexSize,bytes=uint64_t(p[2])*s.indexSize;
   if(offset<=s.indexBytes && bytes<=s.indexBytes-offset && bytes<=4096)span(address(s.index,offset),bytes,Index);
  }
  if((s.known&(SurfaceBase|BT))!=(SurfaceBase|BT)){c.q[5]|=MissingState;return;}
  const uint64_t table=address(s.surface,s.binding);span(table,128,Table);
  for(unsigned i=0;table && i<32;++i) {
   auto t=page(table+4*i,Table);if(!readable(t))continue;
   const uint32_t off=word(t->bytes+((table+4*i)&4095));if(!off || (off&63))continue;
   const uint64_t target=address(s.surface,off);auto state=page(target,Surface);if(!readable(state))continue;
   const auto bytes=state->bytes+(target&4095);
   // Gen12 RENDER_SURFACE_STATE: data bits256..319, aux332..383,
   // clear390..431/enable330. Follow only observed non-planar color formats;
   // planar aliases must not be interpreted as an auxiliary allocation.
   if((word(bytes)>>29)==1) {
    page(TGLHangPending::u64(bytes+32),Pixels);
    const unsigned format=(word(bytes)>>18)&1023,mode=word(bytes+24)&7;
    if((format==0xc0 || format==0x80) && (mode==1 || mode==5)) {
     page(TGLHangPending::u64(bytes+40)&~4095ULL,Auxiliary);
     if(word(bytes+40)&1024)span(TGLHangPending::u64(bytes+48)&0x0000ffffffffffc0ULL,64,ClearValue);
    } else if(mode)++c.q[10];
   }
  }
 }
 void update(const uint32_t *p,unsigned n,unsigned depth) {
  auto &s=c.state[depth];const auto w=p[0];
  if(w==0x61010014 && n==22) {
   if(p[4]&1){s.surface=pair(p+4)&~4095ULL;s.known|=SurfaceBase;}
   if(p[6]&1){s.dynamic=pair(p+6)&~4095ULL;s.known|=DynamicBase;}
   if(p[10]&1){s.instruction=pair(p+10)&~4095ULL;s.known|=InstructionBase;}
  }
  if((w>>16)==0x7810 && n==9){s.programs[0]=pair(p+1)&~63ULL;s.known|=VS;memcpy(s.vs,p,36);}
  if((w>>16)==0x7820 && n==12){s.programs[1]=pair(p+1)&~63ULL;s.programs[2]=pair(p+8)&~63ULL;s.programs[3]=pair(p+10)&~63ULL;s.known|=PS;memcpy(s.ps,p,48);}
  if((w>>16)==0x784f && n==2)memcpy(s.psExtra,p,8);
  if((w>>16)==0x782a && n==2){s.binding=p[1]&~31U;s.known|=BT;}
  if((w>>16)==0x782f && n==2){s.sampler=p[1]&~31U;s.known|=SP;}
  if((w>>16)==0x786d && n==4) {
   if((w&0x1f00)==0x100 && (p[1]&0xf0000)==0x10000)s.vsConstant=pair(p+2);
   if((w&0x1f00)==0x1000 && (p[1]&0xf0000)==0x20000)s.psConstant=pair(p+2);
  }
  if((w>>16)==0x7808 && n>=5 && (n-1)%4==0)for(unsigned i=1;i<n;i+=4){const unsigned v=p[i]>>26;s.vertices[v]=pair(p+i+1);s.strides[v]=p[i]&4095;}
  if((w>>16)==0x7809 && n>=3 && (n-1)%2==0){s.vertexMask=0;for(unsigned i=1;i<n;i+=2)if(p[i]&(1U<<25))s.vertexMask|=1ULL<<(p[i]>>26);}
  if((w>>16)==0x780a && n==5){s.index=pair(p+2);s.indexSize=1U<<((p[1]>>8)&3);s.indexBytes=p[4];}
 }
 // Intel TGL Vol2a pp966-972: 0x18b00101 has POSH Start/Enable, not
 // second-level bit22. RCS skips that branch; its state must not be imported
 // from POCS. Also admit a genuine second-level call from the first level.
 // Other chaining/nesting modes remain explicit unresolved control flow.
 uint64_t walk(unsigned candidate,uint64_t base,const uint8_t *main,unsigned size,unsigned depth,unsigned &walked) {
  const unsigned limit=!main && !depth?MappedRootBytes:16384;
  walked=0;if(!base || base>>47 || (base&3) || size>limit || size>(1ULL<<47)-base)return Truncated;
  c.path[depth]=base;auto p=c.packet[depth];
  for(unsigned o=0;o<size;) {
   if(++c.q[7]>16384)return CallLimit;
   if(size-o<4)return Truncated;
   if(main)p[0]=word(main+o);else if(!fetch(base+o,p[0]))return ReadHole;
   const unsigned n=length(p[0]);if(!n)return UnknownCommand;
   if(n>(size-o)/4 || n>257)return Truncated;
   for(unsigned i=1;i<n;++i){if(main)p[i]=word(main+o+i*4);else if(!fetch(base+o+i*4,p[i]))return ReadHole;}
   walked=o+n*4;
   if((p[0]&0xff800000U)==0x05000000U)return 0;
   if((p[0]&0xff800000U)==0x18800000U) {
    if(c.q[3]>=MaxCalls)return CallLimit;
    auto &call=c.calls[c.q[3]++];auto q=call.q;
    q[0]=candidate;q[1]=base+o;q[2]=n==3?pair(p+1)&~3ULL:0;q[3]=depth+1;q[6]=p[0];
    const bool posh=p[0]==0x18b00101 && provenance==1;
    const bool chain=p[0]==0x18800101 || p[0]==0x18900101 || (p[0]==0x18b00101 && provenance==2);
    q[7]=posh?2:chain?3:p[0]==0x18c00101?1:0;
    if(!q[7] || n!=3)q[4]=UnsupportedCall;
    else if(depth>=2 || (p[0]==0x18c00101 && depth))q[4]=Depth;
    else {
     for(unsigned i=0;i<=depth;++i)if(q[2]==c.path[i])q[4]=Cycle;
     bool found=false;for(unsigned i=0;i<c.q[4];++i)found|=c.targets[i]==q[2];
     if(!found){if(c.q[4]>=MaxTargets)q[4]|=CallLimit;else c.targets[c.q[4]++]=q[2];}
     if(!q[4]) {
      const uint64_t saved=provenance;if(posh)provenance=2;
      if(posh)memset(c.state+depth+1,0,sizeof(State));else c.state[depth+1]=c.state[depth];
      span(q[2],16384,Command);unsigned done=0;
      q[4]=walk(candidate,q[2],nullptr,16384,depth+1,done);q[5]=done;
      if(!posh && !q[4])c.state[depth]=c.state[depth+1];
      provenance=saved;
     }
    }
    if(q[4]){c.q[5]|=q[4];if(!posh)memset(c.state+depth,0,sizeof(State));}
    if(chain)return q[4];
   } else {
    update(p,n,depth);if((p[0]>>16)==0x7b00 && n>=7)draw(candidate,base+o,p,n,depth);
   }
   o+=n*4;
  }
  return NoEnd;
 }
 void candidate(unsigned index,const TGLHangPending::Entry &entry) {
  c.q[0]=1;provenance=1;memset(c.state,0,sizeof(c.state));
  if(!entry.stable || entry.dwords>4096 || !entry.dwords || entry.copied!=entry.dwords*4){c.q[5]|=ReadHole;return;}
  unsigned walked=0;c.q[5]|=walk(index,entry.gpu,entry.bytes,entry.copied,0,walked);
 }
 void mappedCandidate(unsigned index,uint64_t gpu) {
  c.q[0]=1;provenance=1;memset(c.state,0,sizeof(c.state));
  unsigned walked=0;c.q[5]|=walk(index,gpu,nullptr,MappedRootBytes,0,walked);
  c.q[12]=walked;
 }
};
}
#endif
