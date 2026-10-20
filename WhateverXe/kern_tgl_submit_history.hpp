// Bounded receipts at existing native submission boundaries. No waiting.
#ifndef kern_tgl_submit_history_hpp
#define kern_tgl_submit_history_hpp
#include <stdint.h>
#include <string.h>
namespace TGLSubmitHistory {
constexpr uint32_t Registers[] {0x2030,0x2034,0x2038,0x203c,0x2064,0x2068,
 0x2074,0x2078,0x2084,0x209c,0x2234,0x2238,0x2510,0x2514,0x21c4,0x21c8};
struct Record {
 // schema, begin/end ns, native submit ordinal, phase (before/after/pre-STOP),
 // owner, streamer, reset epoch, active bank, guard, read errors,
 // consumed CSBs, software CSB head, last CSB, render ACK, thread.
 uint64_t q[16];
 // Four native bank slots: item, context, descriptor, image, image TAIL,
 // image CMD_BUF_CCTL, retired stamp, CPU write index, force flag, context ID.
 uint64_t slots[4][10];
 uint32_t registers[16];
};
static_assert(sizeof(Record)==512,"submission history wire record");
template<unsigned Capacity=256,unsigned Early=64> struct History {
 struct Slot {uint32_t busy;uint64_t sequence;Record record;};
 Slot recent[Capacity]{},early[Early]{};
 uint64_t attempts{},drops{},frozenAt{};
 uint32_t frozen{};
 bool stopped() const {return __atomic_load_n(&frozen,__ATOMIC_ACQUIRE)!=0;}
 bool put(Slot &s,uint64_t sequence,const Record &r) {
  uint32_t free=0;
  if(!__atomic_compare_exchange_n(&s.busy,&free,1U,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))return false;
  const bool ok=!stopped();
  if(ok) {s.record=r;s.sequence=sequence;}
  __atomic_store_n(&s.busy,0U,__ATOMIC_RELEASE);return ok;
 }
 void append(const Record &r) {
  if(stopped())return;
  const auto n=__atomic_fetch_add(&attempts,1ULL,__ATOMIC_RELAXED);
  if(!put(recent[n%Capacity],n+1,r))__atomic_fetch_add(&drops,1ULL,__ATOMIC_RELAXED);
  if(n<Early)put(early[n],n+1,r);
 }
 void freeze(uint64_t ns) {
  frozenAt=ns;__atomic_store_n(&frozen,1U,__ATOMIC_RELEASE);
 }
 // A busy slot is explicitly absent. Never race its writer or wait for it.
 static uint64_t copy(Slot &s,Record &out) {
  uint32_t free=0;
  if(!__atomic_compare_exchange_n(&s.busy,&free,1U,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))return 0;
  const auto sequence=s.sequence;if(sequence)out=s.record;
  __atomic_store_n(&s.busy,0U,__ATOMIC_RELEASE);return sequence;
 }
};
template<class Read>
void contextSlots(Record &r,Read read) {
 auto field=[&](uint64_t p,unsigned off,uint64_t &v,unsigned n=8) {
  v=0;
  if(!p || p+off+n<p || read(p+off,&v,n)!=n) {++r.q[10];return false;}
  return true;
 };
 uint64_t owner=0,desc=0,engine=255;
 r.q[9]=1;
 if(!field(r.q[6],0x10,owner) || owner!=r.q[5] ||
    !field(r.q[6],0x28,desc) || !field(desc,0,engine,1) || engine ||
    !field(r.q[6],0x50,r.q[8],4) || r.q[8]>1)return;
 r.q[9]=2;
 for(unsigned i=0;i<4;++i) {
  auto s=r.slots[i];uint64_t buffer=0,length=0,ring=0,stamps=0,index=0,reg=0;
  if(!field(r.q[6],0x30+i*8,s[0]))continue;
  if(!s[0])continue;
  if(!field(s[0],0,s[1]) || !field(s[1],0x50,owner) || owner!=r.q[5] ||
     !field(s[1],0x6c,engine,1) || engine || !field(s[1],0x89,s[2]) || !(s[2]&1) ||
     !field(s[1],0x98,buffer) || !field(buffer,0x20,length) || length!=81920 ||
     !field(buffer,0x38,s[3]) || !field(s[3],0x101c,s[4],4) ||
     !field(s[3],0x12d8,reg,4) || reg!=0x2084 || !field(s[3],0x12dc,s[5],4) ||
     !field(s[1],0xb0,ring) || !field(ring,0x10,owner) || owner!=r.q[5] ||
     !field(ring,0x30,stamps) || !field(ring,0x38,index,4) || index>=64 ||
     !field(stamps,unsigned(index*64),s[6],4) || !field(ring,0x8a0,s[7],4) ||
     !field(s[1],0xc9,s[8],1) || !field(s[1],0x7c,s[9],4)) {++r.q[10];continue;}
 }
 r.q[9]=r.q[10]?3:0;
}
}
#endif
