#include "steam_install.h"
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
namespace fs = std::filesystem;
using skcoop::launcher::inspect_steam_install;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template <class Action> void rejected(Action action) {
    bool failed = false;
    try { action(); } catch (const std::exception&) { failed = true; }
    require(failed, "Invalid or ambiguous Steam metadata was accepted.");
}
void write(const fs::path& path, std::string_view text) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    require(static_cast<bool>(out), "Cannot write owned fixture.");
}
std::string quote(std::string_view text) {
    std::string result = "\"";
    for (const auto c : text) {
        if (c == '\\' || c == '"') result += '\\';
        result += c;
    }
    return result + '"';
}
std::string quote(const fs::path& path) {
    const auto utf8 = path.u8string();
    return quote(std::string_view(reinterpret_cast<const char*>(utf8.data()), utf8.size()));
}
std::string manifest(std::string_view directory = "Sekiro", std::string_view appid = "814380",
                     std::string_view state = "4", std::string_view build = "5794815") {
    return "\"AppState\" { \"appid\" " + quote(appid) + " \"StateFlags\" " + quote(state) +
        " \"installdir\" " + quote(directory) + " \"buildid\" " + quote(build) + " }\n";
}

class Fixture {
public:
    Fixture() : parent_(fs::temp_directory_path().lexically_normal()) {
        const auto prefix = L"skcoop-steam-\u4fdd\u5b58-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
            std::to_wstring(GetTickCount64()) + L"-";
        for (int attempt = 0; attempt < 100; ++attempt) {
            const auto candidate = parent_ / (prefix + std::to_wstring(attempt));
            std::error_code error;
            if (fs::create_directory(candidate, error)) { root_ = candidate; break; }
            if (error) throw std::runtime_error("Cannot create Steam discovery fixture.");
        }
        require(!root_.empty(), "Steam fixture names exhausted.");
        fs::create_directories(steam() / L"steamapps");
        write(steam() / L"steam.exe", "owned test marker, never executed");
    }
    ~Fixture() {
        // Resolve and check the exact owned directory before recursive cleanup.
        if (!root_.empty() && root_.is_absolute() && root_.parent_path() == parent_ &&
            root_.filename().wstring().starts_with(L"skcoop-steam-")) {
            std::error_code ignored;
            fs::remove_all(root_, ignored);
        }
    }
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;
    fs::path steam() const { return root_ / L"Steam"; }
    fs::path other() const { return root_ / L"Library-\u65e5\u672c"; }
    fs::path manifest_path(const fs::path& library = {}) const {
        return (library.empty() ? steam() : library) / L"steamapps" / L"appmanifest_814380.acf";
    }
    fs::path exe(const fs::path& library = {}) const {
        return (library.empty() ? steam() : library) / L"steamapps" / L"common" / L"Sekiro" / L"sekiro.exe";
    }
    fs::path folders() const { return steam() / L"steamapps" / L"libraryfolders.vdf"; }
    void install(const fs::path& library = {}) const {
        write(manifest_path(library), manifest());
        write(exe(library), "owned fixture executable marker, never executed");
    }
private:
    fs::path parent_, root_;
};

