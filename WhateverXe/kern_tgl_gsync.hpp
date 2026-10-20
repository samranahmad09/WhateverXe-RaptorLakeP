// TGL PRM Vol14 UP3/UP4 p43: disable colored GSYNC for the B0
// multicontext-preemption hang. Vol2c p1146: SCRATCH_LNCF2[2], not CSFE[2].
#ifndef kern_tgl_gsync_hpp
#define kern_tgl_gsync_hpp
#include <stdint.h>
namespace TGLGSYNC {
constexpr uint32_t Register=0xB0A0, Disable=4, Reserved=0xFF800001;
enum Status : uint32_t { Rejected, AlreadyDisabled, Applied, InvalidRegister, ReadbackMismatch, WriteInhibited };
struct Result { Status status=Rejected; uint32_t before=UINT32_MAX,after=UINT32_MAX; bool wrote=false; };
template<class Read,class Write>
Result apply(bool admitted,bool awake,bool initialize,Read read,Write write) {
 Result r;
 if(!admitted || !awake)return r;
 r.before=read(Register);r.after=r.before;
 if(r.before&Reserved) {r.status=InvalidRegister;return r;}
 if(r.before&Disable) {r.status=AlreadyDisabled;return r;}
 if(!initialize) {r.status=WriteInhibited;return r;}
 const auto value=r.before|Disable;
 write(Register,value);r.wrote=true;r.after=read(Register);
 r.status=r.after==value ? Applied:ReadbackMismatch;
 return r;
}
}
#endif
