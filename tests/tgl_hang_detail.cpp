#include "kern_tgl_hang_detail.hpp"
#include "kern_tgl_hang_select.hpp"
#include <cassert>
#include <map>
#include <memory>
#include <vector>
#include <cstdio>
using namespace TGLHangDetail;
namespace {
constexpr uint64_t K=0xffff800000000000ULL,Ring=K+0x200000,Ctx=Ring+0x1000,Task=Ring+0x2000,
 LRCBuf=Ring+0x3000,RingBuf=Ring+0x4000,LRC=Ring+0x5000,RingStorage=Ring+0x8000,Stamps=Ring+0x9000,
 Cache=Ring+0xa000,TableObject=K+0x20000,Class=K+0xabcdef,Main=0x40000000;
struct Fixture {
 std::map<uint64_t,std::vector<uint8_t>> blocks;
 TGLHangPending::Capture pending{};uint8_t regs[0xb0]{};
 bool mutateOwner{},mutateCache{},partial{},badPhysical{};bool did{};
 void alloc(uint64_t p,unsigned n){blocks[p].resize(n);}
 template<class T>void put(uint64_t p,T value){auto it=blocks.upper_bound(p);assert(it!=blocks.begin());--it;assert(p-it->first+sizeof(value)<=it->second.size());memcpy(it->second.data()+p-it->first,&value,sizeof(value));}
 void leaf(uint64_t gpu,uint64_t phys){put(K+0x12000+((gpu>>12)&511)*8,phys|3ULL);alloc(K+phys,4096);}
 Fixture() {
  for(auto p:{Ring,Ctx,Task,LRCBuf,RingBuf,RingStorage,Stamps,Cache,TableObject})alloc(p,4096);alloc(LRC,8192);
  for(auto p:{K+0x1000,K+0x2000,K+0x7000,K+0xc000,K+0x11000,K+0x12000,K+0x14000,K+0x15000})alloc(p,4096);
  for(auto p:{K+0x3000,K+0x8000,K+0xd000})alloc(p,16384);
  put(TableObject,Class);put(TableObject+0x40,K+0x1000);put(TableObject+0x48,K+0x3000);
  put(K+0x1000+0x18,uint64_t(0x2000));put(K+0x1000+0x20,K+0x2000);
  put(K+0x3000,K+0x7000);put(K+0x3008,K+0x8000);
  put(K+0x7000+0x18,uint64_t(0x14000));put(K+0x7000+0x20,K+0x14000);
  put(K+0x8000+32,K+0xc000);put(K+0x8000+40,K+0xd000);
  put(K+0xc000+0x18,uint64_t(0x15000));put(K+0xc000+0x20,K+0x15000);
  put(K+0xd000,K+0x11000);put(K+0x11000+0x18,uint64_t(0x12000));put(K+0x11000+0x20,K+0x12000);
  put(K+0x2000,uint64_t(0x14003));put(K+0x14000+8,uint64_t(0x15003));put(K+0x15000,uint64_t(0x12003));
  leaf(Main,0x30000);for(unsigned i=0;i<3;++i)leaf(Main+0x20000+i*4096,0x40000+i*4096);
  leaf(Main+0x30000,0x50000);leaf(Main+0x40000,0x60000);
  put(Ring+0x20,Ctx);put(Ring+0x10,K+0x999000);put(Ctx+0x50,K+0x999000);put(Ctx+0xb0,Ring);
  put(Ctx+0x58,Task);put(Task+0x260,TableObject);put(Ctx+0x98,LRCBuf);put(LRCBuf,Class+1);
  put(LRCBuf+0x38,LRC);put(LRCBuf+0x20,uint64_t(8192));put(Ring+0x18,LRC);
  put(Ring+0x80,RingBuf);put(RingBuf+0x38,RingStorage);put(RingBuf+0x20,uint64_t(4096));put(Ring+0x8c,uint32_t(4096));
  put(Ctx+0x89,uint64_t(0x1234));put(Ctx+0x7c,uint32_t(8));put(Ring+0x30,Stamps);put(Ring+0x28,Main+0x50000);
  put(Ring+0x38,uint32_t(1));put(Ring+0x8a0,uint32_t(1));put(Stamps+64,uint32_t(10));put(LRC+0x10cc,uint32_t(0x2000));
  uint32_t id=8<<5,head=12;memcpy(regs+0xac,&id,4);memcpy(regs,&head,4);
  put(RingStorage,uint32_t(0x18900101));put(RingStorage+4,uint32_t(Main));
  auto &e=pending.entries[0];std::vector<uint32_t> v(22);v[0]=0x61010014;v[4]=(Main+0x30000)|1;v[6]=(Main+0x40000)|1;v[10]=(Main+0x20000)|1;
  const uint32_t rest[]={0x78100007,0x240,0,0,0,0,0,0,0,0x7820000a,0x40,0,0,0,0,0,0,0x140,0,0xc0,0,0x782a0000,0x20,0x782f0000,0,0x7b000005,0,3,0,1,0,0,0x05000000};v.insert(v.end(),rest,rest+sizeof(rest)/4);
  e.index=0;e.stamp=11;e.gpu=Main;e.cpu=Cache;e.dwords=v.size();e.copied=v.size()*4;e.stable=1;memcpy(e.bytes,v.data(),e.copied);
  memcpy(blocks[Cache].data(),v.data(),e.copied);memcpy(blocks[K+0x30000].data(),v.data(),e.copied);
  put(Ring+0xa0,uint32_t(11));put(Ring+0xa8,Main);put(Ring+0xb0,Cache);put(Ring+0xb8,uint32_t(v.size()));
  pending.q[0]=1;pending.q[3]=Ring;pending.q[4]=Ctx;pending.q[5]=pending.q[6]=8;pending.q[7]=1;pending.q[8]=pending.q[9]=10;pending.q[10]=1;pending.q[11]=1;
 }
 uint64_t read(uint64_t p,void *out,unsigned n) {
  if(p==K+0x30000 && n==4096 && !did){did=true;if(mutateOwner)put(Stamps+64,uint32_t(12));if(mutateCache)put(Ring+0xa0,uint32_t(12));}
  auto it=blocks.upper_bound(p);if(it==blocks.begin())return 0;--it;
  if(p-it->first+n>it->second.size())return 0;
  const unsigned count=partial && p==K+0x30000 && n==4096?n-1:n;memcpy(out,it->second.data()+p-it->first,count);return count;
 }
 std::unique_ptr<Capture> run() {
  auto c=std::make_unique<Capture>();
  collect(*c,pending,regs,Class,K,K+0x1000000,[&](uint64_t p,void *out,unsigned n){return read(p,out,n);},
   [&](uint64_t cpu,uint64_t &phys){phys=cpu-K;if(badPhysical && cpu==K+0x2000)phys+=4096;return cpu>=K && cpu<K+0x1000000;});return c;
 }
 void root() {
  uint32_t head=40,start=0x40200000;memcpy(regs,&head,4);memcpy(regs+12,&start,4);
  put(LRC+0x1024,start);put(RingStorage+12,uint32_t(0x7a000004));put(RingStorage+16,uint32_t(1<<14));
  put(RingStorage+20,Main+0x50000+64);put(RingStorage+28,uint32_t(11));
  // The cache tail is not the root's CPU alias. A mapped walk must not use it.
  blocks[Cache][100]^=1;pending.entries[0].bytes[100]^=1;
 }
};
}
int main(int argc,char **argv) {
 {Fixture f;const uint64_t origin=Ring+0xb000,other=Ring+0xc000,stream=Ring+0xd000,item=Ring+0xe000;
  for(auto p:{origin,other,stream,item})f.alloc(p,4096);
  f.put(origin+0x10,K+0x999000);f.put(origin+0x20,other);f.put(origin+0x40,uint8_t(1));
  f.put(other+0x7c,uint32_t(145));f.put(stream+0x10,K+0x999000);f.put(stream+0x30,item);f.put(stream+0x40,item);f.put(item,Ctx);
  uint32_t ctrl=1,start=0x40000000;memcpy(f.regs+8,&ctrl,4);memcpy(f.regs+12,&start,4);
  auto read=[&](uint64_t p,void *out,unsigned n){return f.read(p,out,n);};TGLHangSelect::Selection selected{};
  TGLHangSelect::select(selected,origin,stream,f.regs,read);assert(!selected.q[1] && selected.q[4]==Ring);
  uint8_t ring[0x8a8]{};assert(read(selected.q[4],ring,sizeof(ring))==sizeof(ring));
  f.pending={};TGLHangPending::collect(f.pending,selected.q[4],f.regs,ring,read);
  assert(!f.pending.q[2] && f.pending.q[11]==1);auto c=f.run();assert(!c->q[1] && c->graph.q[2]==1);
 }
 {Fixture f;auto c=f.run();assert(c->q[1]==0 && c->candidates[0][7]==0 && c->graph.q[2]==1);assert(c->q[10]==3 && c->q[12]==1 && c->q[13]==4096 && c->q[14]==4096);
  assert(!c->graph.q[5] && !c->graph.q[8]);
  if(argc==2){FILE *file=fopen(argv[1],"wb");assert(file);assert(fwrite(c.get(),1,sizeof(*c),file)==sizeof(*c));fclose(file);}
 }
 {Fixture f;f.put(TableObject,Class+1);assert(f.run()->q[1]==1);}
 {Fixture f;f.put(LRC+0x10cc,uint32_t(0x3000));auto c=f.run();assert(c->candidates[0][7]&4);assert(!c->graph.q[2]);}
 {Fixture f;f.partial=true;auto c=f.run();assert(c->candidates[0][7]&4);assert(!c->graph.q[2]);}
 {Fixture f;f.badPhysical=true;auto c=f.run();assert(c->candidates[0][7]&4);assert(c->graph.q[9]);}
 {Fixture f;f.put(K+0x2000,uint64_t(0x16003));auto c=f.run();assert(c->graph.q[9] && (c->candidates[0][7]&4));}
 {Fixture f;f.put(K+0x12000,uint64_t(0));auto c=f.run();assert(c->candidates[0][7]&4);}
 {Fixture f;f.mutateOwner=true;auto c=f.run();assert(c->q[1]==4);}
 {Fixture f;f.mutateCache=true;auto c=f.run();assert(c->candidates[0][7]&2);}
 {Fixture f;f.blocks[K+0x30000][100]^=1;auto c=f.run();assert(c->candidates[0][7]&8);assert(!c->graph.q[2]);}
 {Fixture f;f.blocks[Cache][100]^=1;auto c=f.run();assert(c->candidates[0][7]==16);}
 {Fixture f;f.put(Ctx+0xb0,Ring+8);assert(f.run()->q[1]==1);}
 {Fixture f;f.put(Ring+0x8c,uint32_t(32769));assert(f.run()->q[1]==1);}
 {Fixture f;f.put(Ring+0x38,uint32_t(64));assert(f.run()->q[1]==1);}
 {Fixture f;f.pending.q[2]=5;assert(f.run()->q[1]==1);}
 {Fixture f;f.root();auto c=f.run();assert(!c->q[1] && c->q[24]==1 && !c->q[29]);
  assert(c->q[25]==Main && c->q[26]==0 && c->q[27]==0 && c->q[28]==11);
  assert(c->candidates[0][7]&8);assert(c->graph.q[2]==1 && c->graph.draws[0].q[0]==TGLHangPending::Count);
 }
 {Fixture f;f.root();f.put(RingStorage,uint32_t(0));auto c=f.run();assert(c->q[29]==1 && !c->graph.q[2]);}
 {Fixture f;f.root();f.put(RingStorage+12,uint32_t(0x7a000204));f.put(RingStorage+16,uint32_t(0x20104594));
  auto c=f.run();assert(!c->q[1] && !c->q[29] && c->q[25]==Main && c->q[28]==11 && c->graph.q[2]==1);
 }
 for(uint32_t header:{0x7a000203U,0x7a000205U,0x7a000604U,0x7b000204U}) {
  Fixture f;f.root();f.put(RingStorage+12,header);auto c=f.run();assert(c->q[29]==2 && !c->graph.q[2]);
 }
 for(uint32_t flags:{0x20100594U,0x20108594U,0x2010c594U}) {
  Fixture f;f.root();f.put(RingStorage+12,uint32_t(0x7a000204));f.put(RingStorage+16,flags);
  auto c=f.run();assert(c->q[29]==2 && !c->graph.q[2]);
 }
 {Fixture f;f.root();f.put(RingStorage+28,uint32_t(12));auto c=f.run();assert(c->q[29]==3 && !c->graph.q[2]);}
 {Fixture f;f.root();f.put(RingStorage+20,Main);auto c=f.run();assert(c->q[29]==2 && !c->graph.q[2]);}
 {Fixture f;f.root();f.put(LRC+0x1024,uint32_t(0));auto c=f.run();assert(c->q[29]==5 && !c->graph.q[2]);}
 {Fixture f;f.root();f.mutateCache=true;auto c=f.run();assert(c->q[1]==8 && c->q[29]==4);}
 {Fixture f;f.root();f.partial=true;auto c=f.run();assert(c->graph.q[5]&TGLHangGraph::ReadHole);assert(!c->graph.q[2]);}
 printf("detail bytes=%zu graph offset=%zu pending offset=%zu tests passed\n",sizeof(Capture),offsetof(Capture,graph),offsetof(Capture,pending));
}
