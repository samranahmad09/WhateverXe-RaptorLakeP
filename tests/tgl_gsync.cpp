#include "kern_tgl_gsync.hpp"
#include <cassert>
#include <array>
int main() {
 using namespace TGLGSYNC;
 // Admission/wake failure must not even read an inaccessible register.
 for(auto guards: {std::array<bool,2>{false,false},{false,true},{true,false}}) {
  auto r=apply(guards[0],guards[1],true,[](uint32_t)->uint32_t {assert(false);return 0;},
   [](uint32_t,uint32_t){assert(false);});
  assert(r.status==Rejected && !r.wrote);
 }
 // Invalid readback is never used as the input to a destructive RMW.
 for(uint32_t value: {UINT32_MAX,0x80000000U,0x00800000U,1U}) {
  auto r=apply(true,true,true,[&](uint32_t reg){assert(reg==Register);return value;},
   [](uint32_t,uint32_t){assert(false);});
  assert(r.status==InvalidRegister && !r.wrote);
 }
 // Preserve every other writable bit, including the adjacent queue control.
 for(uint32_t value: {0U,2U,0x7FFFFAU,0x123450U,0x7FFFFEU}) {
  unsigned reads=0,writes=0;uint32_t actual=value;
  auto read=[&](uint32_t reg){assert(reg==Register);++reads;return actual;};
  auto write=[&](uint32_t reg,uint32_t v){assert(reg==Register);++writes;actual=v;};
  auto r=apply(true,true,true,read,write);
  assert(actual==(value|Disable) && (actual^value)==((value&Disable)?0:Disable));
  assert(r.before==value && r.after==actual);
  assert(r.wrote==!(value&Disable) && writes==unsigned(r.wrote));
  assert(reads==(r.wrote?2U:1U));
  assert(r.status==((value&Disable)?AlreadyDisabled:Applied));
  auto next=apply(true,true,false,read,write);
  assert(next.status==AlreadyDisabled && !next.wrote && writes==unsigned(r.wrote));
 }
 // A dropped write or changed unrelated field must not be reported applied.
 for(uint32_t observed: {0U,UINT32_MAX,6U}) {
  unsigned reads=0,writes=0;
  auto r=apply(true,true,true,[&](uint32_t){return reads++ ? observed:0U;},
   [&](uint32_t reg,uint32_t v){assert(reg==Register && v==Disable);++writes;});
  assert(r.status==ReadbackMismatch && r.wrote && writes==1 && reads==2);
 }
 // Never flex the global control after startup, even if it was lost.
 auto late=apply(true,true,false,[](uint32_t){return 2U;},
  [](uint32_t,uint32_t){assert(false);});
 assert(late.status==WriteInhibited && late.before==2 && !late.wrote);
}
