#include "launch_settings.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
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
using namespace skcoop::launcher;
namespace fs = std::filesystem;
void require(bool result, const char* message) { if (!result) throw std::runtime_error(message); }
template<class Callable> void invalid(Callable action) {
    bool rejected{};
    try { action(); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "Invalid launch settings were accepted.");
}
std::string ini(std::string_view executable = "C:\\Steam\\common\\Test\\sekiro.exe",
                std::string_view coop = "C:\\profiles\\coop", std::string_view account = "12345678901234567",
                std::string_view receipts = "C:\\receipts") {
    return "[LAUNCH]\ntest_executable=" + std::string(executable) + "\ncoop_profile_root=" + std::string(coop) +
        "\naccount=" + std::string(account) + "\nreceipt_directory=" + std::string(receipts) + "\n";
}
std::string maximum_ini() {
    auto text = ini();
    text += ';';
    text.append(max_launch_settings_bytes - text.size(), 'x');
    return text;
}
std::string path_with_units(std::size_t units) {
    std::string result = "C:\\";
    while (units - result.size() > 255) result += std::string(254, 'a') + '\\';
    result.append(units - result.size(), 'b');
    return result;
}
void valid_values_are_preserved() {
    const auto settings = parse_launch_settings(ini());
    require(settings.test_executable == L"C:\\Steam\\common\\Test\\sekiro.exe" &&
        settings.coop_profile_root == L"C:\\profiles\\coop" && settings.account == L"12345678901234567" &&
        settings.receipt_directory == L"C:\\receipts", "Config values changed.");
    const auto mixed = parse_launch_settings(ini("c:/Test Folder/sekiro.exe", "D:/profiles/coop", "1", "D:\\Logs"));
    require(mixed.test_executable.wstring() == L"c:/Test Folder/sekiro.exe" &&
        mixed.coop_profile_root.wstring() == L"D:/profiles/coop" && mixed.receipt_directory.wstring() == L"D:\\Logs",
        "Separators or spaces changed.");
}
void unicode_bom_crlf_comments_and_whitespace() {
    const std::string bytes = "\xef\xbb\xbf; comment\r\n # another comment\r\n [LAUNCH] \r\n"
        " receipt_directory = C:\\logs \r\naccount = 18446744073709551615\r\n"
        "test_executable = C:\\Steam Games\\\xe6\x97\xa5\xe6\x9c\xac\\sekiro.exe\r\n"
        "coop_profile_root = C:\\coop-\xc3\xa9-\xf0\x9f\x90\xba\r\n";
    const auto settings = parse_launch_settings(bytes);
    require(settings.test_executable == L"C:\\Steam Games\\\u65e5\u672c\\sekiro.exe" &&
        settings.coop_profile_root == L"C:\\coop-\u00e9-\U0001f43a" &&
        settings.account == L"18446744073709551615", "Unicode/format handling changed values.");
}
void every_key_is_required_and_nonempty() {
    for (const auto text : {"", "[LAUNCH]\n", "test_executable=C:\\a\\sekiro.exe\n", "[launch]\n"})
        invalid([&] { (void)parse_launch_settings(text); });
    const auto complete = ini();
    for (const auto key : {"test_executable=", "coop_profile_root=", "account=", "receipt_directory="}) {
        auto text = complete;
        const auto start = text.find(key), end = text.find('\n', start);
        text.erase(start, end - start + 1);
        invalid([&] { (void)parse_launch_settings(text); });
        text = complete;
        const auto value = text.find(key) + std::string_view(key).size();
        text.erase(value, text.find('\n', value) - value);
        invalid([&] { (void)parse_launch_settings(text); });
    }
}
void duplicates_unknowns_and_sections_are_rejected() {
    for (const auto suffix : {"test_executable=C:\\other.exe\n", "coop_profile_root=C:\\other\n", "account=1\n",
        "receipt_directory=C:\\other\n", "[LAUNCH]\n", "[OTHER]\n", "runtime=C:\\mod.dll\n", "ACCOUNT=1\n"})
        invalid([&] { (void)parse_launch_settings(ini() + suffix); });
    for (const auto suffix : {"stray text\n", "=value\n", "[LAUNCH\n", "[LAUNCH] # inline comment\n"})
        invalid([&] { (void)parse_launch_settings(ini() + suffix); });
}
void accounts_are_canonical_uint64() {
    require(parse_launch_settings(ini("C:\\sekiro.exe", "C:\\coop", "18446744073709551615")).account ==
        L"18446744073709551615", "Maximum account rejected.");
    for (const auto account : {"0", "01", "0001", "-1", "+1", "1.0", "1e2", "1 2", "18446744073709551616",
        "999999999999999999999", "\xd9\xa1", "\xef\xbc\x91", "\"1\"", "1 # inline"})
        invalid([&] { (void)parse_launch_settings(ini("C:\\sekiro.exe", "C:\\coop", account)); });
}
void unsafe_paths_are_rejected_for_every_path_key() {
    for (const auto path : {"relative", "C:relative", "C:\\", "/tmp/game", "%APPDATA%", "C:\\%TEMP%\\coop",
        "\\\\server\\share", "\\\\?\\C:\\coop", "\\\\.\\C:\\coop", "\\??\\C:\\coop", "C:\\a\\..\\b",
        "C:\\a\\.\\b", "C:\\a\\\\b", "C:\\a\\\\", "C:\\a:stream", "C:\\a.\\b", "C:\\a \\b",
        "C:\\a?b", "C:\\a*b", "C:\\a<b", "C:\\a>b", "C:\\a|b", "\"C:\\coop\"", "C:\\a\"b", "C:\\coop\\", "C:/coop/"}) {
        invalid([&] { (void)parse_launch_settings(ini(path)); });
        invalid([&] { (void)parse_launch_settings(ini("C:\\sekiro.exe", path)); });
        invalid([&] { (void)parse_launch_settings(ini("C:\\sekiro.exe", "C:\\coop", "1", path)); });
    }
    invalid([&] { (void)parse_launch_settings(ini("C:\\game\\")); });
    for (const char control : {char{1}, char{7}, char{9}, char{13}, char{31}, char{127}}) {
        const auto path = std::string("C:\\co") + control + "op";
        invalid([&] { (void)parse_launch_settings(ini("C:\\sekiro.exe", path)); });
    }
}
void devices_are_rejected_case_insensitively() {
    for (const auto device : {"con", "PRN", "AuX.dat", "NUL.txt", "CONIN$", "CONOUT$", "Com1.ext", "LPT9",
        "COM\xc2\xb9", "LPT\xc2\xb2.txt", "com\xc2\xb3"}) {
        const auto path = "C:\\" + std::string(device) + "\\coop";
        invalid([&] { (void)parse_launch_settings(ini("C:\\sekiro.exe", path)); });
    }
    require(parse_launch_settings(ini("C:\\sekiro.exe", "C:\\COM0\\LPT10\\CONSOLE")).account == L"12345678901234567",
        "Ordinary components incorrectly treated as devices.");
}
void path_and_component_limits_use_utf16_units() {
    require(parse_launch_settings(ini("C:\\sekiro.exe", path_with_units(1023))).coop_profile_root.wstring().size() == 1023,
        "1023-unit root rejected.");
    invalid([&] { (void)parse_launch_settings(ini("C:\\sekiro.exe", path_with_units(1024))); });
    require(parse_launch_settings(ini("C:\\sekiro.exe", "C:\\" + std::string(255, 'a'))).coop_profile_root.wstring().size() == 258,
        "255-unit component rejected.");
    invalid([&] { (void)parse_launch_settings(ini("C:\\sekiro.exe", "C:\\" + std::string(256, 'a'))); });
    const std::string wolf = "\xf0\x9f\x90\xba";
    std::string component;
    for (int i = 0; i < 127; ++i) component += wolf;
    component += 'a';
    require(parse_launch_settings(ini("C:\\sekiro.exe", "C:\\" + component)).coop_profile_root.wstring().size() == 258,
        "UTF-16 surrogate pairs counted as UTF-8 bytes.");
    invalid([&] { (void)parse_launch_settings(ini("C:\\sekiro.exe", "C:\\" + component + 'a')); });
}
void encoding_is_validated_even_in_comments() {
    for (const auto& bytes : {std::string("\xc0\x80", 2), std::string("\x80", 1), std::string("\xf0\x9f", 2),
        std::string("\xed\xa0\x80", 3), std::string("\xf4\x90\x80\x80", 4), std::string("\xff\xfe", 2)})
        invalid([&] { (void)parse_launch_settings(ini() + ';' + bytes); });
    auto embedded = ini() + ';';
    embedded.push_back('\0');
    invalid([&] { (void)parse_launch_settings(embedded); });
}
void byte_limit_is_exact() {
    const auto text = maximum_ini();
    require(text.size() == max_launch_settings_bytes && parse_launch_settings(text).account == L"12345678901234567",
        "Exact 64 KiB boundary rejected.");
    invalid([&] { (void)parse_launch_settings(text + 'x'); });
}
class Fixture {
public:
    Fixture() {
        const auto parent = fs::temp_directory_path();
        for (unsigned attempt = 0; attempt < 100; ++attempt) {
            const auto candidate = parent / (L"skcoop-launch-settings-\u65e5\u672c-" + std::to_wstring(GetCurrentProcessId()) +
                L"-" + std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(attempt));
            if (fs::create_directory(candidate)) { directory_ = candidate; break; }
        }
        require(!directory_.empty(), "Cannot create owned config fixture.");
    }
    ~Fixture() {
        std::error_code ignored;
        fs::remove(path(), ignored);
        fs::remove(directory_, ignored);
    }
    fs::path path() const { return directory_ / L"launch-\u4fdd\u5b58.ini"; }
    void write(const std::string& bytes) const {
        std::ofstream output(path(), std::ios::binary | std::ios::trunc);
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        require(static_cast<bool>(output), "Cannot write owned fixture config.");
    }
private:
    fs::path directory_;
};
void reader_is_bounded_read_only_and_handles_unicode_path() {
    Fixture fixture;
    bool missing{};
    try { (void)read_launch_settings(fixture.path()); } catch (const std::runtime_error&) { missing = true; }
    require(missing && !fs::exists(fixture.path()), "Missing configuration was created or accepted.");
    const auto content = std::string("\xef\xbb\xbf") + ini();
    fixture.write(content);
    require(read_launch_settings(fixture.path()).account == L"12345678901234567", "Unicode reader failed.");
    std::ifstream input(fixture.path(), std::ios::binary);
    const std::string actual{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    require(actual == content, "Reader modified its configuration.");
    input.close();
    fixture.write(maximum_ini());
    require(read_launch_settings(fixture.path()).account == L"12345678901234567", "Reader rejected exact byte limit.");
    fixture.write(maximum_ini() + 'x');
    invalid([&] { (void)read_launch_settings(fixture.path()); });
    fixture.write(ini() + std::string("\xff", 1));
    invalid([&] { (void)read_launch_settings(fixture.path()); });
}
} // namespace

