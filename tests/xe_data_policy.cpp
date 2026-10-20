#include "kern_xe_data_policy.hpp"
#include <cassert>
#include <string>
int main() {
    using namespace XeDataPolicy;
    const std::string base = "/Library/GPUBundles/XeCompilerAdapter.bundle/Contents/";
    assert(service("/System/Library/Frameworks/Metal.framework/Versions/A/XPCServices/MTLCompilerService.xpc/Contents/MacOS/MTLCompilerService"));
    assert(!service("/tmp/MTLCompilerService"));
    assert(!service("/System/Library/Frameworks/Metal.framework/Versions/A/XPCServices/MTLCompilerService.xpc/Contents/MacOS/MTLCompilerService-extra"));
    for (auto suffix : {"MacOS/adapter.dylib", "MacOS/libigc-tgl.dylib"}) {
        assert(file((base+suffix).c_str(), true));
        assert(file(("/System/Volumes/Data"+base+suffix).c_str(), true));
    }
    for (auto suffix : {"MacOS/runtime.igc64.bc", "Info.plist"}) {
        assert(file((base+suffix).c_str(), false));
        assert(!file((base+suffix).c_str(), true));
    }
    for (auto suffix : {"", "MacOS/other.dylib", "MacOS/adapter.dylib.old", "MacOS/../Info.plist"})
        assert(!file((base+suffix).c_str(), false));
    assert(!file("/System/Volumes/Database/Library/GPUBundles/XeCompilerAdapter.bundle/Contents/MacOS/adapter.dylib", false));
    assert(!file(nullptr, false));
    assert(file("/Library/GPUBundles/XeCompilerAdapter.bundle/Contents/MacOS", false));
    assert(!file("/Library/GPUBundles/XeCompilerAdapter.bundle/Contents/MacOS", true));
    assert(!file("/Library/GPUBundles/XeCompilerAdapter.bundle/Contents/MacOS/unknown", false));
    for (int mode : {0,2,3,0x201,0x401}) assert(!readOnly(mode));
    assert(readOnly(1));
    assert(executeOnly(5,2));
    for (int prot : {0,1,2,3,6,7}) assert(!executeOnly(prot,2));
    assert(!executeOnly(5,1));
    assert(!executeOnly(5,3));
    const char *web = "/System/Library/Frameworks/WebKit.framework/Versions/A/XPCServices/com.apple.WebKit.GPU.xpc/Contents/MacOS/com.apple.WebKit.GPU";
    const char *metal = "/System/Library/Frameworks/Metal.framework/Versions/A/XPCServices/MTLCompilerService.xpc/Contents/MacOS/MTLCompilerService";
    const std::string va = "/Library/GPUBundles/AppleIntelTGLGraphicsVADriver.bundle/Contents/MacOS/";
    for (auto name : {"AppleIntelTGLGraphicsVADriver", "TGLVA"}) {
        assert(clientForFile((va+name).c_str(), true) == Client::WebKitGPU);
        assert(clientForFile(("/System/Volumes/Data"+va+name).c_str(), true) == Client::WebKitGPU);
        assert(clientForFile((va+name).c_str(), false) == Client::None);
    }
    for (auto name : {"", "other", "TGLVA.old", "../Info.plist"})
        assert(clientForFile((va+name).c_str(), true) == Client::None);
    assert(service(web, Client::WebKitGPU));
    for (auto root : {"/System/Volumes/Preboot/Cryptexes/OS", "/System/Volumes/Preboot/Cryptexes/Incoming/OS"}) {
        assert(service((std::string(root)+web).c_str(), Client::WebKitGPU));
        assert(!service((std::string(root)+web).c_str(), Client::Compiler));
        assert(!service((std::string(root)+metal).c_str(), Client::WebKitGPU));
        assert(!service((std::string(root)+web+".old").c_str(), Client::WebKitGPU));
    }
    for (auto root : {"/tmp", "/System/Volumes/Data", "/System/Volumes/Preboot/Cryptexes/OS-extra",
        "/System/Volumes/Preboot/Cryptexes/Incoming/OS/..", "/System/Volumes/Preboot/Cryptexes/App"})
        assert(!service((std::string(root)+web).c_str(), Client::WebKitGPU));
    assert(!service(nullptr, Client::WebKitGPU));
    const std::string mtl = "/Library/GPUBundles/AppleIntelTGLGraphicsMTLDriver.bundle/Contents/MacOS/";
    for (auto name : {"AppleIntelTGLGraphicsMTLDriver", "descriptor.dylib", "impostor.dylib", "libigdmd.dylib"}) {
        assert(clientForFile((mtl+name).c_str(), true) == Client::WebKitGPU);
        assert(clientForFile(("/System/Volumes/Data"+mtl+name).c_str(), true) == Client::WebKitGPU);
        assert(clientForFile((mtl+name).c_str(), false) == Client::None);
    }
    for (auto name : {"", "other.dylib", "descriptor.dylib.old", "../Info.plist"})
        assert(clientForFile((mtl+name).c_str(), true) == Client::None);
    assert(!service(web, Client::Compiler));
    assert(!service(metal, Client::WebKitGPU));
    assert(!service("/tmp/com.apple.WebKit.GPU", Client::WebKitGPU));
    assert(!service(metal, Client::None));
    assert(clientForFile((base+"MacOS/adapter.dylib").c_str(), true) == Client::Compiler);
}
