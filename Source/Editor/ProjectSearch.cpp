#include "ProjectSearch.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <regex>

namespace ned::editor {

namespace {

    bool IsDotDirectory(const std::filesystem::directory_entry& entry) {
        const std::string name = entry.path().filename().string();
        return !name.empty() && name.front() == '.';
    }

    bool LooksBinary(const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return true; // unreadable -- not worth searching
        }

        std::array<char, 8192> buffer{};
        file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto bytesRead = static_cast<std::size_t>(file.gcount());
        return std::find(buffer.data(), buffer.data() + bytesRead, '\0') != buffer.data() + bytesRead;
    }

} // namespace

std::vector<SearchMatch> SearchDirectory(const std::filesystem::path& root, const std::string& pattern) {
    const std::regex regex(pattern); // throws std::regex_error on invalid syntax

    std::vector<SearchMatch> matches;

    std::error_code             ec;
    const std::filesystem::path absoluteRoot = std::filesystem::absolute(root, ec);
    if (ec) {
        return matches;
    }

    auto it = std::filesystem::recursive_directory_iterator(
        absoluteRoot, std::filesystem::directory_options::skip_permission_denied, ec);
    const auto end = std::filesystem::recursive_directory_iterator();
    if (ec) {
        return matches;
    }

    for (; it != end; it.increment(ec)) {
        if (ec) {
            break;
        }

        const std::filesystem::directory_entry& entry = *it;

        if (entry.is_directory()) {
            if (IsDotDirectory(entry)) {
                it.disable_recursion_pending();
            }
            continue;
        }
        if (!entry.is_regular_file() || LooksBinary(entry.path())) {
            continue;
        }

        std::ifstream file(entry.path());
        if (!file) {
            continue;
        }

        std::string line;
        std::size_t lineNumber = 0;
        while (std::getline(file, line)) {
            ++lineNumber;
            if (std::regex_search(line, regex)) {
                matches.push_back(SearchMatch{entry.path(), lineNumber, line});
            }
        }
    }

    return matches;
}

} // namespace ned::editor
