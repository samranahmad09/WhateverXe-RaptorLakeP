#!/usr/bin/env python3
"""CPU-execute the exact RCS completion emitter; verify native notification ABI."""
from pathlib import Path
import hashlib, re, subprocess, sys, tempfile
repo = Path(__file__).resolve().parents[1]
source = (repo/'WhateverXe/kern_xe.cpp').read_text()
data = Path(sys.argv[1]).read_bytes()
assert hashlib.sha256(data).hexdigest() in {
 '890735a93a19d3ddda24ff31bda3075dbd145fb7b26c11769f33c4564a8027ac',
 'bd1a21f5e622aadabdab6441f6b5c38ed73fa3053c783fdf0fb7e4b309f273d0'}
def arrays(name):
    body = re.search(r'const uint8_t '+name+r'\[\]\[10\] \{(.*?)\};', source, re.S).group(1)
    return [bytes(int(x.strip(), 0) for x in row.split(',')) for row in re.findall(r'\{([^}]+)\}', body)]
old, new = arrays('completionCacheFind'), arrays('completionCacheReplace')
for at, a, b in zip([0x84ea4, 0x84f95], old, new):
    assert data[at:at+10] == a
    expected = bytearray(a); expected[3] |= 2; expected[6] = (expected[6] & ~8) | 4; expected[9] |= 0x20
    assert b == expected
assert 'tahoeTrialScope=!adlnDetected && checkKernelArgument("-disablegfxfirmware")' in source
assert 'completionCacheRequested=tahoeTrialScope && !(tglTahoeDisabledTrials&1)' in source
assert 'recoveryOK && completionCacheOK' in source
first = arrays('flushReplace')[0]
code = r'''
#include <sys/mman.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <fstream>
#include <vector>
static uint64_t scratch,guard=0xfeed12345678ULL;
static unsigned mode;
static std::vector<uint32_t> output,lengths;
extern "C" uint32_t gtt(void*) {return mode;}
extern "C" uint64_t address(void*) {return scratch;}
extern "C" void writebuf(void*,uint32_t*p,unsigned n) {assert(n<64);lengths.push_back(n);output.insert(output.end(),p,p+n);}
void jump(uint8_t*p,void*f){p[0]=0x48;p[1]=0xb8;memcpy(p+2,&f,8);p[10]=0xff;p[11]=0xe0;}
template<class T> void put(uint8_t*p,unsigned off,T v){memcpy(p+off,&v,sizeof(v));}
int main(int argc,char**argv){
 assert(argc==2);std::ifstream f(argv[1],std::ios::binary);
 std::vector<char>d((std::istreambuf_iterator<char>(f)),{});
 auto mem=(uint8_t*)mmap(nullptr,d.size(),7,MAP_ANON|MAP_PRIVATE,-1,0);assert(mem!=MAP_FAILED);memcpy(mem,d.data(),d.size());
 jump(mem+0x42364,(void*)gtt);jump(mem+0x8224,(void*)address);jump(mem+0x41e7a,(void*)writebuf);put(mem,0xc8200,&guard);
 uint8_t self[0x100]={},owner[0x1200]={},context[0x100]={},task[0x100]={};
 put(self,0x10,owner);put(self,0x20,context);put(context,0x58,task);put(self,0x28,uint64_t(0x123456780040ULL));
 const uint8_t first[]={FIRST},old[][10]={OLD},fixed[][10]={FIXED};
 memcpy(mem+0x84e5d,first,10);
 unsigned cases=0,packets=0;
 for(auto s:{0x40000010ULL,0x123400001010ULL,0x123456789abfULL,0xfffffffffffcULL})
 for(mode=0;mode<2;++mode)for(unsigned path=0;path<2;++path)for(unsigned notify=0;notify<2;++notify)
 for(uint32_t index:{0u,17u,63u})for(uint32_t stamp:{0u,0x1234abcdu,0xffffffffu}){
  scratch=s;owner[0x1190]=path?0x40:0;put(self,0x38,index);
  std::vector<uint32_t>a,al,b,bl;
  for(unsigned variant=0;variant<2;++variant){
   memcpy(mem+0x84ea4,variant?fixed[0]:old[0],10);memcpy(mem+0x84f95,variant?fixed[1]:old[1],10);
   output.clear();lengths.clear();((void(*)(void*,uint32_t,bool))(mem+0x84dc8))(self,stamp,notify);
   if(!variant){a=output;al=lengths;}else{b=output;bl=lengths;}
  }
  assert(al==bl && a.size()==b.size() && a.size()==(path&&notify?22:12));
  assert(al[0]==6 && al[1]==6 && a[0]==0x7a000204 && (a[1]&(1u<<20)));
  assert(a[2]==(uint32_t(s)&~3u) && a[3]==uint32_t(s>>32));
  assert(a[8]==uint32_t(0x123456780040ULL+index*64) && a[9]==0x1234 && a[10]==stamp);
  // Only cache bits change in the stamp and, when present, notification PC.
  for(unsigned off:{6u,16u})if(off<a.size()){
   assert(a[off]==0x7a000004 && (a[off+1]&8));
   a[off]|=1u<<9;a[off+1]=(a[off+1]&~8u)|4u|(1u<<29);++packets;
  }
  assert(a==b);++cases;
 }
 printf("%u actual donor cases / %u PCs PASS: all output lengths, GTT mode, addresses, stamps, notify and optional semaphore writes preserved\n",cases,packets);
 munmap(mem,d.size());
}
'''
fmt = lambda value: ','.join(str(v) for v in value)
code = code.replace('FIRST',fmt(first)).replace('OLD',','.join('{'+fmt(v)+'}' for v in old)).replace('FIXED',','.join('{'+fmt(v)+'}' for v in new))
with tempfile.TemporaryDirectory(prefix='tgl-completion-cache-') as tmp:
    p=Path(tmp);(p/'test.cpp').write_text(code)
    subprocess.run(['clang++','-arch','x86_64','-std=c++14','-O2',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test'),str(Path(sys.argv[1]).resolve())],check=True)
print('Exact donor guards and scoped admission PASS; GPU behavior remains unproven')
