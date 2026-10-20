#include "kern_tgl_same_tail.hpp"
#include <array>
#include <cassert>
#include <cstring>
#include <cstdio>
int main() {
 std::array<uint8_t,0x80> streamer{},desc{},buffer{};
 std::array<uint8_t,0x100> context{};
 std::array<uint8_t,32> item{},other{};
 std::array<uint8_t,81920> image{};
 const uint64_t owner=123,sp=reinterpret_cast<uint64_t>(streamer.data());
 auto put=[](auto &b,unsigned off,uint64_t v,unsigned n=8){memcpy(b.data()+off,&v,n);};
 put(streamer,0x10,owner);put(streamer,0x28,reinterpret_cast<uint64_t>(desc.data()));
 put(item,0,reinterpret_cast<uint64_t>(context.data()));
 put(context,0x50,owner);put(context,0x89,0xc040324319);put(context,0x98,reinterpret_cast<uint64_t>(buffer.data()));
 put(buffer,0x20,image.size());put(buffer,0x38,reinterpret_cast<uint64_t>(image.data()));
 uint64_t fail=0;unsigned writes=0;
 auto read=[&](uint64_t p,void *out,unsigned n)->unsigned {
  if(p==fail)return 0;memcpy(out,reinterpret_cast<void *>(p),n);return n;
 };
 auto force=[&](uint64_t p){assert(p==reinterpret_cast<uint64_t>(context.data())+0xc9);++writes;};
 auto ip=reinterpret_cast<uint64_t>(item.data());
 for(unsigned bank=0;bank<2;++bank) {
  put(streamer,0x50,bank,4);put(streamer,0x30+bank*16,ip);put(streamer,0x30+(1-bank)*16,ip);
  put(image,0x101c,0x3040,4);TGLSameTail::State state{};writes=0;
  auto run=[&](unsigned epoch=1){TGLSameTail::prepare(state,owner,sp,epoch,read,force);};
  run();assert(!writes);run();assert(writes==1 && state.counts[5]==1 && state.first.tail==0x3040);
  // Native flag already set: no extra write; normal forward progress: no force.
  put(context,0xc9,1,1);run();assert(writes==1 && state.counts[6]==1);put(context,0xc9,0,1);
  put(image,0x101c,0x3050,4);run();assert(writes==1);
  run(2);assert(writes==1);run(2);assert(writes==2);
  put(streamer,0x30+bank*16,reinterpret_cast<uint64_t>(other.data()));run(2);assert(writes==2);
  put(streamer,0x30+bank*16,ip);
  // A failed read invalidates history; the next valid call cannot match stale data.
  fail=reinterpret_cast<uint64_t>(image.data())+0x101c;run(2);assert(writes==2 && state.counts[7]==1);
  fail=0;run(2);assert(writes==2);run(2);assert(writes==3);
  // Same context/TAIL but a newly queued item does not match the prior submission.
  other=item;auto op=reinterpret_cast<uint64_t>(other.data());
  put(streamer,0x30+bank*16,op);put(streamer,0x30+(1-bank)*16,op);run(2);assert(writes==3);
  put(desc,0,1,1);run(2);assert(writes==3);put(desc,0,0,1);
  put(context,0x50,999);run(2);assert(writes==3);put(context,0x50,owner);
  put(buffer,0x20,4096);run(2);assert(writes==3);put(buffer,0x20,image.size());
  put(image,0x101c,0x3051,4);run(2);assert(writes==3);
  put(image,0x101c,0x3050,4);put(streamer,0x50,2,4);run(2);assert(writes==3);
 }
 puts("PASS same-tail: both banks, advancing tail, active-item identity, epoch, prior native flag, failed reads and layout guards");
}
