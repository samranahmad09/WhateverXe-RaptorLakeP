// Failure-only CPU snapshots with explicit holes and ownership rechecks.
// nofault tolerates a concurrent unmap; matching metadata does not pin storage.
#ifndef kern_tgl_hang_detail_hpp
#define kern_tgl_hang_detail_hpp
#include "kern_tgl_hang_graph.hpp"
namespace TGLHangDetail {
constexpr uint64_t Schema=2;
using TGLHangPending::kernelRange;
enum Field {Context,Owner,Task,Table,TableClass,LRCBuffer,LRCCPU,LRCSize,LRCClass,
 RingBuffer,RingCPU,RingSize,RingLength,Descriptor,ContextID,Stamps,StampGPU,
 StampIndex,WriteIndex,Retired,Root,Fields=24};
struct Capture {
 uint64_t q[32]; // schema, guard, start/end ns, actual ring, expected class, reserved
 uint64_t before[Fields],after[Fields],candidates[4][8];
 // CPU physical backing and qualification, not a GGTT translation claim.
 uint64_t backing[28][3];
 uint8_t regs[0xb0],lrc[81920],ring[32768],stamps[2][4096];
 TGLHangPending::Capture pending;
 TGLHangGraph::Capture graph;
 uint8_t scratch[16384];
};
static_assert(sizeof(Capture)<1048576,"first-failure property must remain below 1MiB");
template<class Read>
bool ownership(uint64_t *q,uint64_t ring,uint64_t expectedClass,Read &read) {
 auto get=[&](uint64_t p,uint64_t offset,uint64_t &v,unsigned n=8) {
  v=0;return kernelRange(p,offset+n) && read(p+offset,&v,n)==n;
 };
 uint64_t back=0,owner=0,lrc=0,hi=0,lo=0,engine=0;
 if(!get(ring,0x20,q[Context]) || !get(ring,0x10,q[Owner]) ||
    !get(q[Context],0x50,owner) || owner!=q[Owner] ||
    !get(q[Context],0xb0,back) || back!=ring ||
    !get(q[Context],0x58,q[Task]) || !get(q[Task],0x260,q[Table]) ||
    !get(q[Table],0,q[TableClass]) || !expectedClass || q[TableClass]!=expectedClass ||
    !get(q[Context],0x98,q[LRCBuffer]) || !get(q[LRCBuffer],0,q[LRCClass]) ||
    !get(q[LRCBuffer],0x38,q[LRCCPU]) || !get(q[LRCBuffer],0x20,q[LRCSize]) ||
    !get(ring,0x18,lrc) || lrc!=q[LRCCPU] ||
    !get(ring,0x80,q[RingBuffer]) || !get(q[RingBuffer],0x38,q[RingCPU]) ||
    !get(q[RingBuffer],0x20,q[RingSize]) || !get(ring,0x8c,q[RingLength],4) ||
    !get(ring,0x40,engine,1) || engine ||
    !get(q[Context],0x89,q[Descriptor]) || !get(q[Context],0x7c,q[ContextID],4) ||
    !get(ring,0x30,q[Stamps]) || !get(ring,0x28,q[StampGPU]) ||
    !get(ring,0x38,q[StampIndex],4) || !get(ring,0x8a0,q[WriteIndex],4))return false;
 if(q[LRCSize]<8192 || q[LRCSize]>81920 || (q[LRCSize]&4095) ||
    !kernelRange(q[LRCCPU],q[LRCSize]) || (q[LRCCPU]&4095) ||
    q[RingLength]<4096 || q[RingLength]>32768 || (q[RingLength]&(q[RingLength]-1)) ||
    q[RingSize]<q[RingLength] || !kernelRange(q[RingCPU],q[RingLength]) || (q[RingCPU]&4095) ||
    q[StampIndex]>=64 || q[WriteIndex]>=64 || !kernelRange(q[Stamps],4096))return false;
 if(!get(q[Stamps],q[StampIndex]*64,q[Retired],4) ||
    !get(q[LRCCPU],0x10c4,hi,4) || !get(q[LRCCPU],0x10cc,lo,4))return false;
 q[Root]=(hi<<32)|lo;return q[Root] && !(q[Root]&4095);
}
template<class Read,class Physical>
void mappedPage(TGLHangGraph::Page &p,const uint64_t *owner,uint64_t directBase,uint64_t directEnd,Read &read,Physical &physical) {
 auto get=[&](uint64_t address,uint64_t &v)->bool{return kernelRange(address,8) && read(address,&v,8)==8;};
 p.q[4]=1;p.before=TGLPPGTT::inspect(owner[Table],p.q[0],get);
 p.chainBefore=TGLPPGTT::inspectChain(owner[Table],p.q[0],get,physical);
 const auto &m=p.before;
 if(!m.status && m.rootPhysical==owner[Root] && m.physical<=UINT64_MAX-directBase) {
  const uint64_t cpu=directBase+m.physical;p.q[4]=2;
  if(cpu>=directBase && cpu<directEnd && directEnd-cpu>=4096 && kernelRange(cpu,4096)) {
   p.q[2]=read(cpu,p.bytes,4096);p.q[4]=p.q[2]==4096?0:3;
  }
 }
 p.after=TGLPPGTT::inspect(owner[Table],p.q[0],get);
 p.chainAfter=TGLPPGTT::inspectChain(owner[Table],p.q[0],get,physical);
 p.q[3]=p.q[2]==4096 && !p.before.status && !memcmp(&p.before,&p.after,sizeof(p.before));
 if(!p.q[4] && !p.q[3])p.q[4]=4;
 p.q[5]=!p.chainBefore.q[1] && !p.chainAfter.q[1] && !memcmp(&p.chainBefore,&p.chainAfter,sizeof(p.chainBefore));
 p.q[6]=p.chainBefore.q[6]|p.chainAfter.q[6];
}
template<class Read>
bool cacheMatches(uint64_t ring,const TGLHangPending::Entry &e,Read &read) {
 uint8_t row[32];if(e.index>=64 || read(ring+0xa0+e.index*32,row,32)!=32)return false;
 return TGLHangPending::u32(row)==e.stamp && TGLHangPending::u64(row+8)==e.gpu &&
  TGLHangPending::u64(row+16)==e.cpu && TGLHangPending::u32(row+24)==e.dwords;
}
// A coalesced native submission keeps the FIRST GPU address but the LAST
// segment's CPU pointer/length (execute@51f50/51fdc, replay@2f6ef/2f745).
// Locate a separate GPU root using the nearest preceding ring BBS, its stamp
// write and the matching cache record. This is provenance, not proof that the
// GPU is still executing it. In particular a completed RCS stamp says nothing
// here about outstanding POCS work. Preserve strict tail-copy checks below.
template<class Read,class Walker>
void ringRoot(Capture &c,Read &read,Walker &walker) {
 auto q=c.q+24;q[0]=1;q[5]=5;
 const auto b=c.before;const unsigned length=static_cast<unsigned>(b[RingLength]);
 const unsigned head=TGLHangPending::u32(c.regs)&0x1ffffc;
 if(head>=length || (c.q[12]&((1ULL<<(length/4096))-1))!=((1ULL<<(length/4096))-1) ||
    !(c.q[10]&2) || TGLHangPending::u32(c.lrc+0x1024)!=TGLHangPending::u32(c.regs+12))return;
 auto word=[&](unsigned at){return TGLHangPending::u32(c.ring+(at&(length-1)));};
 q[5]=1;
 for(unsigned distance=12;distance<=length;distance+=4) {
  const unsigned at=(head+length-distance)&(length-1);
  if(word(at)!=0x18900101)continue;
  const uint64_t gpu=uint64_t(word(at+4))|(uint64_t(word(at+8))<<32);
  if(!gpu || gpu>>47 || (gpu&3))continue;
  q[1]=gpu;q[2]=at;q[5]=2;
  bool stampFound=false;uint32_t stamp=0;
  for(unsigned off=12;off+24<=distance && off<=1024;off+=4) {
   // HDC Pipeline Flush changes only DW0 bit 9, not the six-DWORD layout.
   if((word(at+off)&~0x200U)==0x7a000004 && (word(at+off+4)&(3U<<14))==(1U<<14) &&
      (uint64_t(word(at+off+8))|(uint64_t(word(at+off+12))<<32))==b[StampGPU]+b[StampIndex]*64) {
    stamp=word(at+off+16);stampFound=true;break;
   }
  }
  if(!stampFound)return;
  q[4]=stamp;q[5]=3;
  for(unsigned n=0;n<64;++n) {
   const unsigned index=(static_cast<unsigned>(b[WriteIndex])+63-n)&63;
   const uint64_t address=c.q[4]+0xa0+index*32;uint8_t row[32],after[32],bbs[12]{};
   if(read(address,row,32)!=32 || TGLHangPending::u32(row)!=stamp || TGLHangPending::u64(row+8)!=gpu)continue;
   q[3]=index;q[5]=0;walker.mappedCandidate(TGLHangPending::Count,gpu);
   // Refuse all dependent pages if provenance changed during the walk.
   bool same=read(address,after,32)==32 && !memcmp(row,after,32);
   for(unsigned j=0;j<3;++j) {
    const unsigned offset=(at+j*4)&(length-1);
    same=read(b[RingCPU]+offset,bbs+j*4,4)==4 && same;
    same=TGLHangPending::u32(bbs+j*4)==word(at+j*4) && same;
   }
   if(!same){q[5]=4;c.q[1]=8;}
   return;
  }
  return;
 }
}
template<class Read,class Physical>
void collect(Capture &c,const TGLHangPending::Capture &pending,const uint8_t *regs,uint64_t expectedClass,
 uint64_t directBase,uint64_t directEnd,Read read,Physical physical) {
 c.q[0]=Schema;c.graph.q[0]=1;c.q[1]=1;c.q[4]=pending.q[3];c.q[5]=expectedClass;
 memcpy(c.regs,regs,sizeof(c.regs));memcpy(&c.pending,&pending,sizeof(pending));
 if(pending.q[0]!=1 || pending.q[2] || pending.q[11]>4 || regs[0x94])return;
 if(!ownership(c.before,c.q[4],expectedClass,read))return;
 const auto b=c.before;
 c.q[1]=2;
 if(b[ContextID]!=((TGLHangPending::u32(regs+0xac)>>5)&2047) || b[Context]!=pending.q[4] || b[ContextID]!=pending.q[5] || b[ContextID]!=pending.q[6] ||
    b[StampIndex]!=pending.q[7] || b[Retired]!=pending.q[8] || b[Retired]!=pending.q[9] ||
    b[WriteIndex]!=pending.q[10])return;
 c.q[13]=read(b[Stamps],c.stamps[0],4096);
 for(unsigned kind=0;kind<2;++kind) {
  const uint64_t cpu=kind?b[RingCPU]:b[LRCCPU],size=kind?b[RingLength]:b[LRCSize];
  auto bytes=kind?c.ring:c.lrc;const unsigned off=kind?20:0;
  for(unsigned p=0;p<size/4096;++p) {
   const uint64_t got=read(cpu+p*4096,bytes+p*4096,4096);
   c.q[kind?11:9]+=got;if(got==4096)c.q[kind?12:10]|=1ULL<<p;
   auto row=c.backing[off+p];row[0]=cpu+p*4096;row[2]=physical(row[0],row[1]);
  }
 }
 auto pageReader=[&](TGLHangGraph::Page &p){mappedPage(p,b,directBase,directEnd,read,physical);};
 TGLHangGraph::Walker<decltype(pageReader)> walker{c.graph,pageReader};
 ringRoot(c,read,walker);if(c.q[1]==8)return;
 // Prioritize the candidate nearest the saved ring head, then the smallest
 // positive retired delta. Header search is a ranking hint, not an execution claim.
 uint64_t score[4];unsigned order[4];
 const uint64_t head=TGLHangPending::u32(regs)&0x1ffffc;
 for(unsigned i=0;i<pending.q[11];++i) {
  const auto &e=pending.entries[i];order[i]=i;score[i]=(1ULL<<32)+uint32_t(e.stamp-b[Retired]);
  for(unsigned o=0;head<b[RingLength] && o<b[RingLength];o+=4) {
   if(!(c.q[12]&(1ULL<<(o/4096))))continue;
   const auto w=TGLHangPending::u32(c.ring+o);if(w!=0x18900101)continue;
   uint32_t lo=0,hi=0;unsigned p1=(o+4)&(b[RingLength]-1),p2=(o+8)&(b[RingLength]-1);
   if(!(c.q[12]&(1ULL<<(p1/4096))) || !(c.q[12]&(1ULL<<(p2/4096))))continue;
   lo=TGLHangPending::u32(c.ring+p1);hi=TGLHangPending::u32(c.ring+p2);
   if(((uint64_t(hi)<<32)|lo)==e.gpu){const uint64_t distance=(head-o)&(b[RingLength]-1);if(distance<score[i])score[i]=distance;}
  }
 }
 for(unsigned i=0;i<pending.q[11];++i)for(unsigned j=i+1;j<pending.q[11];++j)if(score[order[j]]<score[order[i]]){auto t=order[i];order[i]=order[j];order[j]=t;}
 for(unsigned n=0;n<pending.q[11];++n) {
  const unsigned i=order[n];const auto &e=pending.entries[i];auto q=c.candidates[n];c.q[15]=n+1;
  q[0]=i;q[1]=e.stamp;q[2]=e.gpu;q[3]=e.copied;q[4]=score[i];q[7]=1;
  if(!e.stable || !e.dwords || e.dwords>4096 || e.copied!=e.dwords*4 || !e.gpu ||
     e.gpu>>47 || e.copied>(1ULL<<47)-e.gpu || !kernelRange(e.cpu,e.copied))continue;
  q[7]=2;if(!cacheMatches(c.q[4],e,read))continue;
  q[7]=16;if(read(e.cpu,c.scratch,e.copied)!=e.copied || memcmp(c.scratch,e.bytes,e.copied))continue;
  q[7]=0;
  for(unsigned o=0;o<e.copied;) {
   const uint64_t gpu=e.gpu+o;auto page=walker.page(gpu,TGLHangGraph::Command);
   const unsigned size=static_cast<unsigned>(e.copied-o<4096-(gpu&4095)?e.copied-o:4096-(gpu&4095));
   if(!walker.readable(page))q[7]|=4;
   else {q[5]+=size;if(memcmp(page->bytes+(gpu&4095),e.bytes+o,size))q[7]|=8;}
   o+=size;
  }
  if(!q[7])walker.candidate(i,e);
  q[6]=cacheMatches(c.q[4],e,read);if(!q[6])q[7]|=2;
 }
 c.q[14]=read(b[Stamps],c.stamps[1],4096);c.q[1]=3;
 if(!ownership(c.after,c.q[4],expectedClass,read))return;
 c.q[1]=memcmp(c.before,c.after,sizeof(c.before))?4:0;
}
}
#endif
