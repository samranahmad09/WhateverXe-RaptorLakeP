#include "kern_tgl_context_cache.hpp"
#include <cassert>
#include <array>
int main() {
 std::array<uint8_t,81920> image{},before{};std::array<uint8_t,8192> page{};
 const uint64_t gpu=0x402b3000;const uint32_t fields[]={0x21C0,uint32_t(gpu|5),0x21C4,0,0x21C8,0};
 memcpy(image.data()+0x1048,fields,sizeof(fields));before=image;
 assert(TGLContextCache::install(image.data(),image.size(),gpu));
 for(size_t i=0;i<image.size();++i)if((i<0x1054||i>=0x1058)&&(i<0x105C||i>=0x1060))assert(image[i]==before[i]);
 auto installed=image;assert(TGLContextCache::install(image.data(),image.size(),gpu));assert(image==installed);
 for(unsigned field:{0U,1U,2U,3U,4U,5U}) {
  image=before;uint32_t bad=0x12345678;memcpy(image.data()+0x1048+field*4,&bad,4);auto copy=image;
  assert(!TGLContextCache::install(image.data(),image.size(),gpu));assert(image==copy);
 }
 for(uint64_t bad:{0ULL,0x402b3001ULL,0xFFFFF000ULL,0x100000000ULL})assert(!TGLContextCache::install(before.data(),before.size(),bad));
 assert(!TGLContextCache::install(before.data(),0x105F,gpu));
 page.fill(0xA5);TGLContextCache::batch(page.data());uint32_t words[16];memcpy(words,page.data()+4096,64);
 assert(words[0]==0x11000001 && words[1]==0x20D8 && words[2]==0x00400040);
 for(unsigned i=3;i<16;++i)assert(!words[i]);
 for(unsigned i=0;i<8192;++i)if(i<4096||i>=4160)assert(page[i]==0xA5);
 for(uint32_t old=0;old<65536;++old)assert(((old&~(words[2]>>16))|(words[2]&0xffff))==(old|0x40));
}
