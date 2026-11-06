#!/usr/bin/env python3
"""Exercise the actual seven-argument VM wrapper with a mock original and vnodes, for both page-correction targets."""
from pathlib import Path
import os, subprocess, tempfile

root = Path(__file__).resolve().parents[1]
source = (root/'WhateverXe/kern_rplp_gl_urb_runtime.cpp').read_text()
body = source[source.index('void validate('):source.index('\nbool hashMatches(')]
code = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include "kern_rplp_gl_urb.hpp"
#include "kern_rplp_va_vpp.hpp"
constexpr size_t PageSize=RPLPGLURB::PageSize;
struct Node {unsigned generation=7;bool readonly=true;};
using vnode_t=Node *;using memory_object_t=void *;using memory_object_offset_t=uint64_t;
unsigned vnode_vid(Node *v){return v->generation;}Node *vnode_mount(Node *v){return v;}
bool vfs_isrdonly(Node *v){return v->readonly;}
enum class Setup : uint32_t {Halted=11};
enum class Kind : unsigned { GL, VA, Count };
struct Target {const char *flag;uint64_t pageOffset;vnode_t vnode;unsigned generation;bool armed;uint32_t setup;uint64_t counters[8];};
Node glFile,vaFile,other;
Target targets[2] {{"gl",0,&glFile,7,true,10,{}},{"va",RPLPVAVPP::PageOffset,&vaFile,7,true,10,{}}};
RPLPGLURB::Plan glPlan;RPLPVAVPP::Plan vaPlan;
unsigned calls,writes,enters,ends;int publisher=3;bool fail,restoreFail,allow;
int expectedV,expectedT,expectedN;vnode_t expectedNode;void *expectedPager;
uint64_t expectedOffset;const void *expectedData;
int *expectedVP,*expectedTP,*expectedNP;
void original(vnode_t vp,void *pager,uint64_t offset,const void *data,int *v,int *t,int *n) {
 assert(!calls && !writes && !enters);++calls;
 assert(vp==expectedNode && pager==expectedPager && offset==expectedOffset && data==expectedData);
 assert(v==expectedVP && t==expectedTP && n==expectedNP);
 *v=expectedV;*t=expectedT;*n=expectedN;
}
auto originalValidate=original;
#define FunctionCast(type,address) (address)
struct WritePage {
 vnode_t vp;unsigned generation;const bool *armed;
 bool begin(){assert(calls==1);return allow;}
 void copy(uint8_t *to,const uint8_t *from,size_t size){++writes;
   if(fail && writes==1){to[0]^=1;return;}
   if(restoreFail && writes==2){to[size>1?1:0]^=1;if(size==1)to[0]^=1;return;}
   memcpy(to,from,size);
 }
 void end(){++ends;}
};
void thread_call_enter(int p){assert(p==publisher && calls==1);++enters;}
[[noreturn]] void panic(const char *){throw 1;}
''' + body + r'''
int main(){
 glPlan.ready=vaPlan.ready=true;
 for(unsigned i=0;i<PageSize;++i)glPlan.original[i]=uint8_t(i);
 memcpy(glPlan.corrected,glPlan.original,PageSize);memset(glPlan.corrected+RPLPGLURB::Shim,0x91,RPLPGLURB::ShimSize);
 for(unsigned i=0;i<PageSize;++i)vaPlan.original[i]=uint8_t(i*7+3);
 vaPlan.original[RPLPVAVPP::ByteOffset]=RPLPVAVPP::ByteBefore;
 memcpy(vaPlan.corrected,vaPlan.original,PageSize);vaPlan.corrected[RPLPVAVPP::ByteOffset]=RPLPVAVPP::ByteAfter;
 uint8_t page[PageSize];int v,t,n,pager;
 for(unsigned k=0;k<2;++k)for(unsigned test=0;test<14;++test){
  auto &tg=targets[k];Node &file=k?vaFile:glFile;
  const uint8_t *orig=k?vaPlan.original:glPlan.original,*corr=k?vaPlan.corrected:glPlan.corrected;
  calls=writes=enters=ends=0;memset(tg.counters,0,sizeof(tg.counters));
  targets[0].armed=targets[1].armed=true;tg.armed=test!=1;tg.setup=10;file={};allow=test!=9;fail=test>=11 && test<=12;restoreFail=test==12;
  expectedV=test==5?1:15;expectedT=test==6?1:0;expectedN=test==7?1:0;
  expectedNode=test==2?&other:&file;expectedPager=&pager;
  expectedOffset=test==3?tg.pageOffset+4096:test==13?targets[1-k].pageOffset:tg.pageOffset;
  expectedData=page;expectedVP=&v;expectedTP=&t;expectedNP=&n;
  if(test==4)file.generation=8;
  if(test==8)file.readonly=false;
  memcpy(page,test==10?corr:orig,PageSize);v=t=n=-1;
  bool panicked=false;try{validate(expectedNode,&pager,expectedOffset,page,&v,&t,&n);}catch(int){panicked=true;}
  assert(calls==1 && v==expectedV && t==expectedT && n==expectedN);
  if(test==0)assert(writes==1 && enters==1 && tg.counters[unsigned(RPLPGLURB::Result::Applied)]==1 && !memcmp(page,corr,PageSize));
  else if(test<11 || test==13)assert(!writes && !memcmp(page,test==10?corr:orig,PageSize));
  else {assert(!tg.armed && tg.setup==11 && writes==2 && ends==1);
        if(test==11)assert(!memcmp(page,orig,PageSize));}
  assert(panicked==(test==12));
  if((test>=1 && test<=3) || test==13)assert(!enters);
  // The other target's state is never touched by this target's page.
  assert(targets[1-k].armed && !targets[1-k].counters[unsigned(RPLPGLURB::Result::Applied)]);
 }
 puts("PASS actual page-correction wrapper: seven arguments, original exactly once/first, unchanged verdict, identity/offset guards per target, the other target's page untouched, rollback and halt");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    cpp=Path(tmp)/'wrapper.cpp';exe=Path(tmp)/('wrapper.exe' if os.name=='nt' else 'wrapper')
    cpp.write_text(code)
    subprocess.run([os.environ.get('CXX','clang++'),'-std=c++17','-Wall','-Wextra',
                    '-I'+str(root/'WhateverXe'),str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
