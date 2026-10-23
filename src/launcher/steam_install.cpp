#include "steam_install.h"

#include <Windows.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <fstream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace skcoop::launcher {
namespace {
namespace fs = std::filesystem;
constexpr std::size_t max_metadata_bytes = 1024 * 1024;
constexpr std::size_t max_token_bytes = 32768;
constexpr std::size_t max_nodes = 8192;
constexpr std::size_t max_depth = 32;

std::string lower_ascii(std::string value) {
    for (auto& c : value) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return value;
}

struct Node {
    bool object = false;
    std::string value;
    std::map<std::string, Node> children;
};

// Valve KeyValues: quoted/unquoted strings, braces, // comments and escaped
// quotes/backslashes. Unknown escapes are rejected rather than changing paths.
class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {
        if (text_.size() > max_metadata_bytes || text_.find('\0') != text_.npos)
            throw std::runtime_error("Steam metadata exceeds its limit or contains NUL.");
        if (text_.starts_with("\xef\xbb\xbf")) at_ = 3;
    }
    Node parse() {
        Node root;
        root.object = true;
        entries(root, 0, false);
        return root;
    }
private:
    void space() {
        for (;;) {
            while (at_ < text_.size() && (text_[at_] == ' ' || text_[at_] == '\t' ||
                   text_[at_] == '\r' || text_[at_] == '\n')) ++at_;
            if (at_ + 1 < text_.size() && text_[at_] == '/' && text_[at_ + 1] == '/') {
                at_ = text_.find('\n', at_ + 2);
                if (at_ == text_.npos) at_ = text_.size();
                continue;
            }
            break;
        }
    }
    std::string token() {
        space();
        if (at_ == text_.size() || text_[at_] == '{' || text_[at_] == '}')
            throw std::runtime_error("Steam metadata has a missing string.");
        std::string value;
        const bool quoted = text_[at_] == '"';
        if (quoted) ++at_;
        bool closed = !quoted;
        while (at_ < text_.size()) {
            char c = text_[at_];
            if (quoted && c == '"') { ++at_; closed = true; break; }
            if (!quoted && (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '{' || c == '}')) break;
            ++at_;
            if (static_cast<unsigned char>(c) < 0x20)
                throw std::runtime_error("Steam metadata contains a control character.");
            if (quoted && c == '\\') {
                if (at_ == text_.size()) throw std::runtime_error("Steam metadata has an incomplete escape.");
                c = text_[at_++];
                if (c != '\\' && c != '"') throw std::runtime_error("Steam metadata has an unsupported escape.");
            }
            value += c;
            if (value.size() > max_token_bytes) throw std::runtime_error("Steam metadata string exceeds its limit.");
        }
        if (!closed || (!quoted && value.empty())) throw std::runtime_error("Steam metadata has an unterminated string.");
        if (quoted && at_ < text_.size() && text_[at_] != ' ' && text_[at_] != '\t' &&
            text_[at_] != '\r' && text_[at_] != '\n' && text_[at_] != '{' && text_[at_] != '}' &&
            !(text_[at_] == '/' && at_ + 1 < text_.size() && text_[at_ + 1] == '/'))
            throw std::runtime_error("Steam metadata strings need a delimiter.");
        return value;
    }
    void entries(Node& parent, std::size_t depth, bool closing) {
        if (depth > max_depth) throw std::runtime_error("Steam metadata nesting exceeds its limit.");
        for (;;) {
            space();
            if (at_ == text_.size()) {
                if (closing) throw std::runtime_error("Steam metadata is missing a closing brace.");
                return;
            }
            if (text_[at_] == '}') {
                if (!closing) throw std::runtime_error("Steam metadata has an unexpected closing brace.");
                ++at_;
                return;
            }
            auto key = lower_ascii(token());
            if (key.empty() || ++nodes_ > max_nodes) throw std::runtime_error("Steam metadata has too many keys or an empty key.");
            Node child;
            space();
            if (at_ < text_.size() && text_[at_] == '{') {
                ++at_;
                child.object = true;
                entries(child, depth + 1, true);
            } else child.value = token();
            if (!parent.children.emplace(std::move(key), std::move(child)).second)
                throw std::runtime_error("Steam metadata has duplicate keys.");
        }
    }
    std::string_view text_;
    std::size_t at_ = 0;
    std::size_t nodes_ = 0;
};

std::string read_metadata(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot read Steam installation metadata.");
    input.seekg(0, std::ios::end);
    const auto length = input.tellg();
    if (length < 0 || length > static_cast<std::streamoff>(max_metadata_bytes))
        throw std::runtime_error("Steam installation metadata exceeds its 1 MiB limit.");
    input.seekg(0);
    std::string text(static_cast<std::size_t>(length), '\0');
    if (!text.empty() && !input.read(text.data(), static_cast<std::streamsize>(text.size())))
        throw std::runtime_error("Cannot read complete Steam installation metadata.");
    return text;
}

