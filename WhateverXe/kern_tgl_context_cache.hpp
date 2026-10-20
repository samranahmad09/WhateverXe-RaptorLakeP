// Gen12 Wa_18022495364: invalidate instruction/state cache during RCS restore.
// Linux v6.12 intel_lrc.c uses a cacheline-sized INDIRECT_CTX at offset 0xD.
#ifndef kern_tgl_context_cache_hpp
#define kern_tgl_context_cache_hpp
#include <stdint.h>
#include <stddef.h>
#include <string.h>
namespace TGLContextCache {
constexpr unsigned BatchOffset=4096, BatchBytes=64, RestoreOffset=0xD<<6;
inline void batch(void *page) {
 uint32_t words[16] {0x11000001,0x20D8,0x00400040};
 // INDIRECT_CTX is length-delimited, with NOOP padding, not MI_BATCH_BUFFER_END.
 memcpy(static_cast<uint8_t *>(page)+BatchOffset,words,sizeof(words));
}
inline bool install(void *image,size_t size,uint64_t gpu) {
 if(!image || size<0x1060 || !gpu || (gpu&4095) || gpu>0xFFFFE000ULL)return false;
 auto p=static_cast<uint8_t *>(image);uint32_t w[6];memcpy(w,p+0x1048,sizeof(w));
 const uint32_t pointer=static_cast<uint32_t>(gpu)+BatchOffset+1;
 if(w[0]!=0x21C0 || w[1]!=(gpu|5) || w[2]!=0x21C4 || w[4]!=0x21C8 ||
    (w[3] && w[3]!=pointer) || (w[5] && w[5]!=RestoreOffset))return false;
 memcpy(p+0x1054,&pointer,4);memcpy(p+0x105C,&RestoreOffset,4);return true;
}
}
#endif