void primary_library_manifest_selects_exact_directory() {
    Fixture fixture;
    fixture.install();
    write(fixture.steam() / L"steamapps" / L"common" / L"Sekiro-SkCoop-Test" / L"sekiro.exe", "decoy");
    const auto found = inspect_steam_install(fixture.steam());
    require(found.steam_root == fixture.steam() && found.library_root == fixture.steam() &&
            found.manifest_path == fixture.manifest_path() && found.executable == fixture.exe() &&
            found.build_id == "5794815", "Discovery changed the manifest-selected installation.");
}
void modern_external_library_nested_unicode_and_escapes() {
    Fixture fixture;
    fixture.install(fixture.other());
    write(fixture.folders(), std::string("\xef\xbb\xbf") + "// root comment\r\n\"libraryfolders\" {\n"
        "\"0\" { \"path\" " + quote(fixture.steam()) + " \"apps\" {} }\n"
        "\"1\" { \"path\" " + quote(fixture.other()) + " \"label\" \"A \\\"label\\\"\" "
        "\"apps\" { \"814380\" \"14960771294\" } } } // done\n");
    const auto found = inspect_steam_install(fixture.steam());
    require(found.library_root == fixture.other() && found.executable == fixture.exe(fixture.other()),
            "Modern nested Unicode library path was not decoded exactly.");
}
void legacy_external_library_and_unquoted_values() {
    Fixture fixture;
    fixture.install(fixture.other());
    write(fixture.folders(), "LibraryFolders { TimeNextStatsReport 1 ContentStatsID 2 1 " + quote(fixture.other()) + " }");
    write(fixture.manifest_path(fixture.other()), "AppState { appid 814380 StateFlags 4 installdir Sekiro buildid 5794815 }");
    require(inspect_steam_install(fixture.steam()).executable == fixture.exe(fixture.other()),
            "Legacy library format was not discovered.");
}
void duplicate_library_paths_are_one_candidate() {
    Fixture fixture;
    fixture.install();
    write(fixture.folders(), "libraryfolders { 0 { path " + quote(fixture.steam()) + " } 1 " + quote(fixture.steam()) + " }");
    require(inspect_steam_install(fixture.steam()).executable == fixture.exe(), "Repeated library path was ambiguous.");
}
void missing_manifest_never_guesses_a_sibling() {
    Fixture fixture;
    write(fixture.steam() / L"steamapps" / L"common" / L"Sekiro-SkCoop-Test" / L"sekiro.exe", "decoy");
    rejected([&] { (void)inspect_steam_install(fixture.steam()); });
}
void multiple_installed_manifests_are_rejected() {
    Fixture fixture;
    fixture.install();
    fixture.install(fixture.other());
    write(fixture.folders(), "libraryfolders { 1 { path " + quote(fixture.other()) + " } }");
    rejected([&] { (void)inspect_steam_install(fixture.steam()); });
}
void disconnected_libraries_are_ignored() {
    Fixture fixture;
    fixture.install();
    write(fixture.folders(), "libraryfolders { 1 { path " + quote(fixture.other()) + " } }");
    require(inspect_steam_install(fixture.steam()).executable == fixture.exe(), "Disconnected library hid the installed game.");
}
void malformed_vdf_never_falls_back() {
    Fixture fixture;
    fixture.install();
    const std::vector<std::string> invalid{
        "libraryfolders { 0 { path \"unterminated } }",
        "libraryfolders { 0 { path \"C:\\invalid\\escape\" } }",
        "libraryfolders { 0 { path \"one\" path \"two\" } }",
        "libraryfolders { 0 { PATH \"one\" path \"two\" } }",
        "libraryfolders { 0 { path \"relative\" } }",
        "libraryfolders { 0 { path \"one\" }",
        "libraryfolders {} }",
        "libraryfolders {} unexpected {}",
        "libraryfolders { 0 { path \"a\"junk } }",
        std::string("libraryfolders {}") + '\0' + "ignored"
    };
    for (const auto& text : invalid) {
        write(fixture.folders(), text);
        rejected([&] { (void)inspect_steam_install(fixture.steam()); });
    }
}
void manifest_identity_state_and_build_are_required() {
    Fixture fixture;
    fixture.install();
    const std::vector<std::string> invalid{
        manifest("Sekiro", "123"), manifest("Sekiro", "814380", "1026"),
        manifest("Sekiro", "814380", "4", "bad"),
        "AppState { appid 814380 StateFlags 4 installdir Sekiro }",
        "AppState { appid 814380 APPID 814380 StateFlags 4 installdir Sekiro buildid 1 }",
        "AppState { appid 814380 StateFlags 4 installdir Sekiro buildid {} }",
        "AppState { appid 814380 StateFlags 4 installdir Sekiro buildid 1 } Extra {}"
    };
    for (const auto& text : invalid) {
        write(fixture.manifest_path(), text);
        rejected([&] { (void)inspect_steam_install(fixture.steam()); });
    }
}
void manifest_cannot_escape_common_directory() {
    Fixture fixture;
    fixture.install();
    for (const auto* directory : {"..", "../Sekiro", "..\\Sekiro", "C:\\Sekiro", ".", "Sekiro.", "Sekiro "}) {
        write(fixture.manifest_path(), manifest(directory));
        rejected([&] { (void)inspect_steam_install(fixture.steam()); });
    }
}
void missing_steam_or_game_executable_is_rejected() {
    Fixture fixture;
    fixture.install();
    fs::remove(fixture.exe());
    rejected([&] { (void)inspect_steam_install(fixture.steam()); });
    fixture.install();
    fs::remove(fixture.steam() / L"steam.exe");
    rejected([&] { (void)inspect_steam_install(fixture.steam()); });
    rejected([&] { (void)inspect_steam_install(L"relative-steam"); });
}
void parser_budgets_fail_closed() {
    Fixture fixture;
    fixture.install();
    write(fixture.folders(), std::string(1024 * 1024 + 1, ' '));
    rejected([&] { (void)inspect_steam_install(fixture.steam()); });
    write(fixture.folders(), "libraryfolders { key \"" + std::string(32769, 'x') + "\" }");
    rejected([&] { (void)inspect_steam_install(fixture.steam()); });
    std::string deep = "libraryfolders {";
    for (int i = 0; i < 33; ++i) deep += " x {";
    deep += std::string(34, '}');
    write(fixture.folders(), deep);
    rejected([&] { (void)inspect_steam_install(fixture.steam()); });
    std::string many = "libraryfolders {";
    for (int i = 0; i < 8193; ++i) many += " k" + std::to_string(i) + " x";
    many += '}';
    write(fixture.folders(), many);
    rejected([&] { (void)inspect_steam_install(fixture.steam()); });
}
}

int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> tests{
        {"manifest-selects-original", primary_library_manifest_selects_exact_directory},
        {"modern-library", modern_external_library_nested_unicode_and_escapes},
        {"legacy-library", legacy_external_library_and_unquoted_values},
        {"deduplicate-libraries", duplicate_library_paths_are_one_candidate},
        {"no-directory-guess", missing_manifest_never_guesses_a_sibling},
        {"ambiguous-install", multiple_installed_manifests_are_rejected},
        {"disconnected-library", disconnected_libraries_are_ignored},
        {"malformed-vdf", malformed_vdf_never_falls_back},
        {"manifest-fields", manifest_identity_state_and_build_are_required},
        {"manifest-path-traversal", manifest_cannot_escape_common_directory},
        {"missing-executable", missing_steam_or_game_executable_is_rejected},
        {"metadata-budgets", parser_budgets_fail_closed}
    };
    unsigned failures = 0;
    for (const auto& [name, action] : tests) {
        try { action(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) { ++failures; std::cerr << "FAIL " << name << ": " << error.what() << '\n'; }
    }
    std::cout << tests.size() - failures << '/' << tests.size() << " Steam discovery cases passed.\n";
    return failures ? 1 : 0;
}
