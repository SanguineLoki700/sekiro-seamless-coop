#pragma once
#include <Windows.h>
#include <array>
#include <cstdint>
namespace skcoop::actors {
// Query results live for one serialized callback only. Clear again across a
// native allocation boundary; never keep page observations across game frames.
// A page can still change concurrently: guarded native reads remain necessary.
class RegionQueries {
public:
    void begin() noexcept { active_=true; clear(); }
    void end() noexcept { active_=false; clear(); }
    void clear() noexcept { count_=0; queries_=0; }
    std::size_t queries() const noexcept { return queries_; }
    bool query(std::uintptr_t p,MEMORY_BASIC_INFORMATION& out) noexcept {
        if(active_) for(std::size_t i=0;i<count_;++i) {
            const auto start=reinterpret_cast<std::uintptr_t>(regions_[i].BaseAddress);
            if(p>=start && p-start<regions_[i].RegionSize) { out=regions_[i]; return true; }
        }
        ++queries_;
        if(VirtualQuery(reinterpret_cast<void*>(p),&out,sizeof(out))!=sizeof(out))return false;
        if(active_ && count_<regions_.size())regions_[count_++]=out;
        return true;
    }
private:
    bool active_{};
    std::size_t count_{},queries_{};
    std::array<MEMORY_BASIC_INFORMATION,32> regions_{};
};
}
