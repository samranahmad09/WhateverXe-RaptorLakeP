#include "kern_xelp_os.hpp"
#include <cassert>

int main() {
    using namespace XELP;
    assert(kernelOS(nullptr)==OS::Unknown);
    assert(kernelOS("")==OS::Unknown);
    assert(kernelOS("Darwin Kernel Version 24.6.1: update/RELEASE_X86_64")==OS::Sequoia);
    assert(kernelOS("Darwin Kernel Version 25.6.1: update/RELEASE_X86_64")==OS::Tahoe);
    assert(kernelOS("Darwin Kernel Version 25.7.0: update/RELEASE_X86_64")==OS::Tahoe);
    const char *rejected[] {
        "Darwin Kernel Version 23.6.0: update/RELEASE_X86_64",
        "Darwin Kernel Version 24.5.0: update/RELEASE_X86_64",
        "Darwin Kernel Version 24.7.0: update/RELEASE_X86_64",
        "Darwin Kernel Version 25.5.0: update/RELEASE_X86_64",
        "Darwin Kernel Version 26.0.0: update/RELEASE_X86_64",
        "Darwin Kernel Version 25.6.0: update/DEBUG_X86_64",
        "Darwin Kernel Version 25.6.0: update/RELEASE_ARM64",
        "Darwin Kernel Version 25.6.0: update/RELEASE_X86_64.extra",
        "Darwin Kernel Version 25.6.0:",
        "Darwin Kernel Version 25.6.0: ",
        "Darwin Kernel Version 25.6: update/RELEASE_X86_64",
        "Darwin Kernel Version 25.6.9999999999999: update/RELEASE_X86_64",
    };
    for (auto version:rejected) assert(kernelOS(version)==OS::Unknown);
}
