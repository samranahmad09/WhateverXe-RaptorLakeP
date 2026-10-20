#!/usr/bin/env python3
"""Exercise the actual transmitter observer with inaccessible register domains."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--source', type=Path)
parser.add_argument('--unowned-pipe', action='store_true',
                    help='Leave the BAR readable except for an unrelated pipe bank')
args = parser.parse_args()
source = (args.source or root / 'WhateverXe/kern_xe.cpp').read_text()
body = re.search(r'void XE::tglSnapshotTransmitter\(.*?\n\}', source, re.S).group()
fixture = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <sys/mman.h>
#include <unistd.h>
#include "kern_tgl_port_map.hpp"
#define arrsize(a) (sizeof(a)/sizeof(a[0]))
bool quiet=false, eligible=true;
unsigned locks=0, activations=0;
bool tglQuietTraining(){return quiet;}
unsigned IOSimpleLockLockDisableInterrupt(void*){++locks;return 0;}
void IOSimpleLockUnlockEnableInterrupt(void*,unsigned){}
std::map<std::string,std::vector<uint32_t>> published;
struct IORegistryEntry {
 static IORegistryEntry *getRegistryRoot(){static IORegistryEntry r;return &r;}
 void setProperty(const char *key,const void *p,size_t n){
  auto b=static_cast<const uint32_t*>(p);published[key]={b,b+n/4};
 }
};
struct XE {
 void *tglObservationController=nullptr,*tglDekelLock=(void*)1;
 bool tglLegacyTCEligible(uint32_t ddi){return eligible&&ddi>=3&&ddi<=8;}
 static void tglObserveActivation(void*,void*,void*,unsigned,bool,unsigned){++activations;}
 static void tglSnapshotTransmitter(void*,bool,uint32_t);
};
XE instance,*callbackXE=&instance;
// BODY
int main(int argc,char**){
 const size_t size=0x170000;
 auto mmio=static_cast<uint8_t*>(mmap(nullptr,size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0));
 assert(mmio!=MAP_FAILED);
 *reinterpret_cast<uint32_t*>(mmio+0x1638A0)=0x10;
 if(argc>1){
  // A DP port can be owned without owning every transcoder. The old observer
  // reads all four TP_CTL banks, including this unrelated bank at 0x63540.
  size_t page=getpagesize();size_t start=0x63540/page*page;
  assert(mprotect(mmio+start,page,PROT_NONE)==0);
 }else assert(mprotect(mmio,size,PROT_NONE)==0);
 alignas(8) uint8_t port[0x600]{},access[0x80]{};uint32_t config[6]{};
 *reinterpret_cast<void**>(port+0x548)=config;
 *reinterpret_cast<void**>(port+0x578)=access;
 *reinterpret_cast<void**>(access+0x50)=mmio;
 *reinterpret_cast<uint32_t*>(access+0x60)=size;
 unsigned calls=0;
 for(unsigned ddi=3;ddi<=8;++ddi)for(bool done:{false,true}){
  config[0]=ddi;config[2]=1;published.clear();
  *reinterpret_cast<uint32_t*>(port+0x550)=4;
  *reinterpret_cast<uint32_t*>(port+0x554)=23;
  *reinterpret_cast<uint32_t*>(port+0x124)=4;
  *reinterpret_cast<uint32_t*>(port+0x128)=20;
  *reinterpret_cast<uint32_t*>(port+0x130)=99;
  *reinterpret_cast<uint32_t*>(port+0x590)=0x64000+0x100*ddi;
  XE::tglSnapshotTransmitter(port,done,7);
  auto key="TGLTransmitter"+std::to_string(ddi)+(done?"Exit":"Entry");
  auto &q=published.at(key);
  assert(q.size()==34&&q[1]==done&&q[2]==7&&q[3]==4&&q[4]==23);
  assert(q[5]==4&&q[6]==20&&q[7]==99&&q[8]==0x64000+0x100*ddi&&q[9]==1);
  assert(q[33]==0x4D454D31);for(unsigned i=10;i<33;++i)assert(q[i]==0);
  for(auto &p:published)assert(p.first.find("TGLTransmitterPHY")==std::string::npos);
  ++calls;
 }
 assert(locks==0&&activations==calls);
 // Quiet and unqualified paths must not start hardware observation either.
 for(unsigned fault=0;fault<4;++fault){
  published.clear();quiet=fault==0;eligible=fault!=1;
  config[0]=fault==2?2:4;
  *reinterpret_cast<void**>(port+0x548)=fault==3?nullptr:config;
  XE::tglSnapshotTransmitter(port,false,0);assert(published.empty()&&locks==0);
 }
 quiet=false;published.clear();XE::tglSnapshotTransmitter(nullptr,false,0);
 assert(published.empty()&&locks==0);
 munmap(mmio,size);
 puts("PASS: 12 transmitter boundaries; software fields retained, no MMIO/HIP access");
}
'''.replace('// BODY', body)
with tempfile.TemporaryDirectory(prefix='tgl-transmitter-') as directory:
    directory = Path(directory)
    cpp, binary = directory / 'check.cpp', directory / 'check'
    cpp.write_text(fixture)
    subprocess.run(['clang++', '-std=c++14', '-O1', '-g',
                    '-fsanitize=address,undefined', '-I' + str(root / 'WhateverXe'),
                    str(cpp), '-o', str(binary)], check=True)
    modes = [['unowned-pipe']] if args.unowned_pipe else [[], ['unowned-pipe']]
    for mode in modes:
        subprocess.run([str(binary), *mode], check=True)
