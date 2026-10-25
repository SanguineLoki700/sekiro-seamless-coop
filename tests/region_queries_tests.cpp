#include "region_queries.h"
#include <iostream>
#include <stdexcept>
using skcoop::actors::RegionQueries;
void require(bool v) { if(!v)throw std::runtime_error("Region query lifetime invariant failed"); }
int main() {
    SYSTEM_INFO system{}; GetSystemInfo(&system);
    auto* pages=static_cast<unsigned char*>(VirtualAlloc(nullptr,system.dwPageSize*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!pages)return 1;
    try {
        RegionQueries q; MEMORY_BASIC_INFORMATION a{},b{};
        const auto first=reinterpret_cast<std::uintptr_t>(pages);
        q.begin();
        for(unsigned i=0;i<30;++i)require(q.query(first+i,a) && a.State==MEM_COMMIT && a.Protect==PAGE_READWRITE);
        require(q.queries()==1);
        DWORD old{}; require(VirtualProtect(pages+system.dwPageSize,system.dwPageSize,PAGE_NOACCESS,&old)!=FALSE);
        q.clear();
        require(q.query(first,a) && q.query(first+system.dwPageSize,b));
        require(a.Protect==PAGE_READWRITE && b.Protect==PAGE_NOACCESS && q.queries()==2);
        q.end();
        require(VirtualProtect(pages+system.dwPageSize,system.dwPageSize,PAGE_READONLY,&old)!=FALSE);
        q.begin(); require(q.query(first+system.dwPageSize,b) && b.Protect==PAGE_READONLY && q.queries()==1);
        q.end();
        require(q.query(first,a) && q.query(first,a) && q.queries()==2);
        VirtualFree(pages,0,MEM_RELEASE);
        std::cout<<"PASS repeated-region reuse, protection boundary and frame/allocation invalidation\n";
    } catch(const std::exception& e) { VirtualFree(pages,0,MEM_RELEASE); std::cerr<<e.what()<<'\n'; return 1; }
}
