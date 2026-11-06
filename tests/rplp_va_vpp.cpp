// Synthetic image and delivery fixtures for the video driver's one-byte page correction. No Apple image is embedded.
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include "kern_rplp_va_vpp.hpp"
using namespace RPLPVAVPP;
struct Writer {
    bool allowed=true,locked=false,failFirst=false,failRestore=false,changeAtLock=false;
    unsigned writes=0,ends=0;uint8_t *page=nullptr;
    bool begin(){if(changeAtLock)page[12]^=1;return locked=allowed;}
    void copy(uint8_t *to,const uint8_t *from,size_t n) {
        assert(locked && to==page+ByteOffset && n==1);++writes;
        if((writes==1 && failFirst) || (writes>1 && failRestore)){to[0]^=1;return;}
        memcpy(to,from,n);
    }
    void end(){assert(locked);locked=false;++ends;}
};
static std::vector<uint8_t> image() {
    std::vector<uint8_t> b(ImageSize);
    b[0]=0xCF;b[1]=0xFA;b[2]=0xED;b[3]=0xFE;
    // An artificial page is sufficient for these delivery tests; production additionally requires the independently
    // computed full-file and page digests.
    for(size_t i=0;i<PageSize;++i)b[PageOffset+i]=uint8_t(i*7+3);
    b[PageOffset+ByteOffset]=ByteBefore;
    return b;
}
static void hexDigest(const char *hex,uint8_t (&out)[32]) {
    for(unsigned i=0;i<32;++i) {unsigned v=0;assert(sscanf(hex+2*i,"%2x",&v)==1);out[i]=uint8_t(v);}
}
template<class W> static Result attempt(Plan &p,W &w,uint8_t *page,int validated=15,int tainted=0,int nx=0,
                      bool identity=true,bool armed=true,uint64_t offset=PageOffset,size_t size=PageSize) {
    w.page=page;return apply(p,w,page,size,offset,identity,armed,validated,tainted,nx);
}
int main() {
    uint8_t digest[32];hexDigest(ImageHash,digest);auto b=image();Plan p{};
    assert(ByteBefore!=ByteAfter && ByteOffset<PageSize && PageOffset+PageSize<=ImageSize);
    assert(prepare(p,b.data(),b.size(),digest));assert(p.ready);
    assert(!memcmp(p.original,b.data()+PageOffset,PageSize));
    unsigned differences=0;
    for(size_t i=0;i<PageSize;++i)differences+=p.original[i]!=p.corrected[i];
    assert(differences==1 && p.original[ByteOffset]==ByteBefore && p.corrected[ByteOffset]==ByteAfter);
    // Admission: the magic, the byte's known value, the digest, the size and the pointers.
    const size_t corruptions[] {0,3,PageOffset+ByteOffset};
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
    // Identity, arming, another page of the file (offset 0 is not the page), the wrong size.
    for(unsigned kind=0;kind<7;++kind) {
        memcpy(page,p.original,PageSize);Writer w;
        auto r=attempt(p,w,page,15,0,0,kind!=0,kind!=1,kind==2?0:kind==3?PageOffset+PageSize:kind==4?UINT64_MAX:PageOffset,
                       kind==5?PageSize-1:kind==6?SIZE_MAX:PageSize);
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
    struct Broken : Writer {void copy(uint8_t *to,const uint8_t *,size_t){assert(locked);to[writes++ ? 1 : 0]^=1;}} bad;
    assert(attempt(p,bad,page)==Result::RestoreFailed && bad.ends==1);
    puts("PASS RPL-P VA VPP: preparation, the one-byte page, all 4096 page mutations, validation/identity/offset/size rejection, repeat loads, write/rollback failures");
}
