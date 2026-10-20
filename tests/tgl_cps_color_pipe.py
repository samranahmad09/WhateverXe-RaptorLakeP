#!/usr/bin/env python3
"""Validate native pointer consumer + candidate allocation/lifetime contract on CPU."""
from pathlib import Path
import hashlib,re,subprocess,sys,tempfile,struct
s=(Path(__file__).resolve().parents[1]/'WhateverXe/kern_xe.cpp').read_text()
d=Path(sys.argv[1]).read_bytes()
assert hashlib.sha256(d).hexdigest()=='bd1a21f5e622aadabdab6441f6b5c38ed73fa3053c783fdf0fb7e4b309f273d0'
for name,at in [('cpsAllocHead',0x10924),('cpsCPUHead',0x10b60),('cpsGPUHead',0x13e7c),('cpsPointer',0x7c208),('cpsRelease',0x26683),('cpsRestoreHead',0x7c392),('cpsRestoreCall',0x7c129),('contextCacheHead',0x7b9d4)]:
 vals=re.search(r'const uint8_t '+name+r'\[\] \{([^}]+)',s)[1]
 b=bytes(int(v.strip(),0) for v in vals.split(','));assert b==d[at:at+len(b)],name
assert struct.unpack_from('<I',d,0xc04a0+0x48)[0]==0x21c0
assert '&& ffModeOK && cpsOK && flushOK' in s
assert s.index('const uint8_t cpsAllocHead') < s.index('KernelPatcher::RouteRequest gucRequests[]')
assert 'cpsOK=cpsOK && hostOK && gucOK;' in s
helper=s[s.index('uintptr_t tglCPSAllocate'):s.index('bool tglCPSSerialize')]
helper+=s[s.index('static bool tglContextCacheInit(void'):s.index('// r150:')]
code=r"""#include "kern_tgl_context_cache.hpp"
#include <stdint.h>
#include <sys/mman.h>
#include <cassert>
#include <cstring>
#include <strings.h>
#include <fstream>
#include <vector>
#include <cstdio>
static unsigned released=0;
struct OSObject {void release(){++released;}};
#define FunctionCast(fn,addr) reinterpret_cast<decltype(&fn)>(addr)
HELPER
static void adlnCaptureOwnedGolden(void*){assert(false && "ADL-N path outside this fixture");}
static bool tglAlderLakeNGoldenRestore(void*){assert(false && "ADL-N path outside this fixture");return false;}
alignas(4096) static uint8_t page[0x40000];
static uint8_t saved[0x40000],descriptor[8];static void *descriptorVT[100];
static void *savedAddress(void *d){assert(d==descriptor);return saved;}
static uint8_t buffer[0x48],map[0x100];static void *vtable[64];
static uint64_t addr=0x34567000;static unsigned allocations=0;static bool allocFail=false,cpuFail=false;static void *expectedTask;
static void *alloc(void *task,size_t n,unsigned options,unsigned fill){assert(task==expectedTask&&n==(tglContextCacheEnabled?8192U:4096U)&&options==3&&fill==0);++allocations;return allocFail?nullptr:buffer;}
static void *cpu(void *b){assert(b==buffer);return cpuFail?nullptr:page;}
static uint64_t gpu(void *m){assert(m==map);return addr;}
static bool initOK=true;
static bool fakeInit(void*,void*,const void*,uint8_t){return initOK;}
extern "C" void consume(void*,void*,void*);
asm(".text\n_globl_dummy:\n.globl _consume\n_consume:\npushq %r13\npushq %r15\nsubq $8,%rsp\nmovq %rdi,%r13\nmovq %rsi,%r15\ncallq *%rdx\naddq $8,%rsp\npopq %r15\npopq %r13\nretq\n");
static void put(void *base,size_t off,void *p){memcpy((char*)base+off,&p,8);}
int main(int argc,char**argv){
 assert(argc==2);std::ifstream f(argv[1],std::ios::binary);std::vector<char>d((std::istreambuf_iterator<char>(f)),{});
 auto code=(uint8_t*)mmap(nullptr,d.size(),7,MAP_PRIVATE|MAP_ANON,-1,0);assert(code!=MAP_FAILED);memcpy(code,d.data(),d.size());
 assert(code[0x7c21f]==1);code[0x7c21f]=5;code[0x7c263]=0xc3;
 tglCPSAllocate=(uintptr_t)alloc;tglCPSCPUAddress=(uintptr_t)cpu;tglCPSGPUAddress=(uintptr_t)(code+0x13e7c);
 put(buffer,0x30,map);put(map,0,vtable);vtable[0x128/8]=(void*)gpu;
 uint8_t owner[0x1500]={},task[0x300]={},manager[0x100]={},ctx[0x100]={};void *link[3]={manager,nullptr,nullptr};
 put(owner,0x150,task);put(owner,0x1260,manager);put(task,0x10,owner);put(task,0x268,link);put(manager,0x98,manager);expectedTask=task;put(ctx,0x50,owner);
 memset(page,0xA5,sizeof(page));assert(tglCPSPrepare(owner));assert(allocations==1&&released==0);
 const uint32_t payload[]={0,0x11000005,0x7304,0x02000200,0x6604,0xE0040000,0xE4F4,0x40004000,0x05000000};assert(!memcmp(page,payload,sizeof(payload)));
 for(unsigned i=sizeof(payload);i<4096;++i)assert(page[i]==0);
 for(unsigned i=4096;i<8192;++i)assert(page[i]==0xA5);
 assert(*(void**)(owner+0x13f8)==buffer);assert(tglCPSCounts[2]==addr&&tglCPSCounts[3]==(addr|5));
 uint8_t image[0x1200];memset(image,0x5A,sizeof(image));consume(ctx,image,code+0x7c208);
 assert(*(uint32_t*)(image+0x104c)==(addr|5));for(unsigned i=0;i<sizeof(image);++i)if(i<0x104c||i>=0x1050)assert(image[i]==0x5A);
 page[4000]=0x42;assert(tglCPSPrepare(owner));assert(allocations==1&&page[4000]==0x42); // reuse never rewrites
 put(owner,0x13f8,nullptr);memset(image,0x5A,sizeof(image));consume(ctx,image,code+0x7c208);for(auto b:image)assert(b==0x5A);
 for(auto bad:{0ULL,1ULL,0x100000000ULL}){addr=bad;auto n=released;assert(!tglCPSPrepare(owner));assert(released==n+1&&*(void**)(owner+0x13f8)==nullptr);}
 addr=0x34567000;cpuFail=true;assert(!tglCPSPrepare(owner));cpuFail=false;
 allocFail=true;auto n=released;assert(!tglCPSPrepare(owner));assert(n==released);allocFail=false;
 link[0]=nullptr;n=allocations;assert(!tglCPSPrepare(owner));assert(n==allocations);link[0]=manager;
 assert(!tglCPSPrepare(nullptr));
 // Execute actual native safe-image clone with its two imported memcpy calls
 // directed to libc. The real descriptor size is read from the native table.
 code[0x1000]=0x48;code[0x1001]=0xb8;void *memcpyPtr=(void*)memcpy;memcpy(code+0x1002,&memcpyPtr,8);code[0x100a]=0xff;code[0x100b]=0xe0;
 for(unsigned at:{0x7c3f6U,0x7c450U}){assert(code[at]==0xe8);int32_t delta=0x1000-(at+5);memcpy(code+at+1,&delta,4);}
 const auto imageSize=*(uint32_t*)(code+0xdc5b0+0x55);assert(imageSize>0x1400&&imageSize<=sizeof(page));
 put(descriptor,0,descriptorVT);descriptorVT[0x2e0/8]=(void*)savedAddress;
 put(owner,0x1268,descriptor);put(owner,0x13f8,buffer);put(ctx,0x98,buffer);put(buffer,0x38,page);
 *(uint64_t*)(buffer+0x20)=sizeof(page);memset(saved,0x3B,sizeof(saved));*(uint32_t*)(saved+0x1048)=0x21c0;*(uint32_t*)(saved+0x104c)=0;
 tglCPSRestoreOriginal=(uintptr_t)(code+0x7c392);tglCPSRestoreCaller=0x7c131;
 auto nativeRestore=(bool(*)(void*))(code+0x7c392);assert(nativeRestore(ctx));assert(*(uint32_t*)(page+0x104c)==0);
 std::vector<uint8_t> before(page,page+sizeof(page));assert(tglCPSRestorePointer(ctx,0x7c131));assert(*(uint32_t*)(page+0x104c)==(addr|5));
 for(unsigned i=0;i<sizeof(page);++i)if(i<0x104c||i>=0x1050)assert(page[i]==before[i]);
 assert(tglCPSCounts[12]==1&&tglCPSCounts[13]==1&&tglCPSCounts[15]==1);
 assert(nativeRestore(ctx));before.assign(page,page+sizeof(page));assert(tglCPSRestorePointer(ctx,0x7c91e));assert(!memcmp(page,before.data(),sizeof(page)));
 // Wrapper still executes the original for unknown callers; no mutation.
 assert(tglCPSRestore(ctx));assert(!memcmp(page,before.data(),sizeof(page)));
 *(uint32_t*)(page+0x1048)=0x9999;assert(!tglCPSRestorePointer(ctx,0x7c131));assert(tglCPSCounts[16]==1);
 put(owner,0x1268,nullptr);assert(!tglCPSRestore(ctx));
 printf("Native safe clone size0x%x: pointer reinstated, all other bytes preserved; reset/unknown callers unchanged\n",imageSize);

 for(uint32_t old:{0U,0x800U,0xffffU}){auto word=payload[3],mask=word>>16;assert(((old&~mask)|(word&mask))==(old|0x200));}
 assert((payload[1]&0xff)+2==7 && payload[8]==0x05000000); // 3 LRI pairs + batch end
 for(uint32_t old=0;old<65536;++old){auto word=payload[7],mask=word>>16;assert(((old&~mask)|(word&mask))==(old|0x4000));}
 tglContextCacheEnabled=true;put(owner,0x13f8,nullptr);memset(page,0xA5,sizeof(page));assert(tglCPSPrepare(owner));
 assert(!memcmp(page,payload,sizeof(payload)));const uint32_t indirect[]={0x11000001,0x20D8,0x00400040};
 assert(!memcmp(page+4096,indirect,sizeof(indirect)));for(unsigned i=4108;i<8192;++i)assert(!page[i]);
 for(unsigned i=8192;i<sizeof(page);++i)assert(page[i]==0xA5);
 const auto allocBefore=allocations;page[4200]=0x73;assert(tglCPSPrepare(owner));assert(allocations==allocBefore&&page[4200]==0x73);
 tglContextCacheInitOriginal=(uintptr_t)fakeInit;const uint32_t fields[]={0x21c0,uint32_t(addr|5),0x21c4,0,0x21c8,0};
 memcpy(page+0x1048,fields,sizeof(fields));assert(tglContextCacheInit(ctx,task,nullptr,0));
 assert(*(uint32_t*)(page+0x1054)==(addr+4096+1));assert(*(uint32_t*)(page+0x105c)==0x340);
 before.assign(page,page+sizeof(page));assert(tglContextCacheInit(ctx,task,nullptr,0));assert(!memcmp(page,before.data(),sizeof(page)));
 *(uint32_t*)(page+0x1054)=0x12345678;before.assign(page,page+sizeof(page));assert(!tglContextCacheInit(ctx,task,nullptr,0));assert(!memcmp(page,before.data(),sizeof(page)));
 ctx[0x6c]=1;assert(tglContextCacheInit(ctx,task,nullptr,0));assert(!memcmp(page,before.data(),sizeof(page)));ctx[0x6c]=0;
 initOK=false;assert(!tglContextCacheInit(ctx,task,nullptr,0));initOK=true;tglContextCacheEnabled=false;
 assert(tglContextCacheInit(ctx,task,nullptr,0));assert(!memcmp(page,before.data(),sizeof(page)));
 puts("PASS: optional 8KiB allocation keeps native batch, separate indirect page, immutable reuse, constructor failure/non-RCS/no-op paths");
 puts("PASS: native per-context pointer consumer; GGTT prerequisite, 4KiB extent, valid/force bits, reuse and failure release; CPS plus early-read masked bit14, FF timer and 3-pair LRI extent");
}
""".replace('HELPER',helper)
with tempfile.TemporaryDirectory(prefix='tgl-cps-context-') as t:
 p=Path(t);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-arch','x86_64','-std=c++14','-O2','-I'+str(Path(__file__).resolve().parents[1]/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test'),sys.argv[1]],check=True)
print('CPU contract validation; GPU execution and pixel effects are not established by this test.')
