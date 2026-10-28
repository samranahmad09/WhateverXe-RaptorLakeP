// BSD-3-Clause. Exact packet-layout correction; no extracted driver payload.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace RPLPGLURB {
constexpr size_t PageSize=4096, ImageSize=22536368, Shim=0xDA0, ShimSize=351;
constexpr uint32_t Resume=0xA0C2;
constexpr char ImageHash[]="171441761bd5d456e51847b2af863b0b006b97b6741836d8e624c5cc00fc9e6a";
constexpr char OriginalPageHash[]="d47724fb31184188e694100d802fcb53a3139b9a1894094be6dbabaea688de00";
constexpr char CorrectedPageHash[]="a28b578b49bc722bb7ff01b1d9bd89c8e982f31ec84ef105bd94536fdea5f31b";
constexpr char ImagePath[]="/System/Library/Extensions/AppleIntelTGLGraphicsGLDriver.bundle/Contents/MacOS/AppleIntelTGLGraphicsGLDriver";
struct Rule { uint32_t data[4]; uint32_t stage, replacement; };
// Full header+data QWORD predicates, not a general allocation-size clamp.
constexpr Rule Rules[] {
    {{0x08030380,0x40000000,0x40000000,0x40000000},0,0x08030200},
    {{0x08030580,0x60000000,0x60000000,0x60000000},0,0x08030520},
    {{0x08030780,0x80000000,0x80000000,0x80000000},0,0x08030520},
    {{0x08000880,0x2A000000,0x2A000000,0x2A030360},3,0x2A030300}
};
constexpr uint32_t StageOffset[] {0,0xA70,0x14E0,0x1F50};
constexpr uint32_t header(unsigned i) { return 0x78300000U+(i<<16); }
inline uint32_t u32(const uint8_t *p) { return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24; }
inline uint64_t u64(const uint8_t *p) { return u32(p)|(uint64_t(u32(p+4))<<32); }
inline void put32(uint8_t *p,uint32_t v) { for(unsigned i=0;i<4;++i)p[i]=uint8_t(v>>(8*i)); }
inline bool digestMatches(const uint8_t *digest,const char *hex) {
    if(!digest)return false;
    constexpr char digits[]="0123456789abcdef";
    for(unsigned i=0;i<32;++i)
        if(digits[digest[i]>>4]!=hex[2*i] || digits[digest[i]&15]!=hex[2*i+1])return false;
    return true;
}

// Emit a bounded x86-64 program from the rules. Addresses are image-relative.
// RAX is the packet-variant pointer, RDX is scratch and is restored on all exits.
struct Emitter {
    uint8_t *out; size_t size=0; bool ok=true;
    void byte(uint8_t b) { if(size<ShimSize)out[size++]=b;else ok=false; }
    void word(uint32_t v) { for(unsigned i=0;i<4;++i)byte(uint8_t(v>>(8*i))); }
    void shortTarget(size_t pos,size_t target) {
        if(pos>=size || target<pos+1 || target-pos-1>127){ok=false;return;}
        out[pos]=uint8_t(target-pos-1);
    }
    void longTarget(size_t pos,uint32_t target) {
        if(pos+4>size){ok=false;return;}
        put32(out+pos,target-uint32_t(Shim+pos+4));
    }
};
inline bool generate(uint8_t (&out)[ShimSize]) {
    Emitter e {out};
    // mov eax,ebx; lea rax,[r15+rax*8+0x3978]; push rdx.
    e.byte(0x89);e.byte(0xD8);e.byte(0x49);e.byte(0x8D);e.byte(0x84);e.byte(0xC7);e.word(0x3978);e.byte(0x52);
    size_t doneJumps[3] {};
    for(unsigned r=0;r<4;++r) {
        size_t misses[4] {};
        for(unsigned s=0;s<4;++s) {
            e.byte(0x48);e.byte(0xBA);e.word(header(s));e.word(Rules[r].data[s]); // movabs rdx,packet
            e.byte(0x48);e.byte(0x39);e.byte(s?0x90:0x10); // cmp [rax+stage],rdx
            if(s)e.word(StageOffset[s]);
            e.byte(0x75);misses[s]=e.size;e.byte(0); // jne next rule / epilogue
        }
        e.byte(0xC7);
        if(Rules[r].stage){e.byte(0x80);e.word(StageOffset[Rules[r].stage]+4);}
        else {e.byte(0x40);e.byte(4);}
        e.word(Rules[r].replacement);
        if(r<3){e.byte(0xE9);doneJumps[r]=e.size;e.word(0);}
        for(auto pos:misses)e.shortTarget(pos,e.size);
    }
    const uint32_t done=uint32_t(Shim+e.size);
    for(auto pos:doneJumps)e.longTarget(pos,done);
    e.byte(0x5A);e.byte(0xE9);const auto back=e.size;e.word(0);e.longTarget(back,Resume);
    return e.ok && e.size==ShimSize;
}

