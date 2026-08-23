#include "HeaderSource.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string_view>
#include <utility>
#include <vector>

namespace ned::editor::headersource {

namespace {

constexpr std::array<std::string_view, 5> kHeaderExtensions = {".h", ".hh", ".hpp", ".hxx", ".h++"};
constexpr std::array<std::string_view, 7> kSourceExtensions = {".c", ".cc", ".cpp", ".cxx", ".c++", ".m", ".mm"};

std::string LowerAscii(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

template <std::size_t N>
bool Contains(const std::array<std::string_view, N>& list, std::string_view value) {
    return std::find(list.begin(), list.end(), value) != list.end();
}

// Sibling-directory name swaps common to real C/C++ project layouts, checked
// both directions -- applied to path's parent directory only (see this
// file's header comment for why this stays a small, fixed set rather than a
// real search).
constexpr std::array<std::pair<std::string_view, std::string_view>, 4> kSiblingDirSwaps = {{
    {"src", "include"},
    {"source", "include"},
    {"Source", "Include"},
    {"src", "inc"},
}};

std::vector<std::filesystem::path> CandidateDirectories(const std::filesystem::path& file) {
    const std::filesystem::path dir = file.parent_path();

    std::vector<std::filesystem::path> dirs{dir};
    const std::string                  dirName = dir.filename().string();
    for (const auto& [sourceLike, headerLike] : kSiblingDirSwaps) {
        if (dirName == sourceLike) {
            dirs.push_back(dir.parent_path() / std::string(headerLike));
        }
        else if (dirName == headerLike) {
            dirs.push_back(dir.parent_path() / std::string(sourceLike));
        }
    }
    return dirs;
}

} // namespace

bool IsHeaderExtension(const std::string& extension) {
    return Contains(kHeaderExtensions, LowerAscii(extension));
}

bool IsSourceExtension(const std::string& extension) {
    return Contains(kSourceExtensions, LowerAscii(extension));
}

std::optional<std::filesystem::path> FindCounterpart(const std::filesystem::path& path) {
    const std::string extension = path.extension().string();

    const std::array<std::string_view, 5>* headerCandidates = nullptr;
    const std::array<std::string_view, 7>* sourceCandidates = nullptr;
    if (IsHeaderExtension(extension)) {
        sourceCandidates = &kSourceExtensions;
    }
    else if (IsSourceExtension(extension)) {
        headerCandidates = &kHeaderExtensions;
    }
    else {
        return std::nullopt;
    }

    const std::string stem = path.stem().string();
    for (const std::filesystem::path& dir : CandidateDirectories(path)) {
        if (sourceCandidates != nullptr) {
            for (const std::string_view ext : *sourceCandidates) {
                if (std::filesystem::path candidate = dir / (stem + std::string(ext)); std::filesystem::exists(candidate)) {
                    return candidate;
                }
            }
        }
        else {
            for (const std::string_view ext : *headerCandidates) {
                if (std::filesystem::path candidate = dir / (stem + std::string(ext)); std::filesystem::exists(candidate)) {
                    return candidate;
                }
            }
        }
    }
    return std::nullopt;
}

} // namespace ned::editor::headersource
