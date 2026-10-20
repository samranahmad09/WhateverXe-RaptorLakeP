#include "kern_tgl_submit_history.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <vector>
using namespace TGLSubmitHistory;
int main() {
 History<8,2> h{};Record r{},out{};
 for(unsigned i=0;i<19;++i) {r.q[3]=i;h.append(r);}
 assert(h.attempts==19 && !h.drops);
 for(unsigned i=0;i<8;++i) {
  const auto seq=h.copy(h.recent[i],out);
  assert(seq>=12 && seq<=19 && seq-1==out.q[3] && (seq-1)%8==i);
 }
 for(unsigned i=0;i<2;++i)assert(h.copy(h.early[i],out)==i+1 && out.q[3]==i);
 h.recent[3].busy=1;h.append(r);assert(h.drops==1);
 assert(!h.copy(h.recent[3],out));h.recent[3].busy=0;
 h.freeze(123);h.append(r);assert(h.attempts==20 && h.frozenAt==123);
 assert(!h.put(h.recent[0],100,r));assert(h.copy(h.recent[0],out)==17);

 // Exact donor-shaped memory, including both banks and a shared active item.
 std::vector<uint8_t> memory(0x30000);
 auto put=[&](uint64_t p,uint64_t value,unsigned n=8){memcpy(memory.data()+p,&value,n);};
 auto read=[&](uint64_t p,void *to,unsigned n)->unsigned {
  if(p+n>memory.size())return 0;memcpy(to,memory.data()+p,n);return n;
 };
 constexpr uint64_t owner=0x100,streamer=0x200,desc=0x300,item=0x400,ctx=0x500;
 constexpr uint64_t buffer=0x600,ring=0x1000,stamps=0x2000,image=0x10000;
 put(streamer+0x10,owner);put(streamer+0x28,desc);put(desc,0,1);
 put(streamer+0x30,item);put(streamer+0x40,item);put(item,ctx);
 put(ctx+0x50,owner);put(ctx+0x6c,0,1);put(ctx+0x89,0xc0402d9319);
 put(ctx+0x98,buffer);put(ctx+0xb0,ring);put(ctx+0x7c,6,4);
 put(buffer+0x20,81920);put(buffer+0x38,image);
 put(image+0x101c,0x4010,4);put(image+0x12d8,0x2084,4);put(image+0x12dc,0xffff0446,4);
 put(ring+0x10,owner);put(ring+0x30,stamps);put(ring+0x38,1,4);
 put(stamps+64,768,4);put(ring+0x8a0,769,4);
 for(unsigned bank=0;bank<2;++bank) {
  r={};r.q[5]=owner;r.q[6]=streamer;put(streamer+0x50,bank,4);
  contextSlots(r,read);assert(!r.q[9] && !r.q[10] && r.q[8]==bank);
  assert(r.slots[0][5]==0xffff0446 && r.slots[0][6]==768 && r.slots[0][7]==769);
  assert(!memcmp(r.slots[0],r.slots[2],sizeof(r.slots[0])) && !r.slots[1][0]);
 }
 auto reject=[&](){r={};r.q[5]=owner;r.q[6]=streamer;contextSlots(r,read);assert(r.q[9]);};
 put(image+0x12d8,0x2080,4);reject();put(image+0x12d8,0x2084,4);
 put(buffer+0x20,4096);reject();put(buffer+0x20,81920);
 put(ring+0x38,64,4);reject();put(ring+0x38,1,4);
 put(ctx+0x50,owner+1);reject();put(ctx+0x50,owner);
 put(streamer+0x50,2,4);reject();put(streamer+0x50,0,4);
 put(desc,1,1);reject();put(desc,0,1);
 put(buffer+0x38,memory.size());reject();
 puts("PASS bounded history: overwrite, early receipts, busy omission, freeze, donor slots and failed ownership/layout/read guards");
}
