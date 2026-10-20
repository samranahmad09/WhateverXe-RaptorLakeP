// Sequential witnesses after native wake acquisition, before its STOP request.
#ifndef kern_tgl_hang_prestop_hpp
#define kern_tgl_hang_prestop_hpp
#include <stdint.h>
#include <string.h>
namespace TGLHangPreStop {
// Read-only position/status registers, no FIFO/pop or context-only registers.
constexpr uint32_t Registers[] {0x2030,0x2034,0x2038,0x203c,0x205c,0x2064,
 0x2068,0x206c,0x2074,0x209c,0x20b0,0x2110,0x2140,0x2144,0x2168,
 0x2180,0x21c0,0x21c4,0x21c8,0x2234,0x2238,0x2244,0x20bc,
 0x2510,0x2514,0x2580,0xd84,0x229c,0x2424,0x2270,0x2274,0x21a8};
struct Capture {
 uint64_t q[16]; // schema, guard, begin/end ns, owner/scheduler/streamer, engine/domains, ACK, row errors, changed
 // Schema2 q12..15: paired GT ACK, paired Gen12 fault, fault VA data,
 // paired CS/LTISEQ flush requests. Each pair is before | (after << 32).
 uint32_t registers[2][32];
 uint64_t slots[2][4][12]; // item, context, ID, descriptor, ring, stamps, index, retired, write index, engine, context owner, ring owner
};
template<class Read,class MMIO>
void collect(Capture &c,uint64_t owner,uint64_t scheduler,Read read,MMIO mmio,bool gtLease=false) {
 c.q[0]=2;c.q[1]=1;c.q[4]=owner;c.q[5]=scheduler;
 auto field=[&](uint64_t p,unsigned offset,uint64_t &v,unsigned n=8) {
  v=0;return (p>>47)==0x1ffff && p+offset+n>=p && read(p+offset,&v,n)==n;
 };
 uint64_t so=0,streamer=0;
 if(!owner || !field(scheduler,0x10,so) || so!=owner ||
    !field(scheduler,0xca0,streamer) || !field(streamer,0x10,so) || so!=owner)return;
 c.q[6]=streamer;c.q[1]=2;c.q[9]=mmio(0xd84);
 if(c.q[9]==UINT32_MAX || !(c.q[9]&2))return;
 // Only a caller already owning native GT forcewake may read these registers.
 // Render ACK or another client's GT ACK is not ownership of a GT lease.
 bool gt=false;
 if(gtLease) {
  c.q[12]=mmio(0x130044);
  gt=c.q[12]!=UINT32_MAX && (c.q[12]&2);
  if(gt) {
   c.q[13]=mmio(0xCEC4); // Gen12, not the legacy fault register at4094.
   const auto lo=mmio(0xCEB8),hi=mmio(0xCEBC);
   c.q[14]=uint64_t(lo)|(uint64_t(hi)<<32);
   c.q[15]=mmio(0xB464); // CS/LTISEQ flush message, read does not clear.
  }
 }
 for(unsigned pass=0;pass<2;++pass) {
  for(unsigned i=0;i<32;++i)c.registers[pass][i]=mmio(Registers[i]);
  for(unsigned i=0;i<4;++i) {
   auto r=c.slots[pass][i];
   if(!field(streamer,0x30+i*8,r[0])) {++c.q[10];continue;}
   if(!r[0])continue;
   if(!field(r[0],0,r[1]) || !field(r[1],0x7c,r[2],4) ||
      !field(r[1],0x89,r[3]) || !field(r[1],0xb0,r[4]) ||
      !field(r[4],0x30,r[5]) || !field(r[4],0x38,r[6],4) || r[6]>=64 ||
      !field(r[5],unsigned(r[6]*64),r[7],4) || !field(r[4],0x8a0,r[8],4) ||
      !field(r[1],0x6c,r[9],1) || !field(r[1],0x50,r[10]) || r[10]!=owner ||
      !field(r[4],0x10,r[11]) || r[11]!=owner)++c.q[10];
  }
 }
 c.q[11]=memcmp(c.slots[0],c.slots[1],sizeof(c.slots[0]))!=0;
 if(gt) {
  c.q[15]|=uint64_t(mmio(0xB464))<<32;
  c.q[13]|=uint64_t(mmio(0xCEC4))<<32;
  c.q[12]|=uint64_t(mmio(0x130044))<<32;
 }
 uint64_t again=0;
 c.q[1]=!field(scheduler,0x10,so) || so!=owner ||
  !field(scheduler,0xca0,again) || again!=streamer ||
  !field(streamer,0x10,so) || so!=owner ? 3 : (c.q[10] ? 4 : 0);
}
}
#endif
