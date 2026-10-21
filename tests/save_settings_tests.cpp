#include "skcoop/save_settings.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <array>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using namespace skcoop::saves;
namespace fs = std::filesystem;

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
template <typename Action> void invalid(Action action) {
    bool rejected{};
    try { action(); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "invalid settings were accepted");
}
std::string ini(std::string_view root = {}, std::string_view extension = "co2",
                std::string_view mode = "separate_profile") {
    return "[SAVE]\nsave_file_extension=" + std::string(extension) +
        "\ndestination_mode=" + std::string(mode) + "\ncoop_profile_root=" + std::string(root) + "\n";
}
std::string maximum_ini() {
    auto bytes = ini();
    bytes += ';';
    bytes.append(max_save_settings_bytes - bytes.size(), 'x');
    return bytes;
}

class Fixture {
public:
    Fixture() : parent_(fs::temp_directory_path().lexically_normal()) {
        const auto prefix = L"skcoop-settings-\u4fdd\u5b58-" + std::to_wstring(GetCurrentProcessId()) +
                            L"-" + std::to_wstring(GetTickCount64()) + L"-";
        for (int attempt = 0; attempt < 100; ++attempt) {
            const auto candidate = parent_ / (prefix + std::to_wstring(attempt));
            std::error_code error;
            if (fs::create_directory(candidate, error)) { root_ = candidate; break; }
            if (error) throw std::runtime_error("Cannot create isolated settings fixture.");
        }
        require(!root_.empty(), "Settings fixture name exhausted.");
    }
    ~Fixture() {
        if (!root_.empty() && root_.is_absolute() && root_.parent_path() == parent_ &&
            root_.filename().wstring().starts_with(L"skcoop-settings-")) {
            std::error_code ignored;
            fs::remove_all(root_, ignored);
        }
    }
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;
    fs::path path() const { return root_ / L"config-\u65e5\u672c.ini"; }
    void write(const std::string& bytes) const {
        std::ofstream output(path(), std::ios::binary | std::ios::trunc);
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        require(static_cast<bool>(output), "Cannot write owned settings fixture.");
    }
private:
    fs::path parent_;
    fs::path root_;
};

void empty_root_is_explicitly_incomplete() {
    const auto settings = parse_save_settings(ini());
    require(settings.extension == L"co2" && settings.mode == DestinationMode::separate_profile &&
            settings.coop_profile_root.empty(), "empty root changed settings");
    const auto same = parse_save_settings(ini({}, "co2", "same_account_directory"));
    require(same.mode == DestinationMode::same_account_directory && same.coop_profile_root.empty(),
            "empty root should parse without claiming readiness");
}

void bom_crlf_comments_whitespace_unicode() {
    const auto text = std::string("\xef\xbb\xbf") +
        " ; comment\r\n\t# comment\r\n [SAVE] \r\n save_file_extension = CoOp9 \r\n"
        " destination_mode = separate_profile\r\n coop_profile_root = C:\\coop-\xc3\xa9-"
        "\xe4\xbf\x9d\xe5\xad\x98-\xf0\x9f\x90\xba \r\n";
    const auto settings = parse_save_settings(text);
    require(settings.extension == L"CoOp9" &&
            settings.coop_profile_root == L"C:\\coop-\u00e9-\u4fdd\u5b58-\U0001f43a",
            "UTF-8, BOM, CRLF or surrounding whitespace changed values");
}

void supported_modes_and_root_separators() {
    const auto separate = parse_save_settings(ini("c:/profiles/coop/"));
    require(separate.mode == DestinationMode::separate_profile && separate.coop_profile_root == L"c:/profiles/coop/",
            "accepted root text was normalized unexpectedly");
    const auto same = parse_save_settings(ini("C:\\Profiles", "1", "same_account_directory"));
    require(same.mode == DestinationMode::same_account_directory && same.extension == L"1", "same-account mode rejected");
}

void every_key_and_section_are_required() {
    for (const auto text : {"", "[SAVE]\n", "[SAVE]\ndestination_mode=separate_profile\ncoop_profile_root=\n",
                           "[SAVE]\nsave_file_extension=co2\ncoop_profile_root=\n",
                           "[SAVE]\nsave_file_extension=co2\ndestination_mode=separate_profile\n",
                           "save_file_extension=co2\ndestination_mode=separate_profile\ncoop_profile_root=\n"})
        invalid([&] { (void)parse_save_settings(text); });
}

void duplicates_and_unknowns_are_rejected() {
    for (const auto suffix : {"[SAVE]\n", "[OTHER]\n", "save_file_extension=co2\n",
                             "destination_mode=separate_profile\n", "coop_profile_root=\n",
                             "extension=co2\n", "gameplay=true\n", "SAVE_FILE_EXTENSION=co2\n"})
        invalid([&] { (void)parse_save_settings(ini() + suffix); });
    invalid([&] { (void)parse_save_settings("[save]\n" + ini()); });
}

void malformed_lines_and_quoted_values_are_rejected() {
    for (const auto suffix : {"stray text\n", "[SAVE\n", "=value\n", "[SAVE] ; inline comment\n"})
        invalid([&] { (void)parse_save_settings(ini() + suffix); });
    invalid([&] { (void)parse_save_settings(ini({}, "\"co2\"")); });
    invalid([&] { (void)parse_save_settings(ini({}, "co2 ; inline comment")); });
}

void extension_rules() {
    for (const auto extension : {"", "sl2", "SL2", "sL2", ".co2", "co2.bak", "co_op", "co 2", "co2:stream"})
        invalid([&] { (void)parse_save_settings(ini({}, extension)); });
    invalid([&] { (void)parse_save_settings(ini({}, std::string(121, 'a'))); });
    require(parse_save_settings(ini({}, std::string(120, 'a'))).extension.size() == 120, "maximum extension rejected");
}

