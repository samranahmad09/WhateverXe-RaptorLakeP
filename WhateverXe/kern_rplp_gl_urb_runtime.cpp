// BSD-3-Clause. A fixed-image, opt-in page correction. Validation is never waived.
#include "kern_rplp_gl_urb_runtime.hpp"
#include "kern_rplp_gl_urb.hpp"
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
using namespace RPLPGLURB;
enum class Setup : uint32_t {
    Disabled, WrongProfile, OverrideConflict, WrongKernel, FileUnavailable,
    NotReadOnly, WrongImage, NoPublisher, UnsupportedEntry, RouteFailed, Armed, Halted
};
Plan plan;
vnode_t target;
uint32_t targetGeneration;
mach_vm_address_t originalValidate;
thread_call_t publisher;
bool armed;
uint32_t setup;
uint64_t counters[8] {};

void publish(thread_call_param_t,thread_call_param_t) {
    // May allocate here; never publish from inside the VM callback/write lock.
    uint64_t record[10] {1,__atomic_load_n(&setup,__ATOMIC_ACQUIRE)};
    for(unsigned i=0;i<8;++i)record[2+i]=__atomic_load_n(counters+i,__ATOMIC_RELAXED);
    auto root=IORegistryEntry::getRegistryRoot();
    root->setProperty("RPLPGLURBStatus",record,sizeof(record));
    root->setProperty("RPLPGLURBReady",__atomic_load_n(&armed,__ATOMIC_ACQUIRE));
    root->setProperty("RPLPGLURBApplied",record[2+unsigned(Result::Applied)],64);
}
void state(Setup value) {
    __atomic_store_n(&setup,uint32_t(value),__ATOMIC_RELEASE);
    if(publisher)thread_call_enter(publisher);else publish(nullptr,nullptr);
}
struct WritePage {
    vnode_t vp;
    bool begin() {
        if(MachInfo::setKernelWriting(true,KernelPatcher::kernelWriteLock)!=KERN_SUCCESS)return false;
        if(!__atomic_load_n(&armed,__ATOMIC_ACQUIRE) || vnode_vid(vp)!=targetGeneration ||
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
    if(offset || !__atomic_load_n(&armed,__ATOMIC_ACQUIRE) || vp!=target)return;
    // A retained usecount prevents vnode reuse. These accessors do not do I/O.
    const bool identity=vnode_vid(vp)==targetGeneration && vfs_isrdonly(vnode_mount(vp));
    WritePage io {vp};
    const auto result=apply(plan,io,const_cast<uint8_t *>(static_cast<const uint8_t *>(data)),
                            PageSize,offset,identity,true,*validated,*tainted,*nx);
    __atomic_fetch_add(counters+unsigned(result),uint64_t(1),__ATOMIC_RELAXED);
    if(result==Result::VerifyFailed || result==Result::RestoreFailed) {
        __atomic_store_n(&armed,false,__ATOMIC_RELEASE);
        __atomic_store_n(&setup,uint32_t(Setup::Halted),__ATOMIC_RELEASE);
    }
    // A preallocated deferred call is the only notification on this path.
    thread_call_enter(publisher);
    // Never publish a page with an unknown partial replacement. Ordinary
    // precondition failures return the original page; failed rollback cannot.
    if(result==Result::RestoreFailed)panic("WhateverXe: GL URB page restoration failed");
}

bool hashMatches(const uint8_t *data,uint32_t size,const char *expected) {
    auto digest=Crypto::hash(data,size);
    const bool good=digestMatches(digest,expected);
    if(digest)Buffer::deleter(digest);
    return good;
}

Setup prepareFile() {
    const auto ctxt=vfs_context_create(nullptr);
    if(!ctxt)return Setup::FileUnavailable;
    vnode_t vp=nullptr;uint8_t *file=nullptr;Setup result=Setup::FileUnavailable;
    if(!vnode_lookup(ImagePath,VNODE_LOOKUP_NOFOLLOW,&vp,ctxt)) {
        if(!vnode_isreg(vp) || !vfs_isrdonly(vnode_mount(vp)))result=Setup::NotReadOnly;
        else if(FileIO::readFileSize(vp,ctxt)!=ImageSize)result=Setup::WrongImage;
        else if((file=Buffer::create<uint8_t>(ImageSize))) {
            if(!FileIO::readFileData(file,0,ImageSize,vp,ctxt)) {
                auto digest=Crypto::hash(file,uint32_t(ImageSize));
                const bool good=prepare(plan,file,ImageSize,digest) &&
                    hashMatches(plan.original,PageSize,OriginalPageHash) &&
                    hashMatches(plan.corrected,PageSize,CorrectedPageHash);
                if(digest)Buffer::deleter(digest);
                result=Setup::WrongImage;
                if(good && vfs_isrdonly(vnode_mount(vp)) && !vnode_ref(vp)) {
                    target=vp;targetGeneration=vnode_vid(vp);result=Setup::Armed;
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
}

void RPLPGLURBRuntime::processKernel(KernelPatcher &p,bool physicalReady) {
    if(!checkKernelArgument("-wex-rplp-gl-urb"))return;
    if(!physicalReady){state(Setup::WrongProfile);return;}
    uint32_t overrideValue=0;
    if(PE_parse_boot_argn("wex-rplp-urb-banks",&overrideValue,sizeof(overrideValue))) {
        state(Setup::OverrideConflict);return;
    }
    auto version=reinterpret_cast<const char *>(p.solveSymbol(KernelPatcher::KernelID,"_version"));
    p.clearError();
    auto build=reinterpret_cast<const char *>(p.solveSymbol(KernelPatcher::KernelID,"_osversion"));
    p.clearError();
    if(page_size!=PageSize || getKernelVersion()!=KernelVersion::BigSur ||
       XELP::kernelOS(version)!=XELP::OS::BigSur || !build || memcmp(build,"20G1443",8)) {
        state(Setup::WrongKernel);return;
    }
    const auto entry=p.solveSymbol<mach_vm_address_t>(KernelPatcher::KernelID,"_cs_validate_page");
    p.clearError();
    if(!routeable(entry)){state(Setup::UnsupportedEntry);return;}
    const auto prepared=prepareFile();
    if(prepared!=Setup::Armed){state(prepared);return;}
    publisher=thread_call_allocate(publish,nullptr);
    if(!publisher){vnode_rele(target);target=nullptr;state(Setup::NoPublisher);return;}
    // All immutable data, original/replacement pages and worker state exist
    // before the route. Lilu stores the original trampoline before installing it.
    KernelPatcher::RouteRequest request {"_cs_validate_page",validate,originalValidate};
    const bool routed=p.routeMultipleLong(KernelPatcher::KernelID,&request,1);
    p.clearError();
    if(!routed) {
        // A failed route is not permission to free state a partial route could
        // reference. Keep it inert and retained; no unsupported unload path.
        state(Setup::RouteFailed);return;
    }
    auto root=IORegistryEntry::getRegistryRoot();
    root->setProperty("RPLPGLURBImageSHA256",ImageHash);
    root->setProperty("RPLPGLURBOriginalPageSHA256",OriginalPageHash);
    root->setProperty("RPLPGLURBCorrectedPageSHA256",CorrectedPageHash);
    __atomic_store_n(&armed,true,__ATOMIC_RELEASE);state(Setup::Armed);
    // The vnode usecount, page buffers and deferred call live as long as this
    // non-unloadable Lilu plugin. No late rearming or live executable-page edits.
}
