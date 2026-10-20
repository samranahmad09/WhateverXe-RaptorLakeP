#include "kern_tgl_hang_graph.hpp"
#include <cassert>
#include <memory>
#include <vector>
#include <map>
#include <cstdio>
using namespace TGLHangGraph;
namespace {
constexpr uint64_t Main=0x40000000,External=0x41000000,Code=0x42000000,Surfaces=0x43000000,Dynamic=0x44000000;
using Words=std::vector<uint32_t>;
void add(Words &v,std::initializer_list<uint32_t> w){v.insert(v.end(),w);}
void setup(Words &v,uint64_t code=Code) {
 Words s(22);s[0]=0x61010014;s[4]=Surfaces|1;s[6]=Dynamic|1;s[10]=code|1;v.insert(v.end(),s.begin(),s.end());
 add(v,{0x78100007,0x240,0,0,0,0,0x200800,0x88400405,0x210000});
 add(v,{0x7820000a,0x40,0,0,0,0,0x1f800627,0x60b06,0x140,0,0xc0,0});
 add(v,{0x782a0000,0x20,0x782f0000,0});
}
void draw(Words &v,unsigned count=3){add(v,{0x7b000008,0,count,0,1,0,0,0,0,0});}
struct Fixture {
 std::map<uint64_t,std::vector<uint8_t>> pages;
 bool partial{},mismatch{};unsigned reads{};
 Fixture(){for(uint64_t a:{Code,Code+4096,Code+8192,Surfaces,Dynamic})pages[a].resize(4096);}
 void external(const Words &v,uint64_t base=External,unsigned limit=16384) {
  for(uint64_t page=base&~4095ULL;page<base+limit;page+=4096)pages[page].resize(4096);
  assert(v.size()*4<=limit);
  for(unsigned i=0;i<v.size();++i)memcpy(pages[(base+i*4)&~4095ULL].data()+((base+i*4)&4095),&v[i],4);
 }
 void read(Page &p) {
  ++reads;auto it=pages.find(p.q[0]);if(it==pages.end()){p.q[4]=1;return;}
  p.q[2]=partial?4095:4096;p.q[3]=!partial;p.q[5]=1;p.q[6]=mismatch;
  memcpy(p.bytes,it->second.data(),p.q[2]);
 }
 std::unique_ptr<Capture> run(const Words &main) {
  auto c=std::make_unique<Capture>();auto e=std::make_unique<TGLHangPending::Entry>();
  e->gpu=Main;e->dwords=main.size();e->copied=main.size()*4;e->stable=1;assert(e->copied<=sizeof(e->bytes));memcpy(e->bytes,main.data(),e->copied);
  auto read=[&](Page &p){this->read(p);};Walker<decltype(read)> w{*c,read};w.candidate(0,*e);return c;
 }
 std::unique_ptr<Capture> mapped() {
  auto c=std::make_unique<Capture>();auto read=[&](Page &p){this->read(p);};
  Walker<decltype(read)> w{*c,read};w.mappedCandidate(4,Main);return c;
 }
};
}
int main(int argc,char **argv) {
 {Fixture f;Words root;setup(root);draw(root,3);root.resize(0x6800/4);
  draw(root,6);add(root,{0x05000000});f.external(root,Main,MappedRootBytes);
  auto c=f.mapped();assert(!c->q[5] && c->q[2]==2 && c->q[12]==root.size()*4);
  assert(c->draws[1].q[1]==Main+0x6800 && c->draws[1].raw[2]==6);
  f.pages.erase(Main+0x4000);c=f.mapped();assert(c->q[5]&ReadHole);assert(c->q[2]==1);
 }
 {Fixture f;Words root;
  for(unsigned i=0;i<33;++i){f.external({0x05000000},External+i*64);add(root,{0x18b00101,uint32_t(External+i*64),0});}
  add(root,{0x05000000});f.external(root,Main,MappedRootBytes);auto c=f.mapped();
  assert(c->q[3]==MaxCalls && c->q[4]==MaxTargets && (c->q[5]&CallLimit));
  root.resize(32*3);add(root,{0x05000000});f.external(root,Main,MappedRootBytes);c=f.mapped();
  assert(c->q[3]==32 && c->q[4]==32 && !c->q[5]);
 }
 {Fixture f;f.external(Words(MappedRootBytes/4),Main,MappedRootBytes);auto c=f.mapped();
  assert(c->q[5]==NoEnd && c->q[12]==MappedRootBytes && c->q[7]==MappedRootBytes/4);
 }
 {Fixture f;Words ext;setup(ext);add(ext,{0x05000000});f.external(ext);
  Words main{0x18c00101,uint32_t(External),0};for(unsigned i=0;i<4;++i)draw(main,i+3);add(main,{0x05000000});
  auto c=f.run(main);assert(c->q[2]==4 && c->q[3]==1 && !c->q[5]);
  for(unsigned i=0;i<4;++i){assert(c->draws[i].q[5]==Code);assert(c->draws[i].raw[2]==i+3);}
  assert(c->q[1]==9); // Four external, three instruction, sampler and table pages.
 }
 {Fixture f;Words ext;setup(ext);add(ext,{0x05000000});f.external(ext);
  Words main{0x18c00101,uint32_t(External),0};draw(main);setup(main,Code+4096);draw(main);add(main,{0x05000000});
  f.pages[Code+12288].resize(4096);auto c=f.run(main);assert(c->q[2]==2 && c->draws[0].q[5]==Code && c->draws[1].q[5]==Code+4096);
 }
 {Fixture f;Words ext;setup(ext);add(ext,{0x05000000});f.external(ext);Words main{0x18c00101,uint32_t(External),0};draw(main);add(main,{0x05000000});
  f.partial=true;auto c=f.run(main);assert(c->q[5]&ReadHole);assert(!c->draws[0].q[2]);
 }
 {Fixture f;Words ext;setup(ext);add(ext,{0x05000000});f.external(ext);Words main{0x18c00101,uint32_t(External),0};draw(main);add(main,{0x05000000});
  f.mismatch=true;auto c=f.run(main);assert(c->q[5]&ReadHole);assert(!c->draws[0].q[2]);
 }
 {Fixture f;f.external({0x18b00101,uint32_t(External),0,0x05000000});auto c=f.run({0x18b00101,uint32_t(External),0,0x05000000});assert(c->q[5]&Cycle);}
 {Fixture f;f.external({0x18b00101,uint32_t(External+0x10000),0,0x05000000});f.external({0x18b00101,uint32_t(External+0x20000),0,0x05000000},External+0x10000);
  auto c=f.run({0x18b00101,uint32_t(External),0,0x05000000});assert(c->q[5]&Depth);}
 {Fixture f;auto c=f.run({0x18800001,uint32_t(External),0,0x05000000});assert(c->q[5]&UnsupportedCall);}
 {Fixture f;auto c=f.run({0xffffffff});assert(c->q[5]&Truncated || c->q[5]&UnknownCommand);}
 {Fixture f;auto c=f.run({0x11000005,0});assert(c->q[5]&Truncated);}
 {Fixture f;auto c=f.run({0});assert(c->q[5]&NoEnd);}
 {Fixture f;Words main;setup(main);for(unsigned i=0;i<65;++i)draw(main);add(main,{0x05000000});auto c=f.run(main);assert(c->q[2]==64 && (c->q[5]&DrawLimit));}
 {Fixture f;auto c=std::make_unique<Capture>();auto read=[&](Page &p){f.read(p);};Walker<decltype(read)> w{*c,read};
  for(unsigned i=0;i<MaxPages+1;++i)w.page(Code+i*4096,Shader);assert(c->q[1]==MaxPages && c->q[6]==1 && (c->q[5]&PageLimit));}
 {Fixture f;Words ext;setup(ext);draw(ext,7);add(ext,{0x05000000});f.external(ext);
  Words main{0x18b00101,uint32_t(External),0};draw(main,9);add(main,{0x05000000});auto c=f.run(main);
  assert(c->q[2]==2 && c->draws[0].q[20]==2 && c->draws[0].q[5]==Code);
  assert(c->draws[1].q[20]==1 && c->draws[1].q[5]==0 && !c->draws[1].q[2]);
  assert(c->q[5]&MissingState); // POSH state cannot supply inherited RCS state.
 }
 {Fixture f;Words ext;draw(ext,7);add(ext,{0x05000000});f.external(ext);
  Words main;setup(main);add(main,{0x18800101,uint32_t(External),0});draw(main,9);add(main,{0x05000000});auto c=f.run(main);
  assert(c->q[2]==1 && c->draws[0].raw[2]==7 && c->draws[0].q[5]==Code && c->calls[0].q[7]==3);
 }
 // Optional local captured main buffers. Synthetic external state exercises
 // inheritance only; it is deliberately not claimed to be the missing bytes.
 for(int i=1;i<argc;++i) {
  FILE *file=fopen(argv[i],"rb");assert(file);std::vector<uint8_t> bytes(16385);size_t n=fread(bytes.data(),1,bytes.size(),file);fclose(file);assert(n && n<=16384 && !(n&3));
  Words main(n/4);memcpy(main.data(),bytes.data(),n);assert(main[0]==0x18b00101);
  Fixture f;Words ext;setup(ext);add(ext,{0x05000000});f.external(ext,main[1]);
  auto c=f.run(main);assert(c->q[2]>0);assert(!(c->q[5]&(UnknownCommand|Truncated|Depth|Cycle|NoEnd|UnsupportedCall)));
  for(unsigned j=0;j<c->q[2];++j)assert(c->draws[j].q[20]==1);
  if(n==2308){assert(c->q[2]==4 && (c->q[5]&MissingState));for(unsigned j=0;j<c->q[2];++j)assert(!(c->draws[j].q[2]&SurfaceBase));}
  if(n==2320){assert(c->q[2]==1 && !(c->q[5]&MissingState));assert(c->draws[0].q[2]&SurfaceBase);}
  printf("fixture %d bytes=%zu draws=%llu calls=%llu pages=%llu errors=%llu\n",i,n,c->q[2],c->q[3],c->q[1],c->q[5]);
 }
 printf("graph bytes=%zu page=%zu draw=%zu tests passed\n",sizeof(Capture),sizeof(Page),sizeof(Draw));
}
