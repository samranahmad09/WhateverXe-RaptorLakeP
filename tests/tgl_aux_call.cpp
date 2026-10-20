#include "kern_tgl_aux_call.hpp"
#include <assert.h>
#include <map>
int main() {
 const uint64_t function=0xFFFFFF8000100000ULL,stub=function+0x1000,slot=stub+0x80;
 std::map<uint64_t,uint64_t> memory {
  {function,0x48535641E5894855ULL},{function+8,0x001848B78B4CFB89ULL},{function+16,0x123400},
  {stub,0x25FFULL|((slot-stub-6)<<16)},{slot,function}
 };
 auto read=[&](uint64_t a,uint64_t &v) {auto i=memory.find(a);if(i==memory.end())return false;v=i->second;return true;};
 uint64_t out=7;
 assert(TGLAuxCall::resolveSubmit(function,false,read,out) && out==function);
 assert(TGLAuxCall::resolveSubmit(stub,true,read,out) && out==function);
 out=7;assert(!TGLAuxCall::resolveSubmit(stub,false,read,out) && out==7);
 memory[slot]=stub;assert(!TGLAuxCall::resolveSubmit(stub,true,read,out)); // no nested thunk
 memory[slot]=1;assert(!TGLAuxCall::resolveSubmit(stub,true,read,out));
 memory.erase(slot);assert(!TGLAuxCall::resolveSubmit(stub,true,read,out));
 memory[slot]=function;
 for(uint64_t a:{function,function+8,function+16}) {
  auto before=memory[a];memory[a]^=1;
  assert(!TGLAuxCall::resolveSubmit(stub,true,read,out));memory[a]=before;
 }
 const uint64_t backward=stub-0x80;
 memory[stub]=0x25FFULL|(uint64_t(uint32_t(int32_t(-0x86)))<<16);memory[backward]=function;
 assert(TGLAuxCall::resolveSubmit(stub,true,read,out) && out==function);
 assert(!TGLAuxCall::resolveSubmit(0,false,read,out));
 assert(!TGLAuxCall::resolveSubmit(UINT64_MAX,false,read,out));
}