struct Plan { uint8_t original[PageSize], corrected[PageSize]; bool ready=false; };
// Hash admission is mandatory; shape checks additionally keep all writes out of
// headers, named sections and the adjacent device selector. No unknown variant.
inline bool prepare(Plan &p,const uint8_t *image,size_t size,const uint8_t *digest) {
    p.ready=false;
    if(!image || size!=ImageSize || !digestMatches(digest,ImageHash) ||
       u32(image)!=0xFEEDFACF || u32(image+4)!=0x01000007 || u32(image+20)!=0xD60)return false;
    bool uuid=false,text=false,code=false;
    constexpr uint8_t id[] {0x55,0x2E,0xE0,0x49,0x3A,0xC4,0x31,0x10,0xBE,0xFC,0xAA,0xDF,0x64,0xFA,0x16,0xE5};
    size_t off=32;const auto commands=u32(image+16);
    if(commands>128)return false;
    for(uint32_t i=0;i<commands;++i) {
        if(off>0xD80-8)return false;
        const auto cmd=u32(image+off),len=u32(image+off+4);
        if(len<8 || len>0xD80-off)return false;
        const auto c=image+off;
        if(cmd==0x1B){if(len!=24 || uuid || memcmp(c+8,id,16))return false;uuid=true;}
        if(cmd==0x19 && len>=72 && !memcmp(c+8,"__TEXT\0",7)) {
            if(text || u64(c+24)!=0 || u64(c+40)!=0 || u64(c+48)>size || u64(c+48)<0xA0C2 ||
               u32(c+60)!=5 || u32(c+64)>(len-72)/80)return false;
            text=true;
            for(uint32_t s=0;s<u32(c+64);++s) {
                const auto sec=c+72+80*s;const auto addr=u64(sec+32),bytes=u64(sec+40);
                if(bytes && (addr<0x4160 || bytes>size || addr>size-bytes))return false;
                if(!memcmp(sec,"__text\0",7))code=addr==0x4160 && u32(sec+48)==0x4160;
            }
        }
        off+=len;
    }
    if(off!=0xD80 || !uuid || !text || !code)return false;
    if(image[0xA0B8]!=0xE9 || uint32_t(0xA0BD+u32(image+0xA0B9))!=Shim)return false;
    for(size_t i=0xA0BD;i<0xA0C2;++i)if(image[i]!=0x90)return false;
    for(size_t i=0xE53;i<0xF00;++i)if(image[i])return false;
    uint8_t program[ShimSize];if(!generate(program))return false;
    memcpy(p.original,image,PageSize);memcpy(p.corrected,image,PageSize);
    memcpy(p.corrected+Shim,program,ShimSize);p.ready=true;return true;
}

enum class Result { Rejected, InvalidValidation, WrongPage, AlreadyCorrected, WriteDenied, Applied, VerifyFailed, RestoreFailed };
// The caller owns page/vnode lifetime and publication ordering. IO owns the
// write lock and write-state restoration. It may not alter validation outputs.
template<class IO> Result apply(const Plan &p,IO &io,uint8_t *data,size_t bytes,uint64_t offset,
                               bool identity,bool armed,int validated,int tainted,int nx) {
    if(!armed || !identity || !p.ready || !data || offset || bytes!=PageSize)return Result::Rejected;
    if(validated!=0xF || tainted || nx)return Result::InvalidValidation;
    if(!memcmp(data,p.corrected,PageSize))return Result::AlreadyCorrected;
    if(memcmp(data,p.original,PageSize))return Result::WrongPage;
    if(!io.begin())return Result::WriteDenied;
    Result result=Result::WrongPage;
    if(!memcmp(data,p.original,PageSize)) {
        io.copy(data+Shim,p.corrected+Shim,ShimSize);
        if(!memcmp(data,p.corrected,PageSize))result=Result::Applied;
        else {
            io.copy(data+Shim,p.original+Shim,ShimSize);
            result=!memcmp(data,p.original,PageSize)?Result::VerifyFailed:Result::RestoreFailed;
        }
    }
    io.end();return result;
}
}
