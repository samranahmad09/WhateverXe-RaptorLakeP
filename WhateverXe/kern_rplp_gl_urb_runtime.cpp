// BSD-3-Clause. Fixed-image, opt-in page corrections on the kernel's page validation: the OpenGL URB allocation
// correction (-wex-rplp-gl-urb) and the video driver's post-processing refusal (-wex-rplp-va-vpp). Validation is never
// waived; one route serves both; each target keeps its own state and receipts.
#include "kern_rplp_gl_urb_runtime.hpp"
#include "kern_rplp_gl_urb.hpp"
#include "kern_rplp_va_vpp.hpp"
#include "kern_xelp_os.hpp"
#include <Headers/kern_patcher.hpp>
#include <Headers/kern_disasm.hpp>
#include <Headers/kern_crypto.hpp>
#include <Headers/kern_file.hpp>
#include <IOKit/IORegistryEntry.h>
#include <kern/thread_call.h>
#include <mach/vm_param.h>
#include <sys/vnode.h>
#include <sys/mount.h>

namespace {
constexpr size_t PageSize=RPLPGLURB::PageSize;
static_assert(RPLPVAVPP::PageSize==PageSize,"page size");
// The two result enumerations are kept in the same order so one counter layout serves both targets.
static_assert(unsigned(RPLPVAVPP::Result::RestoreFailed)==unsigned(RPLPGLURB::Result::RestoreFailed) &&
              unsigned(RPLPVAVPP::Result::Applied)==unsigned(RPLPGLURB::Result::Applied) &&
              unsigned(RPLPVAVPP::Result::VerifyFailed)==unsigned(RPLPGLURB::Result::VerifyFailed),"result layout");
enum class Setup : uint32_t {
    Disabled, WrongProfile, OverrideConflict, WrongKernel, FileUnavailable,
    NotReadOnly, WrongImage, NoPublisher, UnsupportedEntry, RouteFailed, Armed, Halted
};
enum class Kind : unsigned { GL, VA, Count };
struct Target {
    const char *path,*statusKey,*readyKey,*appliedKey,*imageKey,*originalKey,*correctedKey;
    const char *imageHash,*originalHash,*correctedHash;
    size_t imageSize;uint64_t pageOffset;
    vnode_t vnode;uint32_t generation;bool armed;uint32_t setup;uint64_t counters[8];
};
Target targets[unsigned(Kind::Count)] {
    {RPLPGLURB::ImagePath,"RPLPGLURBStatus","RPLPGLURBReady","RPLPGLURBApplied",
     "RPLPGLURBImageSHA256","RPLPGLURBOriginalPageSHA256","RPLPGLURBCorrectedPageSHA256",
     RPLPGLURB::ImageHash,RPLPGLURB::OriginalPageHash,RPLPGLURB::CorrectedPageHash,RPLPGLURB::ImageSize,0,
     nullptr,0,false,0,{}},
    {RPLPVAVPP::ImagePath,"RPLPVAVPPStatus","RPLPVAVPPReady","RPLPVAVPPApplied",
     "RPLPVAVPPImageSHA256","RPLPVAVPPOriginalPageSHA256","RPLPVAVPPCorrectedPageSHA256",
     RPLPVAVPP::ImageHash,RPLPVAVPP::OriginalPageHash,RPLPVAVPP::CorrectedPageHash,RPLPVAVPP::ImageSize,RPLPVAVPP::PageOffset,
     nullptr,0,false,0,{}}
};
RPLPGLURB::Plan glPlan;
RPLPVAVPP::Plan vaPlan;
mach_vm_address_t originalValidate;
thread_call_t publisher;
bool routed,routeAttempted;
const char *osBuild;         // the kernel's OS build string storage; userspace fills it after the kernel has started
uint64_t kernelRecord[6] {1}; // RPLPPageKernel: schema, page size ok, kernel major ok, exact version string ok, build
                              // string storage resolved, its first eight bytes when this plugin was set up

void publish(thread_call_param_t,thread_call_param_t) {
    // May allocate here; never publish from inside the VM callback/write lock.
    auto root=IORegistryEntry::getRegistryRoot();
    root->setProperty("RPLPPageKernel",kernelRecord,sizeof(kernelRecord));
    root->setProperty("RPLPPageBuild",!osBuild ? "unresolved" : osBuild[0] ? osBuild : "empty");
    for(auto &t:targets) {
        if(!t.setup && !t.armed)continue;
        uint64_t record[10] {1,__atomic_load_n(&t.setup,__ATOMIC_ACQUIRE)};
        for(unsigned i=0;i<8;++i)record[2+i]=__atomic_load_n(t.counters+i,__ATOMIC_RELAXED);
        root->setProperty(t.statusKey,record,sizeof(record));
        root->setProperty(t.readyKey,__atomic_load_n(&t.armed,__ATOMIC_ACQUIRE));
        root->setProperty(t.appliedKey,record[2+unsigned(RPLPGLURB::Result::Applied)],64);
    }
}
void state(Target &t,Setup value) {
    __atomic_store_n(&t.setup,uint32_t(value),__ATOMIC_RELEASE);
    if(publisher)thread_call_enter(publisher);else publish(nullptr,nullptr);
}
struct WritePage {
    vnode_t vp;uint32_t generation;const bool *armed;
    bool begin() {
        if(MachInfo::setKernelWriting(true,KernelPatcher::kernelWriteLock)!=KERN_SUCCESS)return false;
        if(!__atomic_load_n(armed,__ATOMIC_ACQUIRE) || vnode_vid(vp)!=generation ||
           !vfs_isrdonly(vnode_mount(vp))) {
            MachInfo::setKernelWriting(false,KernelPatcher::kernelWriteLock);return false;
        }
        return true;
    }
    void copy(uint8_t *to,const uint8_t *from,size_t n) { lilu_os_memcpy(to,from,n); }
    void end() { MachInfo::setKernelWriting(false,KernelPatcher::kernelWriteLock); }
};

// XNU 7195.141.49: NOT Lilu's legacy five-argument, boolean page wrapper.
// The original fills all three outputs; its verdict is passed through unchanged.
void validate(vnode_t vp,memory_object_t pager,memory_object_offset_t offset,
              const void *data,int *validated,int *tainted,int *nx) {
    FunctionCast(validate,originalValidate)(vp,pager,offset,data,validated,tainted,nx);
    for(unsigned k=0;k<unsigned(Kind::Count);++k) {
        auto &t=targets[k];
        if(!__atomic_load_n(&t.armed,__ATOMIC_ACQUIRE) || vp!=t.vnode || offset!=t.pageOffset)continue;
        // A retained usecount prevents vnode reuse. These accessors do not do I/O.
        const bool identity=vnode_vid(vp)==t.generation && vfs_isrdonly(vnode_mount(vp)) && buildAccepted();
        WritePage io {vp,t.generation,&t.armed};
        auto page=const_cast<uint8_t *>(static_cast<const uint8_t *>(data));
        const unsigned result=k==unsigned(Kind::GL)
            ? unsigned(RPLPGLURB::apply(glPlan,io,page,PageSize,offset,identity,true,*validated,*tainted,*nx))
            : unsigned(RPLPVAVPP::apply(vaPlan,io,page,PageSize,offset,identity,true,*validated,*tainted,*nx));
        __atomic_fetch_add(t.counters+result,uint64_t(1),__ATOMIC_RELAXED);
        if(result==unsigned(RPLPGLURB::Result::VerifyFailed) || result==unsigned(RPLPGLURB::Result::RestoreFailed)) {
            __atomic_store_n(&t.armed,false,__ATOMIC_RELEASE);
            __atomic_store_n(&t.setup,uint32_t(Setup::Halted),__ATOMIC_RELEASE);
        }
        // A preallocated deferred call is the only notification on this path.
        thread_call_enter(publisher);
        // Never publish a page with an unknown partial replacement. Ordinary
        // precondition failures return the original page; failed rollback cannot.
        if(result==unsigned(RPLPGLURB::Result::RestoreFailed))panic("WhateverXe: page restoration failed");
    }
}

bool hashMatches(const uint8_t *data,uint32_t size,const char *expected) {
    auto digest=Crypto::hash(data,size);
    const bool good=RPLPGLURB::digestMatches(digest,expected);
    if(digest)Buffer::deleter(digest);
    return good;
}

Setup prepareFile(Kind kind) {
    auto &t=targets[unsigned(kind)];
    const auto ctxt=vfs_context_create(nullptr);
    if(!ctxt)return Setup::FileUnavailable;
    vnode_t vp=nullptr;uint8_t *file=nullptr;Setup result=Setup::FileUnavailable;
    if(!vnode_lookup(t.path,VNODE_LOOKUP_NOFOLLOW,&vp,ctxt)) {
        if(!vnode_isreg(vp) || !vfs_isrdonly(vnode_mount(vp)))result=Setup::NotReadOnly;
        else if(FileIO::readFileSize(vp,ctxt)!=t.imageSize)result=Setup::WrongImage;
        else if((file=Buffer::create<uint8_t>(t.imageSize))) {
            if(!FileIO::readFileData(file,0,t.imageSize,vp,ctxt)) {
                auto digest=Crypto::hash(file,uint32_t(t.imageSize));
                const uint8_t *original=kind==Kind::GL ? glPlan.original : vaPlan.original;
                const uint8_t *corrected=kind==Kind::GL ? glPlan.corrected : vaPlan.corrected;
                const bool prepared=kind==Kind::GL ? RPLPGLURB::prepare(glPlan,file,t.imageSize,digest)
                                                  : RPLPVAVPP::prepare(vaPlan,file,t.imageSize,digest);
                const bool good=prepared && hashMatches(original,PageSize,t.originalHash) &&
                    hashMatches(corrected,PageSize,t.correctedHash);
                if(digest)Buffer::deleter(digest);
                result=Setup::WrongImage;
                if(good && vfs_isrdonly(vnode_mount(vp)) && !vnode_ref(vp)) {
                    t.vnode=vp;t.generation=vnode_vid(vp);result=Setup::Armed;
                }
            }
        }
        vnode_put(vp);
    }
    if(file)Buffer::deleter(file);
    vfs_context_rele(ctxt);return result;
}

bool routeable(mach_vm_address_t address) {
    if(!address)return false;
    // Decline pre-existing routes. Do not replace an unknown foreign trampoline.
    const uint8_t prefix[] {0x55,0x48,0x89,0xE5};
    if(memcmp(reinterpret_cast<const void *>(address),prefix,sizeof(prefix)))return false;
    size_t span=0;
    while(span<14) {
        Disassembler::hde_t h {};
        const auto len=Disassembler::hdeDisasm(address+span,&h);
        if(!len || len>15 || (h.flags&(F_ERROR|F_RELATIVE)) ||
           ((h.flags&F_MODRM) && h.modrm_mod==0 && h.modrm_rm==5))return false;
        // Position-independent stack/register prologue only; no branches/calls.
        if(!((h.opcode>=0x50 && h.opcode<=0x57) || h.opcode==0x89 ||
             h.opcode==0x8B || h.opcode==0x81 || h.opcode==0x83))return false;
        span+=len;
    }
    return span<=32;
}

// Whether the kernel is the one the images, offsets and the validation entry were established on. The kernel build is
// pinned by its exact version string. The OS build string is not available this early (userspace sets it after the
// kernel has started; round 61 found it empty here and nothing armed), so it is compared where a page is validated.
bool kernelAccepted(KernelPatcher &p) {
    auto version=reinterpret_cast<const char *>(p.solveSymbol(KernelPatcher::KernelID,"_version"));
    p.clearError();
    osBuild=reinterpret_cast<const char *>(p.solveSymbol(KernelPatcher::KernelID,"_osversion"));
    p.clearError();
    const bool pageOK=page_size==PageSize,majorOK=getKernelVersion()==KernelVersion::BigSur,
        versionOK=XELP::kernelOS(version)==XELP::OS::BigSur;
    kernelRecord[1]=pageOK;kernelRecord[2]=majorOK;kernelRecord[3]=versionOK;kernelRecord[4]=osBuild!=nullptr;
    if(osBuild)lilu_os_memcpy(kernelRecord+5,osBuild,sizeof(uint64_t));
    return pageOK && majorOK && versionOK;
}
// At validation time userspace has long set the build string. An unresolvable symbol leaves the exact kernel
// version and the image and page digests as the identity; a resolved string must read the expected build.
bool buildAccepted() { return !osBuild || !memcmp(osBuild,"20G1443",8); }

// Prepare one target: its own admission, its file, and the shared route once.
void arm(KernelPatcher &p,Kind kind,bool requested,bool physicalReady,bool kernelOK) {
    auto &t=targets[unsigned(kind)];
    if(!requested)return;
    if(!physicalReady){state(t,Setup::WrongProfile);return;}
    uint32_t overrideValue=0;
    if(kind==Kind::GL && PE_parse_boot_argn("wex-rplp-urb-banks",&overrideValue,sizeof(overrideValue))) {
        state(t,Setup::OverrideConflict);return;
    }
    if(!kernelOK){state(t,Setup::WrongKernel);return;}
    if(!routed) {
        const auto entry=p.solveSymbol(KernelPatcher::KernelID,"_cs_validate_page");
        p.clearError();
        if(!routeable(entry)){state(t,Setup::UnsupportedEntry);return;}
    }
    const auto prepared=prepareFile(kind);
    if(prepared!=Setup::Armed){state(t,prepared);return;}
    if(!publisher)publisher=thread_call_allocate(publish,nullptr);
    if(!publisher){vnode_rele(t.vnode);t.vnode=nullptr;state(t,Setup::NoPublisher);return;}
    if(!routed) {
        if(routeAttempted){state(t,Setup::RouteFailed);return;}
        // All immutable data, original/replacement pages and worker state exist
        // before the route. Lilu stores the original trampoline before installing it.
        KernelPatcher::RouteRequest request {"_cs_validate_page",validate,originalValidate};
        routeAttempted=true;routed=p.routeMultipleLong(KernelPatcher::KernelID,&request,1);
        p.clearError();
        if(!routed) {
            // A failed route is not permission to free state a partial route could
            // reference. Keep it inert and retained; no unsupported unload path.
            state(t,Setup::RouteFailed);return;
        }
    }
    auto root=IORegistryEntry::getRegistryRoot();
    root->setProperty(t.imageKey,t.imageHash);
    root->setProperty(t.originalKey,t.originalHash);
    root->setProperty(t.correctedKey,t.correctedHash);
    __atomic_store_n(&t.armed,true,__ATOMIC_RELEASE);state(t,Setup::Armed);
    // The vnode usecount, page buffers and deferred call live as long as this
    // non-unloadable Lilu plugin. No late rearming or live executable-page edits.
}
}

void RPLPGLURBRuntime::processKernel(KernelPatcher &p,bool physicalReady) {
    const bool gl=checkKernelArgument("-wex-rplp-gl-urb"),va=checkKernelArgument("-wex-rplp-va-vpp");
    if(!gl && !va)return;
    const bool kernelOK=kernelAccepted(p);
    arm(p,Kind::GL,gl,physicalReady,kernelOK);
    arm(p,Kind::VA,va,physicalReady,kernelOK);
}
