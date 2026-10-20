#include "kern_tgl_hang_pending.hpp"
#include <array>
#include <cassert>
#include <memory>
using namespace TGLHangPending;
namespace {
constexpr uint64_t Ring=0xffff800000010000ULL,Context=Ring+0x10000,Stamps=Ring+0x20000,Buffer=Ring+0x30000;
struct Fixture {
 std::array<uint8_t,0x8a8> ring{};
 std::array<uint8_t,0xb0> regs{};
 std::array<uint8_t,0x80> context{};
 std::array<uint8_t,4096> stamps{};
 std::array<uint8_t,128> bytes{};
 bool partial{},mutateCache{},mutateOwner{},mutateRetired{},mutateWrite{},deny{};
 unsigned dataReads{};
 template<class T> static void put(uint8_t *p,unsigned offset,T value) {memcpy(p+offset,&value,sizeof(value));}
 Fixture() {
  put(ring.data(),0x20,Context);put(ring.data(),0x30,Stamps);
  put(ring.data(),0x38,uint32_t(1));put(ring.data(),0x8a0,uint32_t(1));
  put(regs.data(),0xac,uint32_t(6<<5));put(context.data(),0x7c,uint32_t(6));
  put(stamps.data(),64,uint32_t(10));entry(0,11);
  for(unsigned i=0;i<bytes.size();++i) bytes[i]=i^0x5a;
  // BB is deliberately invalid: candidate capture must not call this active.
 }
 void entry(unsigned i,uint32_t stamp,uint32_t words=32) {
  auto p=ring.data()+0xa0+i*32;put(p,0,stamp);put(p,8,uint64_t(0x40000000+i*4096));
  put(p,16,Buffer);put(p,24,words);
 }
 uint64_t read(uint64_t p,void *out,unsigned n) {
  if(deny)return 0;
  if(p==Buffer) {
   assert(n<=bytes.size());++dataReads;
   if(mutateCache)ring[0xa0]^=1;
   if(mutateOwner)context[0x7c]^=1;
   if(mutateRetired)stamps[64]^=1;
   if(mutateWrite)ring[0x8a0]^=1;
   const unsigned count=partial?n-1:n;memcpy(out,bytes.data(),count);return count;
  }
  auto copy=[&](uint64_t base,const auto &v)->bool {
   if(p<base || p-base>v.size() || n>v.size()-(p-base))return false;
   memcpy(out,v.data()+(p-base),n);return true;
  };
  return (copy(Ring,ring)||copy(Context,context)||copy(Stamps,stamps))?n:0;
 }
 std::unique_ptr<Capture> run() {
  auto c=std::make_unique<Capture>();auto snapshot=ring;
  collect(*c,Ring,regs.data(),snapshot.data(),[&](uint64_t p,void *o,unsigned n){return read(p,o,n);});return c;
 }
};
}
int main() {
 {Fixture f;auto c=f.run();assert(c->q[2]==0 && c->q[11]==1 && f.dataReads==1);
  assert(c->entries[0].stable && c->entries[0].copied==128);
  assert(!memcmp(c->entries[0].bytes,f.bytes.data(),128));}
 {Fixture f;f.context[0x7c]=7;auto c=f.run();assert(c->q[2]==3 && !f.dataReads);}
 {Fixture f;f.regs[0x94]=1;auto c=f.run();assert(c->q[2]==1 && !f.dataReads);}
 {Fixture f;f.ring[0x40]=1;auto c=f.run();assert(c->q[2]==1 && !f.dataReads);}
 {Fixture f;f.ring[0x38]=64;auto c=f.run();assert(c->q[2]==1 && !f.dataReads);}
 {Fixture f;f.ring[0x8a0]=64;auto c=f.run();assert(c->q[2]==1 && !f.dataReads);}
 {Fixture f;f.deny=true;auto c=f.run();assert(c->q[2]==2 && !f.dataReads);}
 for(uint32_t s:{uint32_t(9),uint32_t(10),uint32_t(10+0x80000000U)}) {
  Fixture f;f.entry(0,s);auto c=f.run();assert(c->q[2]==0 && !c->q[11] && !f.dataReads);
 }
 {Fixture f;Fixture::put(f.stamps.data(),64,uint32_t(0xfffffffe));f.entry(0,1);
  auto c=f.run();assert(c->q[2]==0 && c->q[11]==1 && c->entries[0].stable);}
 {Fixture f;f.partial=true;auto c=f.run();assert(c->q[2]==0 && !c->entries[0].stable && c->entries[0].copied==127);}
 {Fixture f;f.mutateCache=true;auto c=f.run();assert(c->q[2]==0 && !c->entries[0].stable);}
 for(unsigned mode=0;mode<3;++mode) {
  Fixture f;f.mutateOwner=mode==0;f.mutateRetired=mode==1;f.mutateWrite=mode==2;
  auto c=f.run();assert(c->q[2]==5);
 }
 {Fixture f;f.entry(0,11,Bytes/4+1);auto c=f.run();assert(!c->q[11] && c->q[12]==1 && !f.dataReads);}
 {Fixture f;Fixture::put(f.ring.data(),0xa0+16,uint64_t(0x1234));auto c=f.run();assert(!c->q[11] && !f.dataReads);}
 {Fixture f;Fixture::put(f.ring.data(),0xa0+16,UINT64_MAX-3);auto c=f.run();assert(!c->q[11] && !f.dataReads);}
 {Fixture f;Fixture::put(f.ring.data(),0xa0+8,uint64_t(0x7ffffffffffc));auto c=f.run();assert(!c->q[11] && !f.dataReads);}
 {Fixture f;f.ring[0x8a0]=0;for(unsigned i=0;i<64;++i)f.entry(i,11+i);
  auto c=f.run();assert(c->q[11]==4 && f.dataReads==4);
  for(unsigned i=0;i<4;++i)assert(c->entries[i].index==63-i && c->entries[i].stable);}
}
