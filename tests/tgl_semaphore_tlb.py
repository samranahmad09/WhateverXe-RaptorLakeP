#!/usr/bin/env python3
"""Replay the donor's BCS wait/reserve/map/emit order in CPU memory.
Exact native machine code, mocked allocation and submission (no GPU claim).
Proves a mapping published after reservation skips native invalidation, and
checks the real wrappers, both WA branches, early flush, and ring wrap.
"""
from pathlib import Path
import hashlib,re,subprocess,sys,tempfile
repo=Path(__file__).resolve().parents[1]
s=(repo/'WhateverXe/kern_xe.cpp').read_text(); b=Path(sys.argv[1]).read_bytes()
assert hashlib.sha256(b).hexdigest()=='bd1a21f5e622aadabdab6441f6b5c38ed73fa3053c783fdf0fb7e4b309f273d0'
for name,at in [('baseSpaceBody',0x428ec),('tlbWriteHead',0x4290c),('semSpaceBody',0x425f8),('semCommandHead',0x4249e),('semMapCall',0x42644)]:
 vals=re.search(r'const uint8_t '+name+r'\[\] \{([^}]+)',s)[1]
 want=bytes(int(v.strip(),0) for v in vals.split(',')); assert b[at:at+len(want)]==want,name
# Lilu 1.7.2 copies the first >=6 bytes verbatim. This function needs 9,
# including a PC-relative CALL: using its original trampoline is invalid.
assert b[0x425fc]==0xe8
import struct
assert 0x42601+struct.unpack_from('<i',b,0x425fd)[0]==0x42440
assert '{semSpace,tglSemaphoreSpace}' in s and 'tglOrigSemaphoreSpace' not in s
helpers=s[s.index('uintptr_t tglOrigBaseTLBSpace'):s.index('bool tglSemaphoreSerialize')]
code=r'''#include <stdint.h>
#include <sys/mman.h>
#include <cassert>
#include <cstring>
#include <fstream>
#include <vector>
#include <cstdio>
#define FunctionCast(fn,addr) reinterpret_cast<decltype(&fn)>(addr)
uintptr_t tglRCSWriteBuffer;
HELPERS
static uint8_t *mem;static uint64_t cookie;
static uint8_t storage[4096+64],owner[0x1400],ring[0x900],ctx[0x100],sched[0x40],hw[0x40],stats[0x80];
static uint64_t desc[3]={0,0x488d61c0,0};static bool mapped;
static uint32_t mode;
static uint32_t getMode(void*) {return mode;}
static uint64_t getScratch(void*) {return 0x40000010;}
static uint64_t getGPU(void*) {return 0x488d6000;}
static void *getVirtual(void*) {return storage;}
static void noop() {}
static uint32_t one(void*) {return 1;}
static void serialize(void*r) {const uint32_t word=0;((bool(*)(void*,const uint32_t*,uint32_t))tglRCSWriteBuffer)(r,&word,1);}
static void *getBuffer(void*,void*,unsigned,int) {
 if(mapped) ((void(*)(void*))(mem+0x2d1d8))(owner);
 return desc;
}
static void jump(unsigned off,uintptr_t target) {
 uint8_t j[14]={0xff,0x25,0,0,0,0};memcpy(j+6,&target,8);memcpy(mem+off,j,14);
}
template<typename T> static void put(uint8_t *p,unsigned at,T v){memcpy(p+at,&v,sizeof(v));}
static void call(unsigned from,unsigned to){mem[from]=0xe8;put(mem,from+1,int32_t(to-from-5));}
int main(int argc,char **argv){
 assert(argc==2);std::ifstream f(argv[1],std::ios::binary);
 std::vector<char>d((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());
 mem=(uint8_t*)mmap(nullptr,d.size()+0x10000,7,MAP_PRIVATE|MAP_ANON,-1,0);assert(mem!=MAP_FAILED);
 memcpy(mem,d.data(),d.size());put(mem,0xc8200,(uintptr_t)&cookie);
 put(mem,0x14a6b8,0U);
 jump(0x42364,(uintptr_t)getMode);jump(0x8224,(uintptr_t)getScratch);
 jump(0x10b60,(uintptr_t)getVirtual);jump(0x13e7c,(uintptr_t)getGPU);
 jump(0x56818,(uintptr_t)getBuffer);jump(0x42692,(uintptr_t)serialize);jump(0x4266a,(uintptr_t)one);
 unsigned tramp=(d.size()+15)&~15U;jump(tramp,(uintptr_t)noop);call(0x41a23,tramp);
 jump(tramp+16,(uintptr_t)tglSemaphoreSpace);jump(tramp+32,(uintptr_t)tglSemaphoreCommand);
 tglOrigBaseTLBSpace=(uintptr_t)(mem+0x428ec);tglOrigBaseTLBWrite=(uintptr_t)(mem+0x4290c);
 tglCanUseSemaphore=(uintptr_t)(mem+0x42440);tglOrigSemaphoreCommand=(uintptr_t)(mem+0x4249e);
 tglRCSWriteBuffer=(uintptr_t)(mem+0x41e7a);
 auto wait=(void(*)(void*,unsigned,unsigned))(mem+0x4ee58);
 uintptr_t vtable[64]={};vtable[0x138/8]=(uintptr_t)noop;
 unsigned cases=0,missing=0,corrected=0;
 for(unsigned fix=0;fix<2;++fix) for(unsigned wa=0;wa<2;++wa)for(mode=0;mode<2;++mode)
 for(unsigned early=0;early<2;++early)for(unsigned serial=0;serial<2;++serial)
 for(unsigned newmap=0;newmap<2;++newmap)for(unsigned at:{0U,4080U}){
  mapped=newmap;
  memset(owner,0,sizeof(owner));memset(ring,0,sizeof(ring));memset(ctx,0,sizeof(ctx));
  memset(sched,0,sizeof(sched));memset(hw,0,sizeof(hw));memset(storage,0xa5,sizeof(storage));
  memset(tglSemaphoreCounters,0,sizeof(tglSemaphoreCounters));
  put(owner,0xfd6,wa<<16);put(owner,0x1190,0x40000000U|(mode?0U:0x100U));
  owner[0xc78]=2;put(owner,0x1250,(uintptr_t)sched);put(owner,0x388,(uintptr_t)stats);
  sched[0xc]=2|(serial?16:0);put(owner,0x1340,early?4ULL:0ULL);
  put(ring,0,(uintptr_t)vtable);put(ring,0x10,(uintptr_t)owner);put(ring,0x20,(uintptr_t)ctx);
  ring[0x40]=2;put(ring,0x18,(uintptr_t)hw);put(hw,0x10,2048U);
  put(ring,0x64,at);put(ring,0x88,2040U);put(ring,0x8c,4096U);put(ring,0x90,4095U);
  vtable[0x150/8]=fix?(uintptr_t)tglBaseTLBSpace:tglOrigBaseTLBSpace;
  vtable[0x160/8]=fix?(uintptr_t)tglBaseTLBWrite:tglOrigBaseTLBWrite;
  // Exercise the real entry replacement, not a relocated original wrapper.
  memcpy(mem+0x425f8,d.data()+0x425f8,17);
  if(fix)jump(0x425f8,(uintptr_t)tglSemaphoreSpace);
  call(0x42654,fix?tramp+32:0x4249e);
  wait(ring,113,0);
  unsigned end=*(uint32_t*)(ring+0x64);assert(end<2048);
  unsigned bbs=0,invalidations=0,disabled=0,enabled=0;
  for(unsigned i=0;i<end/4;++i){auto w=((uint32_t*)storage)[i];
   if((w&0xffffff00)==0x18800000 || (w&0xffffff00)==0x18800100) bbs=i+1;
   if((w&0xffff0000)==0x13040000)++invalidations;
   if(w==0x02800101)++disabled;if(w==0x02800100)++enabled;
  }
  assert(bbs && ((uint32_t*)storage)[bbs]==0x488d6000);
  const bool late=newmap && (!early || serial);
  if(!fix && late){assert(*(uint64_t*)(owner+0x1340)&4);++missing;}
  if(fix){
   assert(!(*(uint64_t*)(owner+0x1340)&4) && !ring[0x6d]);
   assert(tglSemaphoreCounters[2][0]==1 && tglSemaphoreCounters[2][1]==late && tglSemaphoreCounters[2][2]==late);
   assert(invalidations==unsigned(bool(early))+unsigned(late));
   assert(disabled==invalidations && enabled==invalidations);
   if(late)++corrected;
   if(late || (early && !serial)) assert(((uint32_t*)storage)[bbs-2]==0x02800100);
  }
  for(unsigned j=4096;j<sizeof(storage);++j) assert(storage[j]==0xa5);
  if(at) for(unsigned j=at;j<4096;++j)assert(storage[j]==0);
  ++cases;
 }
 // Preserve every native canUse rejection (including stamp index >59).
 memcpy(mem+0x425f8,d.data()+0x425f8,17);
 auto nativeSpace=(uint32_t(*)(void*,int))(mem+0x425f8);
 for(unsigned e=0;e<6;++e)for(int stamp:{0,1,33,60})for(unsigned on=0;on<2;++on){
  ring[0x40]=e;sched[0xc]=on?2:0;
  const auto n=nativeSpace(ring,stamp);
  assert(tglSemaphoreSpace(ring,stamp)==(n?n+tglBaseTLBSpace(ring):0));
 }
 printf("PASS: %u native BCS cases; %u late-map omissions reproduced, %u corrected; both WA/modes/early flush/ring wrap\n",cases,missing,corrected);
}
'''.replace('HELPERS',helpers)
with tempfile.TemporaryDirectory(prefix='tgl-sem-') as t:
 p=Path(t);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-arch','x86_64','-std=c++14','-O2',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test'),sys.argv[1]],check=True)
