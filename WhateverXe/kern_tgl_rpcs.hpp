#ifndef kern_tgl_rpcs_hpp
#define kern_tgl_rpcs_hpp
#include <stdint.h>
#include <string.h>
namespace TGLRPCS {
// Linux v6.12 intel_sseu.c: TGL has one slice, slice-level PG only.
// intel_sseu_make_rpcs requests ENABLE | S_CNT_ENABLE | one Gen11+ slice.
constexpr uint32_t FullSlice=(1U<<31)|(1U<<18)|(1U<<12);
// Exact constructor store to the unsubmitted RCS image, not CPU MMIO.
constexpr uint8_t Find[] {0x41,0xc7,0x87,0x0c,0x11,0,0,0,0,0,0x80};
constexpr uint8_t Replace[] {0x41,0xc7,0x87,0x0c,0x11,0,0,0,0x10,0x04,0x80};
struct Receipt {
 uint64_t q[18] {1}; // guard, context, image, descriptor, ID, LRI, RPCS, CSR,
 // RCU before/after, render ACK before/after, ordinal, phase, begin/end ns,
 // RCS power-clock MMIO word before/after (not a decoded slice-status report).
};
struct State {
 // schema, recorded samples, valid, unexpected RPCS, RCU override, invalid,
 // snapshot guard, expected RPCS. Receipt reads are sequential, not atomic.
 uint64_t q[8] {1,0,0,0,0,0,0,FullSlice};
 Receipt first{},last{},failure{};
};
template<class Read,class MMIO>
void collect(Receipt &c,uint64_t owner,uint64_t context,Read read,MMIO mmio) {
 auto q=c.q;q[0]=1;q[1]=context;q[10]=mmio(0xd84);
 if(q[10]==UINT32_MAX || !(q[10]&2)) {q[0]=2;return;}
 // Gen12 14800..14fff uses the already-owned render wake domain.
 q[8]=mmio(0x148c8);q[16]=mmio(0x20c8);
 auto finish=[&] {
  q[17]=mmio(0x20c8);q[9]=mmio(0x148c8);q[11]=mmio(0xd84);
  if(q[11]==UINT32_MAX || !(q[11]&2))q[0]=2;
  if(q[8]==UINT32_MAX || q[9]==UINT32_MAX || q[16]==UINT32_MAX || q[17]==UINT32_MAX)q[0]=4;
 };
 auto field=[&](uint64_t p,unsigned off,uint64_t &v,unsigned n=8) {
  v=0;return (p>>47)==0x1ffff && p+off+n>=p && read(p+off,&v,n)==n;
 };
 uint64_t actual=0,engine=255,buffer=0,size=0,reg=0;
 if(!owner || !field(context,0x50,actual) || actual!=owner ||
    !field(context,0x6c,engine,1) || engine || !field(context,0x7c,q[4],4) ||
    !field(context,0x89,q[3]) || !(q[3]&1) ||
    !field(context,0x98,buffer) || !field(buffer,0x20,size) || size!=81920 ||
    !field(buffer,0x38,q[2]) || (q[2]&4095) ||
    !field(q[2],0x1104,q[5],4) || (q[5]!=0x11000001 && q[5]!=0x11080001) ||
    !field(q[2],0x1108,reg,4) || reg!=0x20c8 ||
    !field(q[2],0x110c,q[6],4) || !field(q[2],0x1110,q[7],4)) {finish();return;}
 uint64_t again=0;
 q[0]=!field(context,0x50,again) || again!=owner ||
  !field(context,0x89,again) || again!=q[3] ||
  !field(buffer,0x38,again) || again!=q[2] ||
  !field(q[2],0x110c,again,4) || again!=q[6] ? 3 : 0;
 finish();
}
}
#endif
