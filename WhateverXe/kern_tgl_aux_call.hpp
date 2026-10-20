#pragma once
#include <stdint.h>

namespace TGLAuxCall {
// The exact submitBuffer prologue is shared by the qualified native families.
// AuxKC can insert one RIP-relative indirect jump before that function.
template <typename Read>
bool resolveSubmit(uint64_t call, bool allowThunk, Read read, uint64_t &target) {
 auto kernel=[](uint64_t address) {return (address>>47)==0x1FFFF && address<=UINT64_MAX-24;};
 uint64_t first=0,second=0,last=0;
 if(!kernel(call) || !read(call,first))return false;
 if((first&0xFFFF)==0x25FF) {
  if(!allowThunk)return false;
  const int32_t displacement=static_cast<int32_t>(first>>16);
  const uint64_t slot=call+6+static_cast<int64_t>(displacement);
  if(!kernel(slot) || !read(slot,call) || !kernel(call) || !read(call,first))return false;
 }
 if(first!=0x48535641E5894855ULL || !read(call+8,second) ||
    second!=0x001848B78B4CFB89ULL || !read(call+16,last) || (last&0xFF))return false;
 target=call;return true;
}
}