void mode_rules() {
    for (const auto mode : {"", "SEPARATE_PROFILE", "relative", "separate_profile # comment"})
        invalid([&] { (void)parse_save_settings(ini({}, "co2", mode)); });
}

void malformed_utf8_is_rejected_even_in_comments() {
    for (const auto& bytes : {std::string("\xc0\x80", 2), std::string("\x80", 1),
                            std::string("\xf0\x9f", 2), std::string("\xed\xa0\x80", 3),
                            std::string("\xf4\x90\x80\x80", 4)})
        invalid([&] { (void)parse_save_settings(ini() + ";" + bytes); });
}

void nul_bytes_are_rejected() {
    auto bytes = ini();
    bytes += ';';
    bytes.push_back('\0');
    invalid([&] { (void)parse_save_settings(bytes); });
    invalid([&] { (void)parse_save_settings(std::string("\xff\xfe", 2) + ini()); });
}

void unsafe_roots_are_rejected() {
    for (const auto root : {"relative", "C:relative", "C:\\", "/profiles", "%APPDATA%", "C:\\%TEMP%\\coop",
                           "\\\\server\\share", "\\\\?\\C:\\coop", "\\\\.\\C:\\coop", "\\??\\C:\\coop",
                           "C:\\coop\\..\\outside", "C:\\coop\\.\\nested", "C:\\coop\\\\nested",
                           "C:\\coop\\\\", "C:\\coop:stream", "C:\\coop.\\nested", "C:\\co op \\nested",
                           "C:\\CON\\coop", "C:\\COM1.txt\\coop", "C:\\NUL\\coop", "C:\\co?op"})
        invalid([&] { (void)parse_save_settings(ini(root)); });
    invalid([&] { (void)parse_save_settings(ini(std::string("C:\\a") + char{1} + "b")); });
}

void component_and_root_bounds() {
    invalid([&] { (void)parse_save_settings(ini("C:\\" + std::string(256, 'x'))); });
    require(parse_save_settings(ini("C:\\" + std::string(255, 'x'))).coop_profile_root.size() == 258,
            "maximum component rejected");
    std::string root = "C:\\base";
    while (root.size() <= 32767) root += "\\" + std::string(255, 'x');
    invalid([&] { (void)parse_save_settings(ini(root)); });
}

void parser_byte_limit() {
    const auto bytes = maximum_ini();
    require(bytes.size() == max_save_settings_bytes && parse_save_settings(bytes).extension == L"co2",
            "exactly 64 KiB rejected");
    invalid([&] { (void)parse_save_settings(bytes + "x"); });
}

void reader_unicode_path_does_not_modify_configuration() {
    Fixture fixture;
    const auto bytes = std::string("\xef\xbb\xbf") + ini("C:\\coop");
    fixture.write(bytes);
    const auto settings = read_save_settings(fixture.path());
    require(settings.extension == L"co2" && settings.coop_profile_root == L"C:\\coop", "reader changed config values");
    std::ifstream stream(fixture.path(), std::ios::binary);
    const std::string after{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    require(after == bytes, "reader modified the configuration");
}

void reader_bounds_encoding_and_missing_file() {
    Fixture fixture;
    bool missing{};
    try { (void)read_save_settings(fixture.path()); } catch (const std::runtime_error&) { missing = true; }
    require(missing && !fs::exists(fixture.path()), "missing config was created or accepted");
    fixture.write(maximum_ini());
    require(read_save_settings(fixture.path()).extension == L"co2", "bounded reader rejected exact limit");
    fixture.write(maximum_ini() + "x");
    invalid([&] { (void)read_save_settings(fixture.path()); });
    fixture.write(ini() + std::string("\xff", 1));
    invalid([&] { (void)read_save_settings(fixture.path()); });
}
} // namespace

int main() {
    const std::array<std::pair<std::string_view, std::function<void()>>, 15> cases{{
        {"empty_root_is_explicitly_incomplete", empty_root_is_explicitly_incomplete},
        {"bom_crlf_comments_whitespace_unicode", bom_crlf_comments_whitespace_unicode},
        {"supported_modes_and_root_separators", supported_modes_and_root_separators},
        {"every_key_and_section_are_required", every_key_and_section_are_required},
        {"duplicates_and_unknowns_are_rejected", duplicates_and_unknowns_are_rejected},
        {"malformed_lines_and_quoted_values_are_rejected", malformed_lines_and_quoted_values_are_rejected},
        {"extension_rules", extension_rules}, {"mode_rules", mode_rules},
        {"malformed_utf8_is_rejected_even_in_comments", malformed_utf8_is_rejected_even_in_comments},
        {"nul_bytes_are_rejected", nul_bytes_are_rejected},
        {"unsafe_roots_are_rejected", unsafe_roots_are_rejected},
        {"component_and_root_bounds", component_and_root_bounds},
        {"parser_byte_limit", parser_byte_limit},
        {"reader_unicode_path_does_not_modify_configuration", reader_unicode_path_does_not_modify_configuration},
        {"reader_bounds_encoding_and_missing_file", reader_bounds_encoding_and_missing_file}
    }};
    std::size_t failures{};
    for (const auto& [name, test] : cases) {
        try { test(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) { ++failures; std::cerr << "FAIL " << name << ": " << error.what() << '\n'; }
    }
    std::cout << "SAVE_SETTINGS_RESULT " << (failures == 0 ? "PASS" : "FAIL")
              << " cases=" << cases.size() << " failed=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}
