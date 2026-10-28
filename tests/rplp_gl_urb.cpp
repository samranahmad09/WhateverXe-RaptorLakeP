// Synthetic Mach-O and VM delivery fixtures. No Apple image is embedded.
#include <cassert>
#include <cstdio>
#include <vector>
#include "kern_rplp_gl_urb.hpp"
using namespace RPLPGLURB;
struct Writer {
    bool allowed=true,locked=false,failFirst=false,failRestore=false,changeAtLock=false;
    unsigned writes=0,ends=0;uint8_t *page=nullptr;
    bool begin(){if(changeAtLock)page[12]^=1;return locked=allowed;}
    void copy(uint8_t *to,const uint8_t *from,size_t n) {
        assert(locked && to==page+Shim && n==ShimSize);++writes;
        if((writes==1 && failFirst) || (writes>1 && failRestore)){to[0]^=1;return;}
        memcpy(to,from,n);
    }
    void end(){assert(locked);locked=false;++ends;}
};
static std::vector<uint8_t> image() {
    std::vector<uint8_t> b(ImageSize);put32(b.data(),0xFEEDFACF);put32(b.data()+4,0x01000007);
    put32(b.data()+16,3);put32(b.data()+20,0xD60);
    auto c=b.data()+32;put32(c,0x19);put32(c+4,152);memcpy(c+8,"__TEXT",6);
    put32(c+32,0x1000000);put32(c+48,0x1000000);put32(c+60,5);put32(c+64,1);
    auto s=c+72;memcpy(s,"__text",6);memcpy(s+16,"__TEXT",6);put32(s+32,0x4160);
    put32(s+40,0x100000);put32(s+48,0x4160);
    c+=152;put32(c,0x1B);put32(c+4,24);
    const uint8_t id[] {0x55,0x2E,0xE0,0x49,0x3A,0xC4,0x31,0x10,0xBE,0xFC,0xAA,0xDF,0x64,0xFA,0x16,0xE5};
    memcpy(c+8,id,16);c+=24;put32(c,0x777);put32(c+4,uint32_t(b.data()+0xD80-c));
    b[0xA0B8]=0xE9;put32(b.data()+0xA0B9,uint32_t(Shim-0xA0BD));
    for(unsigned i=0xA0BD;i<0xA0C2;++i)b[i]=0x90;
    // An artificial old shim is sufficient for these delivery tests; production
    // additionally requires the independently computed full-file/page digests.
    for(size_t i=Shim;i<0xE53;++i)b[i]=uint8_t(i);
    return b;
}
static void hexDigest(const char *hex,uint8_t (&out)[32]) {
    for(unsigned i=0;i<32;++i) {unsigned v=0;assert(sscanf(hex+2*i,"%2x",&v)==1);out[i]=uint8_t(v);}
}
template<class W> static Result attempt(Plan &p,W &w,uint8_t *page,int validated=15,int tainted=0,int nx=0,
                      bool identity=true,bool armed=true,uint64_t offset=0,size_t size=PageSize) {
    w.page=page;return apply(p,w,page,size,offset,identity,armed,validated,tainted,nx);
}
int main(int argc,char **argv) {
    uint8_t program[ShimSize];assert(generate(program));
    if(argc==2) {FILE *f=fopen(argv[1],"wb");assert(f);assert(fwrite(program,1,sizeof(program),f)==sizeof(program));fclose(f);}
    uint8_t digest[32];hexDigest(ImageHash,digest);auto b=image();Plan p{};
    assert(prepare(p,b.data(),b.size(),digest));assert(p.ready);
    assert(!memcmp(p.corrected+Shim,program,ShimSize));
    assert(!memcmp(p.corrected,p.original,Shim));
    assert(!memcmp(p.corrected+Shim+ShimSize,p.original+Shim+ShimSize,PageSize-Shim-ShimSize));
    const size_t corruptions[] {0,4,16,20,32+4,32+60,32+72+32,32+152+8,0xA0B8,0xA0B9,0xA0BF,0xE53};
    for(auto pos:corruptions) {b[pos]^=1;assert(!prepare(p,b.data(),b.size(),digest));assert(!p.ready);b[pos]^=1;}
    digest[0]^=1;assert(!prepare(p,b.data(),b.size(),digest));digest[0]^=1;
    assert(!prepare(p,b.data(),b.size()-1,digest));assert(!prepare(p,nullptr,b.size(),digest));
    assert(!prepare(p,b.data(),b.size(),nullptr));assert(prepare(p,b.data(),b.size(),digest));
    uint8_t page[PageSize];
    for(int v: {0,1,7,15})for(int t: {0,1,15})for(int nx: {0,1,15}) {
        memcpy(page,p.original,PageSize);Writer w;
        auto r=attempt(p,w,page,v,t,nx);
        assert(r==((v==15 && !t && !nx)?Result::Applied:Result::InvalidValidation));
        assert(w.writes==(r==Result::Applied?1U:0U));assert(!w.locked);
    }
    for(unsigned kind=0;kind<6;++kind) {
        memcpy(page,p.original,PageSize);Writer w;
        auto r=attempt(p,w,page,15,0,0,kind!=0,kind!=1,kind==2?4096:kind==3?UINT64_MAX:0,
                       kind==4?PageSize-1:kind==5?SIZE_MAX:PageSize);
        assert(r==Result::Rejected && !w.writes && !memcmp(page,p.original,PageSize));
    }
    for(size_t i=0;i<PageSize;++i) {
        memcpy(page,p.original,PageSize);page[i]^=1;Writer w;
        assert(attempt(p,w,page)==Result::WrongPage && !w.writes);
    }
    memcpy(page,p.corrected,PageSize);Writer done;
    assert(attempt(p,done,page)==Result::AlreadyCorrected && !done.writes);
    assert(attempt(p,done,page,15,1)==Result::InvalidValidation && !done.writes);
    for(unsigned repeat=0;repeat<3;++repeat) {
        memcpy(page,p.original,PageSize);Writer w;
        assert(attempt(p,w,page)==Result::Applied && w.writes==1 && w.ends==1);
        assert(!memcmp(page,p.corrected,PageSize));
    }
    memcpy(page,p.original,PageSize);Writer denied;denied.allowed=false;
    assert(attempt(p,denied,page)==Result::WriteDenied && !denied.writes && !denied.ends);
    memcpy(page,p.original,PageSize);Writer changed;changed.changeAtLock=true;
    assert(attempt(p,changed,page)==Result::WrongPage && !changed.writes && changed.ends==1);
    memcpy(page,p.original,PageSize);Writer failed;failed.failFirst=true;
    assert(attempt(p,failed,page)==Result::VerifyFailed && failed.writes==2 && failed.ends==1);
    assert(!memcmp(page,p.original,PageSize));
    memcpy(page,p.original,PageSize);
    // Corrupt a different byte on rollback to exercise restoration failure.
    struct Broken : Writer {void copy(uint8_t *to,const uint8_t *,size_t){assert(locked);to[writes++]^=1;}} bad;
    assert(attempt(p,bad,page)==Result::RestoreFailed && bad.ends==1);
    puts("PASS RPL-P GL URB: preparation, all 4096 page mutations, validation/identity/size rejection, repeat loads, write/rollback failures");
}
