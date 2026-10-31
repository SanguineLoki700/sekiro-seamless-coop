#pragma once
#include <cstddef>
namespace skcoop::launcher {
// Optional samples must leave space for final identity/save/lifecycle evidence.
class ReceiptBudget {
public:
    static constexpr std::size_t maximum=512*1024, final_reserve=128*1024, record_limit=64*1024;
    [[nodiscard]] bool permits(std::size_t json_bytes,bool optional=false) const noexcept {
        const auto limit=optional?maximum-final_reserve:maximum;
        return json_bytes<record_limit && bytes_<limit && json_bytes+1<limit-bytes_;
    }
    void committed(std::size_t json_bytes) noexcept { bytes_+=json_bytes+1; }
    [[nodiscard]] std::size_t bytes() const noexcept { return bytes_; }
private:
    std::size_t bytes_{};
};
}