int main() {
    require(!parse_launch_settings(ini()).actor_follow_test, "Actor test must default to off.");
    require(parse_launch_settings(ini()+"actor_test=follow\n").actor_follow_test, "Explicit actor test missing.");
    require(parse_launch_settings(ini()+"actor_test=locomotion\n").actor_locomotion_test, "Explicit locomotion test missing.");
    const auto pad=parse_launch_settings(ini()+"actor_test=pad-reference\n");
    require(pad.actor_pad_reference_test && pad.actor_follow_test && !pad.actor_locomotion_test, "Native pad reference must be explicit and separate.");
    const auto graph=parse_launch_settings(ini()+"actor_test=graph-locomotion\n");
    require(graph.actor_graph_locomotion_test && graph.actor_follow_test && !graph.actor_locomotion_test && !graph.actor_pad_reference_test, "Native graph test must be explicit and separate.");
    require(!parse_launch_settings(ini()+"actor_test=off\n").actor_follow_test, "Explicit off changed.");
    invalid([&] { (void)parse_launch_settings(ini()+"actor_test=true\n"); });
    invalid([&] { (void)parse_launch_settings(ini()+"actor_test=follow\nactor_test=off\n"); });
    const std::array<std::pair<std::string_view, std::function<void()>>, 11> tests{{
        {"valid_values_are_preserved", valid_values_are_preserved},
        {"unicode_bom_crlf_comments_and_whitespace", unicode_bom_crlf_comments_and_whitespace},
        {"every_key_is_required_and_nonempty", every_key_is_required_and_nonempty},
        {"duplicates_unknowns_and_sections_are_rejected", duplicates_unknowns_and_sections_are_rejected},
        {"accounts_are_canonical_uint64", accounts_are_canonical_uint64},
        {"unsafe_paths_are_rejected_for_every_path_key", unsafe_paths_are_rejected_for_every_path_key},
        {"devices_are_rejected_case_insensitively", devices_are_rejected_case_insensitively},
        {"path_and_component_limits_use_utf16_units", path_and_component_limits_use_utf16_units},
        {"encoding_is_validated_even_in_comments", encoding_is_validated_even_in_comments},
        {"byte_limit_is_exact", byte_limit_is_exact},
        {"reader_is_bounded_read_only_and_handles_unicode_path", reader_is_bounded_read_only_and_handles_unicode_path}
    }};
    std::size_t failed{};
    for (const auto& [name, test] : tests) {
        try { test(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) { ++failed; std::cerr << "FAIL " << name << ": " << error.what() << '\n'; }
    }
    std::cout << "LAUNCH_SETTINGS_RESULT " << (failed ? "FAIL" : "PASS") << " cases=" << tests.size() << " failed=" << failed << '\n';
    return failed ? 1 : 0;
}
