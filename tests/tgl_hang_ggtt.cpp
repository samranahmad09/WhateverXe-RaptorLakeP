#include "kern_tgl_hang_ggtt.hpp"
#include <cassert>
#include <initializer_list>
using namespace TGLHangGGTT;
int main() {
 constexpr uint64_t cpu=0xffff800000001000ULL,gpu=0x40200000,pa=0x212345000;
 unsigned reads=0,walks=0;uint64_t value=pa|1;
 auto physical=[&](uint64_t p,uint64_t &v){++walks;assert(p==cpu);v=pa;return true;};
 auto pte=[&](uint64_t g){++reads;assert(g==gpu);return value;};
 Capture c{};page(c,0,0,cpu,gpu,pa,true,physical,pte);
 assert(c.q[4]==1 && !c.q[5] && !c.q[6] && !c.q[7] && reads==2 && walks==2);
 value=pa+4096+1;page(c,0,1,cpu,gpu,pa,true,physical,pte);assert(c.rows[1][7]==4);
 value=pa;page(c,0,2,cpu,gpu,pa,true,physical,pte);assert(c.rows[2][7]==4);
 value=pa|1;auto changing=[&](uint64_t g){auto v=pte(g);value+=4096;return v;};
 page(c,0,3,cpu,gpu,pa,true,physical,changing);assert(c.rows[3][7]==6);
 const auto count=reads;
 for(uint64_t bad:{uint64_t(0),gpu+1,uint64_t(0x100000000ULL)})page(c,1,0,cpu,bad,pa,true,physical,pte);
 page(c,1,0,cpu,gpu,pa,false,physical,pte);
 assert(reads==count && c.q[6]==4);
 auto changedCPU=[&](uint64_t,uint64_t &v){v=pa+4096;return true;};value=pa|1;
 page(c,1,0,cpu,gpu,pa,true,changedCPU,pte);assert(c.rows[8][7]==2);
 auto missing=[](uint64_t,uint64_t &){return false;};
 page(c,1,0,cpu,gpu,pa,true,missing,pte);assert(c.rows[9][7]==1);
 c.q[4]=29;const auto end=reads;page(c,1,0,cpu,gpu,pa,true,physical,pte);assert(reads==end);
}
