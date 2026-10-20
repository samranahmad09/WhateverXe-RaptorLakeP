#!/usr/bin/env python3
"""Actual lifecycle recorder: concurrent snapshots, IRQ state and opaque objects."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'WhateverXe/kern_xe.cpp').read_text()
body = source[source.index('struct TGLTransitionRecord {'):
              source.index('// Independent and nested training calls')]
fixture = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>
#include <sys/mman.h>
template<class T,size_t N>constexpr size_t arrsize(const T(&)[N]){return N;}
using IOSimpleLock=std::mutex;
thread_local bool irqEnabled=true,held=false;
unsigned IOSimpleLockLockDisableInterrupt(IOSimpleLock*l){
 bool prior=irqEnabled;irqEnabled=false;l->lock();assert(!held);held=true;return prior;
}
void IOSimpleLockUnlockEnableInterrupt(IOSimpleLock*l,unsigned prior){
 assert(held&&!irqEnabled);held=false;l->unlock();irqEnabled=prior;
}
std::atomic<uint64_t> ticks{0},threadCounter{0};
void clock_get_uptime(uint64_t*t){*t=++ticks;}
void absolutetime_to_nanoseconds(uint64_t t,uint64_t*n){*n=t;}
void *current_thread(){thread_local uintptr_t id=++threadCounter;return reinterpret_cast<void*>(id);}
bool failAlloc=false,failData=false;
void *IOMalloc(size_t n){assert(!held);return failAlloc?nullptr:malloc(n);}
void IOFree(void*p,size_t){assert(!held);free(p);}
struct OSSerialize{std::vector<uint8_t> data;};
struct OSData{
 std::vector<uint8_t> data;
 static OSData *withBytes(const void*p,size_t n){
  assert(!held);if(failData)return nullptr;auto d=new OSData;
  d->data.assign((const uint8_t*)p,(const uint8_t*)p+n);return d;
 }
 bool serialize(OSSerialize*s){assert(!held);s->data=data;return true;}
 void release(){assert(!held);delete this;}
};
// BODY
void validate(OSSerialize&s){
 assert(s.data.size()==16+256*128);uint32_t h[4];memcpy(h,s.data.data(),16);
 assert(h[0]==1&&h[2]==256&&h[3]==128);
 for(unsigned i=0;i<256;++i){
  TGLTransitionRecord r;memcpy(&r,s.data.data()+16+128*i,128);
  if(!r.q[2])continue;
  assert(r.q[2]<=h[1]&&(r.q[2]-1)%256==i);
  assert(r.q[1]&&r.q[3]==90&&r.q[4]==1);
  assert(r.q[6]==r.q[7]&&r.q[7]==r.q[8]);
  for(unsigned j=9;j<16;++j)assert(r.q[j]==r.q[6]);
 }
}
int main(){
 OSSerialize snapshot;IOSimpleLock lock;
 assert(!tglTransitionSerialize(nullptr,nullptr,&snapshot));
 tglTransitionLock=&lock;
 auto inaccessible=mmap(nullptr,4096,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);
 assert(inaccessible!=MAP_FAILED);
 tglTransitionRecord(1,false,inaccessible,2,3,4);assert(tglTransitionCount==1);
 // Invalid length/payload rejected before any copy; object is never followed.
 tglTransitionRecord(1,false,inaccessible,2,3,4,inaccessible,57);
 tglTransitionRecord(1,false,inaccessible,2,3,4,nullptr,1);
 assert(tglTransitionCount==1);
 for(bool prior:{false,true}){
  irqEnabled=prior;tglTransitionRecord(1,true,inaccessible,0,0,0);
  assert(irqEnabled==prior&&!held);
  assert(tglTransitionSerialize(nullptr,nullptr,&snapshot));assert(irqEnabled==prior);
 }
 irqEnabled=true;failAlloc=true;assert(!tglTransitionSerialize(nullptr,nullptr,&snapshot));
 failAlloc=false;failData=true;assert(!tglTransitionSerialize(nullptr,nullptr,&snapshot));failData=false;
 // Wrap all slots, then race multiple producers against memory-only snapshots.
 auto write=[&](unsigned count){for(unsigned i=0;i<count;++i){
  uint64_t payload[7];for(auto &v:payload)v=i;
  tglTransitionRecord(90,true,inaccessible,i,i,i,payload,sizeof(payload));
  assert(irqEnabled&&!held);
 }};
 write(512);assert(tglTransitionSerialize(nullptr,nullptr,&snapshot));validate(snapshot);
 std::thread a([&]{write(4000);}),b([&]{write(4000);});
 for(unsigned i=0;i<100;++i){assert(tglTransitionSerialize(nullptr,nullptr,&snapshot));validate(snapshot);}
 a.join();b.join();assert(tglTransitionCount==8515);
 assert(tglTransitionSerialize(nullptr,nullptr,&snapshot));validate(snapshot);
 for(auto reg:{0x45400ul,0x45404ul,0x45408ul,0x4540Cul,0x45450ul,0x45454ul})assert(tglTransitionRegister(reg));
 for(auto reg:{0ul,0x45410ul,0x45451ul,0x138124ul})assert(!tglTransitionRegister(reg));
 munmap(inaccessible,4096);
 puts("PASS: 8515 events, concurrent coherent snapshots, ring wrap, allocation failure, IRQ restore, no object/MMIO access");
}
'''.replace('// BODY', body)
with tempfile.TemporaryDirectory(prefix='tgl-transition-') as tmp:
    tmp = Path(tmp)
    cpp, binary = tmp / 'check.cpp', tmp / 'check'
    cpp.write_text(fixture)
    subprocess.run(['clang++', '-std=c++14', '-O1', '-g', '-pthread',
                    '-fsanitize=address,undefined', str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
