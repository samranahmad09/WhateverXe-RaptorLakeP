#include "kern_tgl_hang_prestop.hpp"
#include <cassert>
#include <map>
#include <cstdio>
using namespace TGLHangPreStop;
int main(int argc,char **argv) {
 constexpr uint64_t owner=0xffff800000001000ULL,scheduler=owner+0x2000,
  streamer=owner+0x4000,item=owner+0x6000,context=owner+0x8000,
  ring=owner+0xa000,stamps=owner+0xc000;
 std::map<uint64_t,uint64_t> m{{scheduler+0x10,owner},{scheduler+0xca0,streamer},
  {streamer+0x10,owner},{streamer+0x30,item},{streamer+0x38,0},{streamer+0x40,0},{streamer+0x48,0},
  {item,context},{context+0x7c,6},{context+0x89,0xc0402d9319},{context+0xb0,ring},
  {context+0x6c,0},{context+0x50,owner},{ring+0x30,stamps},{ring+0x38,1},
  {ring+0x8a0,10},{ring+0x10,owner},{stamps+64,637}};
 unsigned calls=0,reads=0;uint32_t ack=2;
 auto read=[&](uint64_t a,void *p,unsigned n)->uint64_t {
  ++reads;auto it=m.find(a);if(it==m.end())return 0;memcpy(p,&it->second,n);return n;
 };
 auto mmio=[&](uint32_t r)->uint32_t {++calls;return r==0xd84?ack:r;};
 Capture c{};collect(c,owner,scheduler,read,mmio);
 assert(c.q[0]==2 && c.q[1]==0 && c.q[6]==streamer && !c.q[10] && !c.q[11]);
 assert(calls==65 && c.slots[0][0][2]==6 && c.slots[0][0][7]==637);
 for(unsigned i=12;i<16;++i)assert(!c.q[i]);
 c={};calls=0;ack=UINT32_MAX;collect(c,owner,scheduler,read,mmio,true);
 assert(c.q[1]==2 && calls==1 && !c.q[12]);
 ack=2;unsigned gtCalls=0;uint32_t gtAck=2;
 auto withGT=[&](uint32_t reg)->uint32_t {
  if(reg==0x130044) {++gtCalls;return gtAck;}
  if(reg==0xCEC4) {++gtCalls;return 0x113;}
  if(reg==0xCEB8) {++gtCalls;return 0x12345678;}
  if(reg==0xCEBC) {++gtCalls;return 0x12;}
  if(reg==0xB464) {++gtCalls;return 2;}
  return mmio(reg);
 };
 c={};collect(c,owner,scheduler,read,withGT,false);assert(!gtCalls && !c.q[12]);
 c={};collect(c,owner,scheduler,read,withGT,true);
 assert(gtCalls==8 && c.q[1]==0 && c.q[12]==0x200000002ULL);
 assert(c.q[13]==0x11300000113ULL && c.q[14]==0x1212345678ULL && c.q[15]==0x200000002ULL);
 static_assert(sizeof(Capture)==1152,"pre-STOP wire extent unchanged");
 if(argc==2) {auto f=fopen(argv[1],"wb");assert(f);assert(fwrite(&c,1,sizeof(c),f)==sizeof(c));assert(!fclose(f));}
 for(uint32_t invalid:{0U,UINT32_MAX}) {
  c={};gtCalls=0;gtAck=invalid;collect(c,owner,scheduler,read,withGT,true);
  assert(gtCalls==1 && c.q[12]==invalid && !c.q[13] && !c.q[14] && !c.q[15]);
 }
 c={};calls=0;ack=0;collect(c,owner,scheduler,read,mmio);assert(c.q[1]==2 && calls==1);
 c={};calls=0;collect(c,owner,0,read,mmio);assert(c.q[1]==1 && calls==0);
 c={};calls=0;ack=2;m[streamer+0x10]=owner+1;
 collect(c,owner,scheduler,read,mmio);assert(c.q[1]==1 && calls==0);m[streamer+0x10]=owner;
 c={};m[ring+0x38]=64;collect(c,owner,scheduler,read,mmio);assert(c.q[1]==4 && c.q[10]==2);
 m[ring+0x38]=1;c={};unsigned count=0;
 auto changing=[&](uint64_t a,void *p,unsigned n)->uint64_t {
  if(a==stamps+64)m[a]=++count;
  return read(a,p,n);
 };
 collect(c,owner,scheduler,changing,mmio);assert(c.q[1]==0 && c.q[11]==1);
 c={};count=0;
 auto ownerChanges=[&](uint64_t a,void *p,unsigned n)->uint64_t {
  if(a==scheduler+0x10 && ++count==2)m[a]=owner+1;
  return read(a,p,n);
 };
 collect(c,owner,scheduler,ownerChanges,mmio);assert(c.q[1]==3);
}
