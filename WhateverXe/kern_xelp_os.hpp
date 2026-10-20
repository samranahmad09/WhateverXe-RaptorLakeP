// OS/donor ABI selection is independent of GPU family and board wiring.
// The installer enforces product-version minima; runtime patches still verify bytes.
#ifndef KERN_XELP_OS_HPP
#define KERN_XELP_OS_HPP
namespace XELP {
enum class OS { Unknown, Donor, BigSur, Monterey, Ventura, Sonoma, Sequoia, Tahoe };
inline OS kernelOS(const char *version) {
    static const char *const versions[] = {
        "Darwin Kernel Version 20.0.0: Sat Jun 13 17:58:16 PDT 2020; root:xnu-7090.0.0.111.5~1/RELEASE_X86_64",
        "Darwin Kernel Version 20.6.0: Thu Jul  6 22:12:47 PDT 2023; root:xnu-7195.141.49.702.12~1/RELEASE_X86_64",
        "Darwin Kernel Version 21.6.0: Mon Jun 24 00:56:10 PDT 2024; root:xnu-8020.240.18.709.2~1/RELEASE_X86_64",
        "Darwin Kernel Version 22.6.0: Tue Jul 15 08:22:28 PDT 2025; root:xnu-8796.141.3.713.2~2/RELEASE_X86_64",
        "Darwin Kernel Version 23.6.0: Tue Jul 21 21:56:37 PDT 2026; root:xnu-10063.141.1.713.39~1/RELEASE_X86_64",
    };
    if (!version) return OS::Unknown;
    for (unsigned i=0;i<sizeof(versions)/sizeof(versions[0]);++i) {
        const char *a=version,*b=versions[i];
        while (*a && *a==*b) {++a;++b;}
        if (*a==*b) return static_cast<OS>(i+1);
    }
    // Servicing updates must not silently bypass ABI setup because the build
    // date/XNU revision changed. Darwin 24.6 cannot distinguish macOS 15.7
    // from 15.8: the package enforces 15.8.x. Darwin 25.6+ covers 26.7+.
    const char *p=version,*prefix="Darwin Kernel Version ";
    while (*prefix && *p==*prefix) {++p;++prefix;}
    if (*prefix) return OS::Unknown;
    unsigned numbers[3] {};
    for (unsigned i=0;i<3;++i) {
        if (*p<'0' || *p>'9') return OS::Unknown;
        do {
            numbers[i]=numbers[i]*10+static_cast<unsigned>(*p++-'0');
            if (numbers[i]>999) return OS::Unknown;
        } while (*p>='0' && *p<='9');
        if (*p++!=(i==2 ? ':' : '.')) return OS::Unknown;
    }
    if (*p++!=' ') return OS::Unknown;
    const char *end=p; while (*end) ++end;
    const char suffix[]="/RELEASE_X86_64";
    if (end-p<static_cast<long>(sizeof(suffix)-1)) return OS::Unknown;
    const char *tail=end-(sizeof(suffix)-1);
    for (unsigned i=0;i<sizeof(suffix);++i)
        if (tail[i]!=suffix[i]) return OS::Unknown;
    if (numbers[0]==24 && numbers[1]==6) return OS::Sequoia;
    if (numbers[0]==25 && numbers[1]>=6) return OS::Tahoe;
    return OS::Unknown;
}
}
#endif
