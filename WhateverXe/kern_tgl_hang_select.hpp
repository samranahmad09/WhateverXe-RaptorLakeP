// A native dump callback may carry another engine's channel while reporting
// RCS registers. Select only a unique, owned RCS context matching the saved ID.
#ifndef kern_tgl_hang_select_hpp
#define kern_tgl_hang_select_hpp
#include "kern_tgl_hang_pending.hpp"
namespace TGLHangSelect {
struct Selection { uint64_t q[8]; }; // schema, guard, origin, streamer, ring, context, ID, source
template<class Read>
void select(Selection &s,uint64_t origin,uint64_t streamer,const uint8_t *regs,Read read) {
 s={};s.q[0]=1;s.q[1]=1;s.q[2]=origin;s.q[3]=streamer;
 s.q[6]=(TGLHangPending::u32(regs+0xac)>>5)&2047;
 auto get=[&](uint64_t p,uint64_t offset,uint64_t &v,unsigned n=8) {
  v=0;return TGLHangPending::kernelRange(p,offset+n) && read(p+offset,&v,n)==n;
 };
 uint64_t owner=0,context=0;
 if(regs[0x94] || !(TGLHangPending::u32(regs+8)&1) || !TGLHangPending::u32(regs+12) ||
    !get(origin,0x10,owner) || !owner)return;
 auto matches=[&](uint64_t ctx,uint64_t ring)->bool {
  uint64_t id=0,engine=0,o=0,back=0,rctx=0,re=0,ro=0;
  return get(ctx,0x7c,id,4) && id==s.q[6] && get(ctx,0x6c,engine,1) && !engine &&
   get(ctx,0x50,o) && o==owner && get(ctx,0xb0,back) && back==ring &&
   get(ring,0x20,rctx) && rctx==ctx && get(ring,0x40,re,1) && !re &&
   get(ring,0x10,ro) && ro==owner;
 };
 if(get(origin,0x20,context) && matches(context,origin)) {
  s.q[4]=origin;s.q[5]=context;s.q[7]=1;s.q[1]=0;return;
 }
 uint64_t so=0,slots[4]{},again[4]{},items[4]{};
 s.q[1]=2;
 if(!get(streamer,0x10,so) || so!=owner ||
    !TGLHangPending::kernelRange(streamer,0x50) || read(streamer+0x30,slots,32)!=32)return;
 for(unsigned i=0;i<4;++i) {
  if(!slots[i])continue;
  uint64_t ctx=0,id=0,ring=0;
  s.q[1]=3;if(!get(slots[i],0,ctx))return;items[i]=ctx;
  if(!get(ctx,0x7c,id,4))return;
  if(id!=s.q[6])continue;
  if(!get(ctx,0xb0,ring) || !matches(ctx,ring))return;
  s.q[1]=4;if(s.q[4] && (s.q[4]!=ring || s.q[5]!=ctx))return;
  s.q[4]=ring;s.q[5]=ctx;
 }
 s.q[1]=5;if(!s.q[4])return;
 s.q[1]=6;
 if(read(streamer+0x30,again,32)!=32 || memcmp(slots,again,32) ||
    !get(streamer,0x10,so) || so!=owner || !get(origin,0x10,so) || so!=owner)return;
 for(unsigned i=0;i<4;++i)if(slots[i]) {
  uint64_t ctx=0;if(!get(slots[i],0,ctx) || ctx!=items[i])return;
 }
 if(!matches(s.q[5],s.q[4]))return;
 s.q[7]=2;s.q[1]=0;
}
}
#endif
