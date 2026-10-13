#include "kern_xelp_platform.hpp"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <initializer_list>
using namespace XELP;
int main() {
 unsigned implemented=0;
 for(const auto &entry:DeviceTable) {
  const uint32_t pci=uint32_t(entry.device)<<16|0x8086;
  // Each backend is qualified for one physical stepping: RPL-P revision 4, the others revision 0.
  const uint32_t rev=entry.family==Family::RaptorLakeP ? 4 : 0;
  auto p=identify(pci,rev);
  assert(p.identity==pci && p.effectiveIdentity==pci && !p.overridden());
  assert(p.family==entry.family && p.display==entry.display && p.gt==entry.backend);
  assert(!strcmp(model(p),entry.model));
  assert(p.admitted()==(entry.backend!=GT::Unknown));
  implemented+=p.admitted();
  const uint32_t value=entry.device;
  assert(same(p,select(pci,rev,true,&value,4)));
  // Every catalogued backend can be selected for an unknown physical ID.
  auto f=select(0x12348086,rev,true,&value,4);
  assert(f.overridden() && f.identity==0x12348086 && f.effectiveIdentity==pci);
  assert(f.admitted()==p.admitted());
  assert(!strcmp(model(f),entry.model));
  auto nonIntel=select(0x123410DE,rev,true,&value,4);assert(!nonIntel.admitted());
  if(entry.family==Family::RaptorLakeP)for(uint32_t other:{0U,1U,3U,5U,255U})assert(!identify(pci,other).admitted());
 }
 assert(implemented==9); // Recognition of other table rows adds no native backend.
 uint32_t id=0x46D2;
 assert(select(0x12348086,0,true,&id,4).admitted());
 assert(!select(0x12348086,1,true,&id,4).admitted()); // Keep physical stepping guard.
 assert(!select(0x12348086,0,false,nullptr,0).admitted());
 for(size_t n:{size_t(0),size_t(1),size_t(2),size_t(3),size_t(5),size_t(8)})
  assert(!select(0x9A498086,0,true,&id,n).admitted());
 assert(!select(0x9A498086,0,true,nullptr,4).admitted());
 for(uint32_t bad:{0U,0xFFFFU,0x808646D2U,0x1234U})
  assert(!select(0x9A498086,0,true,&bad,4).admitted());
 auto p=select(0x46A68086,0,true,&id,4);assert(p.admitted() && p.family==Family::AlderLakeN);
 assert(!strcmp(model(p),"Intel Iris Xe Graphics")); // Name is physical, backend is overridden.
 auto q=p;q.effectiveIdentity=0x46D18086;assert(!same(p,q));
 ProbeConfig c;c.profile=p;c.combo=c.comboBackend=3;assert(c.valid());
 c.abi=2;assert(!c.valid());c.abi=3;c.profile.gt=GT::TGLGT2;assert(!c.valid());
 puts("PASS device table, physical/effective identity, strict override bytes/vendor/revision, model choice and ABI3");
}
