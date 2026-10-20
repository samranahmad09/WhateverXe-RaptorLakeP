// Copyright (c) 2026 b00t0x. BSD-3-Clause.
#include "kern_xe_data_access.hpp"
#include "kern_xe_data_policy.hpp"
#include <Headers/kern_api.hpp>
#include <IOKit/IORegistryEntry.h>
#include <libkern/OSAtomic.h>

namespace {
const char *paths[] {"/System/Library/Extensions/Sandbox.kext/Contents/MacOS/Sandbox"};
KernelPatcher::KextInfo sandbox {"com.apple.security.sandbox", paths, arrsize(paths), {true}, {},
    KernelPatcher::KextInfo::Unloaded};
void *(*currentProcess)();
void *(*executableVnode)(void *);
void *(*fileVnode)(void *);
int (*getPath)(void *, char *, int *);
int (*putVnode)(void *);
using Open = int (*)(void *, void *, void *, int);
using Map = int (*)(void *, void *, void *, int, int, uint64_t, int *);
Open originalOpen;
Map originalMap;
bool enabled, ready;
volatile SInt32 readGrants, mapGrants;

bool knownOS() {
    const auto v = getKernelVersion();
    return v >= KernelVersion::Sonoma && v <= KernelVersion::Tahoe;
}
bool allowed(void *vnode, bool executable) {
    if (!ready || !vnode) return false;
    char path[1024];
    int length = sizeof(path);
    // vn_getpath does not enter the filesystem; never reopen a path in a MAC hook.
    if (getPath(vnode, path, &length)) return false;
    const auto client = XeDataPolicy::clientForFile(path, executable);
    if (client == XeDataPolicy::Client::None) return false;
    auto process = currentProcess();
    auto image = process ? executableVnode(process) : nullptr;
    if (!image) return false;
    length = sizeof(path);
    const auto error = getPath(image, path, &length);
    putVnode(image); // proc_getexecutablevnode returns an iocount.
    return !error && XeDataPolicy::service(path, client);
}
int checkOpen(void *cred, void *vnode, void *label, int mode) {
    const int result = originalOpen(cred, vnode, label, mode);
    if (result == 1 && XeDataPolicy::readOnly(mode) && allowed(vnode, false)) {
        if (OSIncrementAtomic(&readGrants) < 4) SYSLOG("wex", "GPU Data read admitted");
        return 0;
    }
    return result;
}
int checkMap(void *cred, void *file, void *label, int prot, int flags, uint64_t offset, int *maxprot) {
    const int result = originalMap(cred, file, label, prot, flags, offset, maxprot);
    if (result == 1 && XeDataPolicy::executeOnly(prot, flags) && allowed(fileVnode(file), true)) {
        if (OSIncrementAtomic(&mapGrants) < 4) SYSLOG("wex", "GPU Data executable map admitted");
        return 0;
    }
    return result; // Preserve maxprot and every other MAC policy, including AMFI.
}
}

void XeDataAccess::init() {
    if (knownOS()) lilu.onKextLoadForce(&sandbox);
}
void XeDataAccess::processKernel(KernelPatcher &p) {
    if (!knownOS()) return;
    currentProcess = reinterpret_cast<decltype(currentProcess)>(p.solveSymbol(KernelPatcher::KernelID, "_current_proc"));
    p.clearError();
    executableVnode = reinterpret_cast<decltype(executableVnode)>(p.solveSymbol(KernelPatcher::KernelID, "_proc_getexecutablevnode"));
    p.clearError();
    fileVnode = reinterpret_cast<decltype(fileVnode)>(p.solveSymbol(KernelPatcher::KernelID, "_fg_get_vnode"));
    p.clearError();
    getPath = reinterpret_cast<decltype(getPath)>(p.solveSymbol(KernelPatcher::KernelID, "_vn_getpath"));
    p.clearError();
    putVnode = reinterpret_cast<decltype(putVnode)>(p.solveSymbol(KernelPatcher::KernelID, "_vnode_put"));
    p.clearError();
    enabled = currentProcess && executableVnode && fileVnode && getPath && putVnode;
    IORegistryEntry::getRegistryRoot()->setProperty("WhateverXeDataAccessSymbols", enabled);
}
bool XeDataAccess::processKext(KernelPatcher &p, size_t index, mach_vm_address_t address, size_t size) {
    if (index != sandbox.loadIndex) return false;
    if (!enabled) return true;
    const auto open = p.solveSymbol<mach_vm_address_t>(index, "_hook_vnode_check_open", address, size);
    p.clearError();
    const auto map = p.solveSymbol<mach_vm_address_t>(index, "_hook_file_check_mmap", address, size);
    p.clearError();
    // Both native prologues are position independent through a long-jump span.
    // Do not let Lilu relocate a CALL/RIP-relative instruction into a trampoline.
    const uint8_t openPrefix[] {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x81,0xec};
    const uint8_t mapPrefix[] {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x53,0x48,0x81,0xec};
    const bool known = size >= 32 && open >= address && map >= address &&
        open-address <= size-32 && map-address <= size-32 &&
        !memcmp(reinterpret_cast<void *>(open), openPrefix, sizeof(openPrefix)) &&
        !memcmp(reinterpret_cast<void *>(map), mapPrefix, sizeof(mapPrefix));
    if (known) {
        KernelPatcher::RouteRequest routes[] {
            {"_hook_vnode_check_open", checkOpen, originalOpen},
            {"_hook_file_check_mmap", checkMap, originalMap}
        };
        ready = p.routeMultiple(index, routes, arrsize(routes), address, size);
        p.clearError();
    }
    IORegistryEntry::getRegistryRoot()->setProperty("WhateverXeDataAccess", ready);
    SYSLOG("wex", "GPU Data access ready=%d", ready);
    return true;
}