const Node& object(const Node& parent, const char* key) {
    const auto found = parent.children.find(key);
    if (found == parent.children.end() || !found->second.object)
        throw std::runtime_error("Steam installation metadata has a missing object.");
    return found->second;
}
const std::string& value(const Node& parent, const char* key) {
    const auto found = parent.children.find(key);
    if (found == parent.children.end() || found->second.object)
        throw std::runtime_error("Steam installation metadata has a missing value.");
    return found->second.value;
}
bool digits(std::string_view text) {
    return !text.empty() && std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; });
}
std::wstring utf16(std::string_view text) {
    if (text.empty() || text.size() > max_token_bytes) throw std::runtime_error("Steam path is empty or too long.");
    const auto size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0) throw std::runtime_error("Steam path is not valid UTF-8.");
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), size) != size)
        throw std::runtime_error("Cannot decode Steam path.");
    return result;
}
fs::path absolute_directory(const fs::path& path) {
    if (path.empty() || !path.is_absolute()) throw std::runtime_error("Steam library path must be absolute.");
    const auto normal = path.lexically_normal();
    if (!fs::is_directory(normal)) throw std::runtime_error("Steam library directory is missing.");
    return normal;
}
bool equal_path(const fs::path& a, const fs::path& b) {
    return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_EQUAL;
}
fs::path registry_root() {
    std::array<wchar_t, 32768> buffer{};
    DWORD bytes = static_cast<DWORD>(buffer.size() * sizeof(wchar_t));
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", RRF_RT_REG_SZ,
                    nullptr, buffer.data(), &bytes) != ERROR_SUCCESS || bytes < sizeof(wchar_t) ||
        bytes > buffer.size() * sizeof(wchar_t) || buffer[(bytes / sizeof(wchar_t)) - 1] != L'\0')
        throw std::runtime_error("Cannot discover SteamPath in the current user's Steam registry settings.");
    if (std::find(buffer.begin(), buffer.begin() + bytes / sizeof(wchar_t) - 1, L'\0') !=
        buffer.begin() + bytes / sizeof(wchar_t) - 1)
        throw std::runtime_error("Steam registry path contains embedded NUL.");
    return fs::path(buffer.data());
}
std::vector<fs::path> libraries(const fs::path& root) {
    std::vector<fs::path> result{root};
    const auto file = root / L"steamapps" / L"libraryfolders.vdf";
    if (!fs::exists(file)) return result;
    const auto parsed = Parser(read_metadata(file)).parse();
    const auto& folders = object(parsed, "libraryfolders");
    if (parsed.children.size() != 1) throw std::runtime_error("Steam library metadata has unexpected root keys.");
    for (const auto& [key, child] : folders.children) {
        if (!digits(key)) continue; // Legacy root-level statistics are not paths.
        auto path = fs::path(utf16(child.object ? value(child, "path") : child.value));
        if (path.empty() || !path.is_absolute()) throw std::runtime_error("Steam library path must be absolute.");
        path = path.lexically_normal();
        // Steam can remember a disconnected drive; it cannot select a game on it.
        if (!fs::is_directory(path)) continue;
        if (std::none_of(result.begin(), result.end(), [&](const auto& existing) { return equal_path(existing, path); }))
            result.push_back(std::move(path));
    }
    return result;
}
fs::path install_leaf(std::string_view text) {
    auto wide = utf16(text);
    if (wide == L"." || wide == L".." || wide.back() == L'.' || wide.back() == L' ' ||
        wide.find_first_of(L"<>:\"/\\|?*") != wide.npos)
        throw std::runtime_error("Steam installdir must be a single directory name.");
    return fs::path(wide);
}
}

SteamInstall inspect_steam_install(const fs::path& requested_root) {
    const auto root = absolute_directory(requested_root.empty() ? registry_root() : requested_root);
    if (!fs::is_regular_file(root / L"steam.exe")) throw std::runtime_error("Steam executable is missing from the selected root.");
    std::vector<SteamInstall> candidates;
    for (const auto& library : libraries(root)) {
        const auto manifest = library / L"steamapps" / L"appmanifest_814380.acf";
        if (!fs::exists(manifest)) continue;
        const auto parsed = Parser(read_metadata(manifest)).parse();
        const auto& app = object(parsed, "appstate");
        if (parsed.children.size() != 1 || value(app, "appid") != "814380")
            throw std::runtime_error("Sekiro manifest has an unexpected app ID or root.");
        if (value(app, "stateflags") != "4") throw std::runtime_error("Steam does not report a fully installed, idle Sekiro installation.");
        const auto& build = value(app, "buildid");
        if (!digits(build) || build.size() > 20) throw std::runtime_error("Sekiro manifest has an invalid build ID.");
        const auto executable = library / L"steamapps" / L"common" / install_leaf(value(app, "installdir")) / L"sekiro.exe";
        if (!fs::is_regular_file(executable)) throw std::runtime_error("Sekiro executable is missing from its Steam manifest directory.");
        if (std::none_of(candidates.begin(), candidates.end(), [&](const auto& existing) { return equal_path(existing.executable, executable); }))
            candidates.push_back({root, library, manifest, executable, build});
    }
    if (candidates.empty()) throw std::runtime_error("Steam has no installed Sekiro appmanifest_814380.acf.");
    if (candidates.size() != 1) throw std::runtime_error("Multiple Sekiro Steam manifests are present; select an unambiguous Steam installation.");
    return candidates.front();
}

}
