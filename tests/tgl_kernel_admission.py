#!/usr/bin/env python3
"""Exercise the actual TGL admission branch across physical and OS guards."""
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[1]
out=root/'build/tests';out.mkdir(parents=True,exist_ok=True)
s=(root/'WhateverXe/kern_xe.cpp').read_text()
start=s.index('    tglRequested=profile.admitted()')
end=s.index('\nvoid XE::deinit()',start)
body=s[start:end].rstrip()[:-1]
code=r'''
#include <cstring>
#include "kern_xelp_os.hpp"
#include <cassert>
#include <cstdio>
namespace XELP { enum class Family {TigerLake,Other}; }
struct Profile { bool ok; XELP::Family family; bool admitted() const {return ok;} };
const char *version;
bool noaccel,off;
bool checkKernelArgument(const char *arg) {return !strcmp(arg,"-wex-noaccel")?noaccel:!strcmp(arg,"-wex-tgl-off")?off:false;}
bool tglRequested,tglNoPanel,tglInactivePorts,tglPG1Poll,tglClockOrderRequested,tglTrainingLeaseRequested,tglProgramEnabled,tglAcceleratorRequested;
int kextIntelTGL,kextIntelTGLFb;int *currentGraphics,*currentFramebuffer;
void select(Profile profile) { BODY }
int main(){
const char *versions[]={
"Darwin Kernel Version 20.0.0: Sat Jun 13 17:58:16 PDT 2020; root:xnu-7090.0.0.111.5~1/RELEASE_X86_64",
"Darwin Kernel Version 20.6.0: Thu Jul  6 22:12:47 PDT 2023; root:xnu-7195.141.49.702.12~1/RELEASE_X86_64",
"Darwin Kernel Version 21.6.0: Mon Jun 24 00:56:10 PDT 2024; root:xnu-8020.240.18.709.2~1/RELEASE_X86_64",
"Darwin Kernel Version 22.6.0: Tue Jul 15 08:22:28 PDT 2025; root:xnu-8796.141.3.713.2~2/RELEASE_X86_64",
"Darwin Kernel Version 23.6.0: Tue Jul 21 21:56:37 PDT 2026; root:xnu-10063.141.1.713.39~1/RELEASE_X86_64",
"Darwin Kernel Version 24.6.0: Sun Aug 23 20:30:56 PDT 2026; root:xnu-11417.140.69.712.69~1/RELEASE_X86_64",
"Darwin Kernel Version 25.6.0: Tue Aug 18 17:46:40 PDT 2026; root:xnu-12377.161.15.700.19~2/RELEASE_X86_64",
"Darwin Kernel Version 25.6.0: unqualified",
"Darwin Kernel Version 22.6.0: unqualified",
"Darwin Kernel Version 23.6.0: unqualified",
"Darwin Kernel Version 24.6.0: unqualified"};
unsigned cases=0;
for(unsigned k=0;k<11;++k)for(unsigned n=0;n<2;++n)for(unsigned o=0;o<2;++o)for(unsigned physical=0;physical<2;++physical)for(unsigned family=0;family<2;++family){
version=versions[k];noaccel=n;off=o;currentGraphics=currentFramebuffer=nullptr;
select({bool(physical),family?XELP::Family::TigerLake:XELP::Family::Other});
bool expected=physical&&family&&!o&&(k<7);
assert(tglRequested==expected);assert(tglAcceleratorRequested==(expected&&!n));
assert((currentFramebuffer==&kextIntelTGLFb)==expected);++cases;
}printf("PASS: %u actual TGL admission cases, including Tahoe acceleration, Sequoia acceleration, Sonoma acceleration, Ventura acceleration, noaccel and unknown-kernel rejection\n",cases);
}
'''.replace('BODY',body)
(out/'test-admission.cpp').write_text(code)
subprocess.run(['clang++','-std=c++17','-O2','-I'+str(root/'WhateverXe'),str(out/'test-admission.cpp'),'-o',str(out/'test-admission')],check=True)
subprocess.run([str(out/'test-admission')],check=True)
