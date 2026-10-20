// Failure-only copies of recent unretired submissions. These are candidates,
// not an active-BB claim, a GPU-visible snapshot, or permission to execute data.
#ifndef kern_tgl_hang_pending_hpp
#define kern_tgl_hang_pending_hpp
#include <stdint.h>
#include <string.h>
namespace TGLHangPending {
constexpr unsigned Count=4, Bytes=16384;
struct Entry {
 uint64_t index,stamp,gpu,cpu,dwords,copied,stable,reserved;
 uint8_t bytes[Bytes];
};
struct Capture {
 // version, time, guard, ring, context, hardware ID, owner ID, stamp index,
 // retired before/after, write index, candidate count, oversize count, reserved.
 uint64_t q[16];
 Entry entries[Count];
};
inline uint64_t u64(const uint8_t *p) {uint64_t v;memcpy(&v,p,8);return v;}
inline uint32_t u32(const uint8_t *p) {uint32_t v;memcpy(&v,p,4);return v;}
inline bool kernelRange(uint64_t p,uint64_t n) {
 return n && (p>>47)==0x1ffff && p<=UINT64_MAX-(n-1);
}
template<class Read>
void collect(Capture &c,uint64_t address,const uint8_t *regs,const uint8_t *ring,Read read) {
 c.q[0]=1;c.q[3]=address;c.q[4]=u64(ring+0x20);
 c.q[5]=(u32(regs+0xac)>>5)&2047;c.q[7]=u32(ring+0x38);c.q[10]=u32(ring+0x8a0);
 auto copy=[&](uint64_t p,void *out,unsigned n)->bool {
  return kernelRange(p,n) && read(p,out,n)==n;
 };
 c.q[2]=1;
 if(regs[0x94] || ring[0x40] || c.q[7]>=64 || c.q[10]>=64 ||
    !kernelRange(address,0x8a8) || !kernelRange(c.q[4],0x80)) return;
 uint32_t owner=0,retired=0;const uint64_t stamps=u64(ring+0x30);
 c.q[2]=2;
 if(!copy(c.q[4]+0x7c,&owner,4) || !kernelRange(stamps,4096) ||
    !copy(stamps+c.q[7]*64,&retired,4)) return;
 c.q[6]=owner;c.q[8]=retired;c.q[2]=3;
 if(owner!=c.q[5]) return;
 for(unsigned n=0;n<64 && c.q[11]<Count;++n) {
  const unsigned i=(static_cast<unsigned>(c.q[10])+63-n)&63;
  const uint8_t *src=ring+0xa0+i*32;
  const uint32_t stamp=u32(src),words=u32(src+24),delta=stamp-retired;
  const uint64_t gpu=u64(src+8),cpu=u64(src+16),size=static_cast<uint64_t>(words)*4;
  if(!delta || delta>=0x80000000U || !size || !gpu || gpu>>47 ||
     size>0x800000000000ULL-gpu || !kernelRange(cpu,size)) continue;
  if(size>Bytes) {++c.q[12];continue;}
  auto &e=c.entries[c.q[11]++];
  e.index=i;e.stamp=stamp;e.gpu=gpu;e.cpu=cpu;e.dwords=words;
  // Check the cache record before dereferencing its CPU buffer, then again
  // afterwards. nofault tolerates a concurrent unmap; it does not pin storage.
  uint8_t before[32],after[32];
  if(!copy(address+0xa0+i*32,before,32) || memcmp(before,src,32)) continue;
  e.copied=read(cpu,e.bytes,static_cast<unsigned>(size));
  if(e.copied>size) {e.copied=0;continue;}
  e.stable=e.copied==size && copy(address+0xa0+i*32,after,32) && !memcmp(before,after,32);
 }
 c.q[2]=4;
 uint64_t context=0,stampBase=0;uint32_t index=0,write=0,finalOwner=0,finalRetired=0;
 if(!copy(address+0x20,&context,8) || !copy(address+0x30,&stampBase,8) ||
    !copy(address+0x38,&index,4) || !copy(address+0x8a0,&write,4) ||
    !copy(c.q[4]+0x7c,&finalOwner,4) || !copy(stamps+c.q[7]*64,&finalRetired,4)) return;
 c.q[9]=finalRetired;c.q[2]=5;
 if(context!=c.q[4] || stampBase!=stamps || index!=c.q[7] || write!=c.q[10] ||
    finalOwner!=owner || finalRetired!=retired) return;
 c.q[2]=0;
}
}
#endif
