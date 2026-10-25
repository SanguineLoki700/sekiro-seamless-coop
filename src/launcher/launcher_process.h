#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace skcoop::launcher {
std::filesystem::path executable_directory();
std::wstring quote_argument(const std::wstring& argument);
int run_controlled_child(const std::filesystem::path& executable,
    const std::vector<std::wstring>& arguments, unsigned long timeout_ms = 15000);
}
