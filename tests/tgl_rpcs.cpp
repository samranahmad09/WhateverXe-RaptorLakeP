#include "kern_tgl_rpcs.hpp"
#include <cassert>
#include <map>
using namespace TGLRPCS;
int main() {
 static_assert(FullSlice==0x80041000 && sizeof(Find)==11 && sizeof(Replace)==11);
 static_assert(sizeof(Receipt)==144 && sizeof(State)==496);
 for(unsigned i=0;i<7;++i)assert(Find[i]==Replace[i]);
 uint32_t old=0,now=0;memcpy(&old,Find+7,4);memcpy(&now,Replace+7,4);
 assert(old==0x80000000 && now==FullSlice && (old^now)==0x41000);
 constexpr uint64_t owner=0xffff800000001000ULL,context=owner+0x2000,
  buffer=owner+0x4000,image=owner+0x10000;
 std::map<uint64_t,uint64_t> m{{context+0x50,owner},{context+0x6c,0},{context+0x7c,6},
  {context+0x89,0xc040324319},{context+0x98,buffer},{buffer+0x20,81920},{buffer+0x38,image},
  {image+0x1104,0x11080001},{image+0x1108,0x20c8},{image+0x110c,FullSlice},{image+0x1110,0x61040001}};
 auto read=[&](uint64_t p,void *out,unsigned n)->uint64_t {
  auto it=m.find(p);if(it==m.end())return 0;memcpy(out,&it->second,n);return n;
 };
 unsigned calls=0;uint32_t ack=2,rcu=0;
 auto mmio=[&](uint32_t r)->uint32_t {
  ++calls;assert(r==0xd84||r==0x148c8||r==0x20c8);return r==0xd84?ack:r==0x20c8?FullSlice:rcu;
 };
 Receipt c;collect(c,owner,context,read,mmio);
 assert(!c.q[0] && c.q[2]==image && c.q[6]==FullSlice && !c.q[8] && !c.q[9] && calls==6);
 assert(c.q[16]==FullSlice && c.q[17]==FullSlice);
 for(uint32_t bad:{0U,UINT32_MAX}) {
  c={};calls=0;ack=bad;collect(c,owner,context,read,mmio);assert(c.q[0]==2 && calls==1);
 }
 ack=2;rcu=0x80000000;c={};collect(c,owner,context,read,mmio);assert(!c.q[0] && c.q[8]==rcu);
 rcu=UINT32_MAX;c={};collect(c,owner,context,read,mmio);assert(c.q[0]==4);rcu=0;
 m[context+0x50]=owner+1;c={};collect(c,owner,context,read,mmio);assert(c.q[0]==1);m[context+0x50]=owner;
 m[image+0x1108]=0x148c8;c={};collect(c,owner,context,read,mmio);assert(c.q[0]==1);m[image+0x1108]=0x20c8;
 c={};unsigned reads=0;
 auto race=[&](uint64_t p,void *out,unsigned n)->uint64_t {
  if(p==image+0x110c && ++reads==2)m[p]=0x80000000;
  return read(p,out,n);
 };
 collect(c,owner,context,race,mmio);assert(c.q[0]==3);
}
