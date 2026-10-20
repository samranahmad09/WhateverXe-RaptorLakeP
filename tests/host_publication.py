#!/usr/bin/env python3
"""Replay actual host admission: no PCI reads before successful publication."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1]
adapter=(r/'WhateverXe/kern_xelp_device.hpp').read_text();adapter=adapter[adapter.index('namespace XELP {'):adapter.rindex('#endif')]
s=(r/'WhateverXe/kern_host.cpp').read_text();body=s[s.index('void XeHost::processKernel'):s.index('void XeHost::processKext')]
code=r'''
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cassert>
#include <cstdio>
#include <vector>
#include <algorithm>
#include "kern_xelp_platform.hpp"
std::vector<int> events;bool publish=true,published=false,observeArgument=false,gtArgument=false;uint32_t identity=0x9a498086;uint8_t revision=0;
bool checkKernelArgument(const char *name){return (observeArgument && !strcmp(name,"-wex-rplp-observe")) ||
 (gtArgument && !strcmp(name,"-wex-rplp-gt"));}
struct Object {virtual ~Object()=default;};
struct OSData:Object {uint32_t value=0;void *getBytesNoCopy(){return &value;}size_t getLength(){return 4;}};
OSData fake;Object customModel;bool hasModel=false;unsigned modelWrites=0;Object *deviceProperty=nullptr;
struct IOPCIDevice {
 const char *name="GFX0";unsigned renames=0;
 uint32_t configRead32(int){assert(published);events.push_back(2);return identity;}
 uint8_t configRead8(int){assert(published);events.push_back(3);return revision;}
 uint16_t configRead16(int){assert(published);events.push_back(4);return 0x300;}
 const char *getName(){return name;}Object *getProperty(const char *key){return !strcmp(key,"device-id")?deviceProperty:!strcmp(key,"model")&&hasModel?&customModel:nullptr;}
 void setProperty(const char *,const char *){}void setProperty(const char *key,const void *value,unsigned size){if(!strcmp(key,"model")){assert(value && size>1);++modelWrites;}}void setProperty(const char *,uint64_t,unsigned){}
};
#define OSDynamicCast(type,p) dynamic_cast<type *>(p)
struct DeviceInfo {
 enum class FirmwareVendor{Apple,Other};FirmwareVendor firmwareVendor=FirmwareVendor::Other;
 IOPCIDevice *videoBuiltin=nullptr,*managementEngine=nullptr;bool reportedFramebufferIsConnectorLess=false;
 static DeviceInfo value;static DeviceInfo *create(){return &value;}static void deleter(DeviceInfo *){events.push_back(9);}
};DeviceInfo DeviceInfo::value;
namespace WIOKit {bool awaitPublishing(IOPCIDevice *){events.push_back(1);published=publish;return publish;}void renameDevice(IOPCIDevice *device,const char *name){device->name=name;++device->renames;events.push_back(5);}}
// DEVICE_ADAPTER
struct KernelPatcher {static constexpr int KernelID=0;uintptr_t solveSymbol(int,const char *){return 0;}void clearError(){}};
namespace XeDataAccess {void processKernel(KernelPatcher &){assert(published);events.push_back(7);}}
struct Graphics {bool off=false;void switchOff(){off=true;}} graphics;
struct XeHost {
 bool active=false;enum {FB_DETECT,FB_RESET,FB_COPY,FB_ZEROFILL,FB_NONE,FB_TOTAL};uint32_t resetFramebuffer=FB_NONE;
 struct vc_info{};vc_info consoleVinfo;bool gotConsoleVinfo=false;
 struct XE {void processKernel(KernelPatcher &,DeviceInfo *){events.push_back(8);}void observeRaptorLakeP(IOPCIDevice *){events.push_back(6);}} xe;
 void processKernel(KernelPatcher &);
};
'''.replace('// DEVICE_ADAPTER',adapter)+body+r'''
int main(){KernelPatcher patcher;IOPCIDevice gpu,mei;
const char *names[]={nullptr,"IMEI","HECI","MEI"};
for(unsigned firmware=0;firmware<2;++firmware)for(unsigned me=0;me<5;++me)for(unsigned test=0;test<11;++test){
 const bool rpl=test>=6;
 mei.name=names[me%4];mei.renames=0;gpu.name="GFX0";
 DeviceInfo::value.managementEngine=me==4?nullptr:&mei;
 DeviceInfo::value.firmwareVendor=firmware?DeviceInfo::FirmwareVendor::Apple:DeviceInfo::FirmwareVendor::Other;
 events.clear();published=false;publish=test!=1;identity=rpl?0xa7a88086:(test==2 || test>=4)?0x12348086:0x9a498086;deviceProperty=test==4 || test==5?&fake:nullptr;fake.value=test==4?0x46D2:0xFFFF;graphics.off=false;
 hasModel=test==3;modelWrites=0;observeArgument=test==7 || test==9 || test==10;gtArgument=test==9;revision=test>=8?4:0;
 DeviceInfo::value.videoBuiltin=test==0?nullptr:&gpu;XeHost host;host.processKernel(patcher);
 const bool normalize=(test==3 || test==4 || test==9) && me!=4;
 assert(mei.renames==(normalize && me!=1?1U:0U));
 if(normalize)assert(mei.name && !strcmp(mei.name,"IMEI"));
 else assert(mei.name==names[me%4]);
 assert(events.back()==9);assert(modelWrites==(test==4 || rpl?1U:0U));
 if(test<2){assert(!host.active&&graphics.off);assert(events.size()==(test==0?1:2));}
 else if(test==2 || test==5){assert(!host.active&&graphics.off);assert(events.size()==4);}
 // RPL-P: revision 0 is never admitted; revision 4 is admitted only with -wex-rplp-gt. The observer runs
 // only while inactive and on request.
 else if(test==9){assert(host.active&&!graphics.off);assert(events[events.size()-2]==8);assert(!std::count(events.begin(),events.end(),6));}
 else if(rpl){const bool observed=test==7 || test==10;assert(!host.active&&graphics.off&&!std::count(events.begin(),events.end(),8));
  assert(events.size()==(observed?5U:4U));assert((events[3]==6)==observed);}
 else {assert(host.active&&!graphics.off);assert(events.front()==1);assert(events[events.size()-2]==8);}
}
puts("PASS actual host admission: absent/unpublished/unsupported GPUs never enter Xe; published PCI reads precede selected-driver dispatch; RPL-P admission (rev 4 + -wex-rplp-gt) and observation are opt-in");}
'''
with tempfile.TemporaryDirectory(prefix='wxe-host-') as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-fsanitize=address,undefined','-I'+str(r/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
