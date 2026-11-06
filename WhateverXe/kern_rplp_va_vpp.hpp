// BSD-3-Clause. One byte on one page of the video driver, opt-in; no driver content is embedded, the image and both
// pages are admitted by hash only.
//
// -wex-rplp-va-vpp (Raptor Lake-P, Big Sur 20G1443): the screen recorder's pixel transfer crashes inside the video
// driver on this machine. The project's private static analysis (Q54) identified the one byte at which the driver
// refuses that transfer cleanly instead; only the identity of the image and of its page before and after, and that
// byte's offset and values, are needed here. Delivery mirrors the OpenGL URB correction: the file is read and hashed
// once at kernel start, the page is replaced only where the kernel validates exactly that page of exactly that file on
// a read-only mount, and a receipt records every outcome. The on-disk file is never written.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace RPLPVAVPP {
constexpr size_t PageSize=4096, ImageSize=8463568;
constexpr uint64_t PageOffset=0x233000;
constexpr size_t ByteOffset=0x988;
constexpr uint8_t ByteBefore=0x30, ByteAfter=0x24;
constexpr char ImageHash[]="53bd561254a999b1afa1aa8629138145ea19d22d5c02b29e39391ec9fdf2d883";
constexpr char OriginalPageHash[]="43642c673655285d3b854b230c407132b664aa6dff678e9bc192b7ba775b161d";
constexpr char CorrectedPageHash[]="695aacdad137f2e0fb309dd56900d51c8b1323b27a7d0127c826c5b4ec7870a3";
constexpr char ImagePath[]="/System/Library/Extensions/AppleIntelTGLGraphicsVADriver.bundle/Contents/MacOS/AppleIntelTGLGraphicsVADriver";
inline bool digestMatches(const uint8_t *digest,const char *hex) {
    if(!digest)return false;
    constexpr char digits[]="0123456789abcdef";
    for(unsigned i=0;i<32;++i)
        if(digits[digest[i]>>4]!=hex[2*i] || digits[digest[i]&15]!=hex[2*i+1])return false;
    return true;
}
struct Plan { uint8_t original[PageSize], corrected[PageSize]; bool ready=false; };
// Image admission is by size and full-file digest; the byte must read its known value. The caller additionally
// checks both page digests against the constants above (the runtime does), so no other page can be prepared.
inline bool prepare(Plan &p,const uint8_t *image,size_t size,const uint8_t *digest) {
    p.ready=false;
    if(!image || size!=ImageSize || !digestMatches(digest,ImageHash))return false;
    const uint32_t magic=uint32_t(image[0])|uint32_t(image[1])<<8|uint32_t(image[2])<<16|uint32_t(image[3])<<24;
    if(magic!=0xFEEDFACF || image[PageOffset+ByteOffset]!=ByteBefore)return false;
    memcpy(p.original,image+PageOffset,PageSize);memcpy(p.corrected,p.original,PageSize);
    p.corrected[ByteOffset]=ByteAfter;p.ready=true;return true;
}
enum class Result { Rejected, InvalidValidation, WrongPage, AlreadyCorrected, WriteDenied, Applied, VerifyFailed, RestoreFailed };
// The caller owns page/vnode lifetime and publication ordering. IO owns the write lock and write-state restoration.
// Only exactly the known page of the known file, fully validated, is changed, and by one byte.
template<class IO> Result apply(const Plan &p,IO &io,uint8_t *data,size_t bytes,uint64_t offset,
                               bool identity,bool armed,int validated,int tainted,int nx) {
    if(!armed || !identity || !p.ready || !data || offset!=PageOffset || bytes!=PageSize)return Result::Rejected;
    if(validated!=0xF || tainted || nx)return Result::InvalidValidation;
    if(!memcmp(data,p.corrected,PageSize))return Result::AlreadyCorrected;
    if(memcmp(data,p.original,PageSize))return Result::WrongPage;
    if(!io.begin())return Result::WriteDenied;
    Result result=Result::WrongPage;
    if(!memcmp(data,p.original,PageSize)) {
        io.copy(data+ByteOffset,p.corrected+ByteOffset,1);
        if(!memcmp(data,p.corrected,PageSize))result=Result::Applied;
        else {
            io.copy(data+ByteOffset,p.original+ByteOffset,1);
            result=!memcmp(data,p.original,PageSize)?Result::VerifyFailed:Result::RestoreFailed;
        }
    }
    io.end();return result;
}
}
