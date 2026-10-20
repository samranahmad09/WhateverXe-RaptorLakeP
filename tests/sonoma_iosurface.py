#!/usr/bin/env python3
"""Execute the real Sonoma matrix bridge; optionally audit exact Apple inputs.

Covers every byte-valued matrix, native branch/flags, all unaffected GPRs and
source buffers. This does not prove GPU/display correctness.
"""
from pathlib import Path
import argparse,re,subprocess,tempfile
R=Path(__file__).resolve().parents[1]
s=(R/'WhateverXe/kern_xe.cpp').read_text()
p=argparse.ArgumentParser();p.add_argument('--tahoe',action='store_true');p.add_argument('--accelerator',type=Path);p.add_argument('--framebuffer',type=Path);p.add_argument('--surface',type=Path);a=p.parse_args()
bridge_name='tglTahoeMatrixBridge' if a.tahoe else 'tglSonomaMatrixBridge'
bridge=re.search(r'extern "C" __attribute__\(\(naked,used\)\) void '+bridge_name+r'\(\) \{.*?\n\}',s,re.S)[0]
block=s[s.index('static bool tglInstallSonomaSurfaceABI'):s.index('XE *XE::callbackXE')]
for name,path in [('accel',a.accelerator),('fb',a.framebuffer)]:
 if not path:continue
 d=path.read_bytes()
 spec=re.search(r'const Site '+name+r'\[\] \{(.*?)\n \};',block,re.S)[1]
 for off,ln,before,after in re.findall(r'\{(0x[0-9A-F]+),(\d+),\{([^}]+)\},\{([^}]+)\}',spec):
  before=bytes(int(x,0) for x in before.split(','));assert len(before)==int(ln)
  assert d[int(off,16):int(off,16)+int(ln)]==before,(name,off)
 if name=='accel':
  matrix=bytes(int(x,0) for x in re.search(r'const uint8_t matrix\[\] \{([^}]+)\}',block)[1].split(','))
  off=int(re.search(r'address\+(0x[0-9A-F]+)\),matrix',block)[1],16)
  assert d[off:off+len(matrix)]==matrix
  # Decode both displaced conditional branch destinations independently.
  for at,n in [(8,'Zero'),(13,'Other')]:
   dest=off+at+2+int.from_bytes(matrix[at+1:at+2],'little',signed=True)
   expect=int(re.search('tglSonomaMatrix'+n+r'=address\+(0x[0-9A-F]+)',block)[1],16)
   assert dest==expect
  assert off+len(matrix)==int(re.search(r'tglSonomaMatrixOne=address\+(0x[0-9A-F]+)',block)[1],16)
 print('PASS: source-derived exact donor guards',name)
if a.surface:
 # Native exported getters establish the new field meanings, not folder names.
 text=subprocess.check_output(['otool','-tvV',str(a.surface)],text=True)
 for name,instructions in [('getPixelFormat',['0x80(%rdi)']),('getPlaneCount',['0x98(%rdi)']),('getSurfaceID',['0x10(%rdi)']),('getYCbCrMatrix',['0x178(%rdi)','0x6e(%rax)' if a.tahoe else '0x6a(%rax)'])]:
  body=text[text.index('IOSurface'+str(len(name))+name+'Ev:'):].split('retq',1)[0]
  assert all(i in body for i in instructions),name
 print('PASS: native IOSurface getters confirm all 4 fields')
cpp=r'''#include <stdint.h>
#include <cstring>
#include <cassert>
#include <cstdio>
extern "C" {
 uintptr_t tglSonomaMatrixZero,tglSonomaMatrixOne,tglSonomaMatrixOther;
 uint64_t in[16],out[16],outFlags;uint32_t branch;
 alignas(16) uint8_t surface[512],attachment[256];
}
BRIDGE
extern "C" __attribute__((naked)) void done() {asm volatile(
 CAPTURE
 "pushfq\n popq _outFlags(%rip)\n"
 "popq %r15\n popq %r14\n popq %r13\n popq %r12\n popq %rbx\n popq %rbp\n retq\n");}
extern "C" __attribute__((naked)) void zero() {asm volatile("movl $0,_branch(%rip)\n jmp _done\n");}
extern "C" __attribute__((naked)) void one() {asm volatile("movl $1,_branch(%rip)\n jmp _done\n");}
extern "C" __attribute__((naked)) void other() {asm volatile("movl $2,_branch(%rip)\n jmp _done\n");}
extern "C" __attribute__((naked)) void run() {asm volatile(
 "pushq %rbp\n pushq %rbx\n pushq %r12\n pushq %r13\n pushq %r14\n pushq %r15\n"
 LOAD
 "jmp _tglSonomaMatrixBridge\n");}
int main() {
 tglSonomaMatrixZero=(uintptr_t)zero;tglSonomaMatrixOne=(uintptr_t)one;tglSonomaMatrixOther=(uintptr_t)other;
 for(unsigned value=0;value<256;++value) {
  memset(surface,0xA5,sizeof(surface));memset(attachment,0x5A,sizeof(attachment));
  auto ptr=(uintptr_t)attachment;memcpy(surface+0x178,&ptr,8);attachment[0x6A]=value;
  uint8_t oldSurface[512],oldAttachment[256];memcpy(oldSurface,surface,512);memcpy(oldAttachment,attachment,256);
  for(unsigned i=0;i<16;++i)in[i]=0xAABBCCDD00001000ULL+i*0x1000;
  in[0]=(uintptr_t)surface;run();
  assert(out[0]==value);for(unsigned i=1;i<16;++i)if(i!=4)assert(out[i]==in[i]);
  assert(branch==(value==0?0:value==1?1:2));
  unsigned result=value?value-1:0;unsigned parity=__builtin_parity(result&255)==0;
  assert((outFlags&0x8C5)==((result?0:0x40)|(parity?4:0)));
  assert(!memcmp(surface,oldSurface,512));assert(!memcmp(attachment,oldAttachment,256));
 }
 puts("PASS: 256 actual bridge executions; matrix, all branches, flags, GPRs and source buffers");
}
'''
regs=['rax','rbx','rcx','rdx','rsp','rbp','rsi','rdi']+['r'+str(i) for i in range(8,16)]
capture='\n'.join('"movq %'+r+', _out+'+str(8*i)+'(%rip)\\n"' for i,r in enumerate(regs))
load='\n'.join('"movq _in+'+str(8*i)+'(%rip), %'+r+'\\n"' for i,r in enumerate(regs) if r!='rsp')
if a.tahoe:cpp=cpp.replace('jmp _tglSonomaMatrixBridge','jmp _tglTahoeMatrixBridge').replace('attachment[0x6A]','attachment[0x6E]')
cpp=cpp.replace('BRIDGE',bridge).replace('CAPTURE',capture).replace('LOAD',load)
with tempfile.TemporaryDirectory(prefix='sonoma-surface-') as t:
 p=Path(t);(p/'test.cpp').write_text(cpp)
 subprocess.run(['clang++','-arch','x86_64','-std=c++17','-O2',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
