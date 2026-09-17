#include "LanguageFiles.h"

#include <algorithm>
#include <fstream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

#include "DataDir.h"
#include "QueryData.h"

namespace ned::editor {

namespace {

    std::mutex                                   g_compiledMutex;
    std::unordered_map<std::string, std::string> g_compiled; // path -> query text; references stay valid across rehash

    const std::string& CompiledQuery(const std::string& path) {
        {
            const std::lock_guard<std::mutex> lock(g_compiledMutex);
            if (const auto it = g_compiled.find(path); it != g_compiled.end()) {
                return it->second;
            }
        }
        const std::string source = ReadLanguageFile(path);
        std::string       text;
        try {
            const bool scm = path.ends_with(".scm");
            text           = querydata::ToQueryText(scm ? querydata::ParseScm(source) : querydata::ParseJanet(source));
        }
        catch (const querydata::QueryDataError& error) {
            throw std::runtime_error(path + ":" + error.what());
        }
        const std::lock_guard<std::mutex> lock(g_compiledMutex);
        return g_compiled.emplace(path, std::move(text)).first->second;
    }

    bool ReadWholeFile(const std::filesystem::path& path, std::string& out) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            return false;
        }
        std::ostringstream buffer;
        buffer << in.rdbuf();
        out = buffer.str();
        return true;
    }

    std::filesystem::path Resolve(std::string_view path) {
        const std::filesystem::path fsPath(path);
        return fsPath.is_absolute() ? fsPath : BundledLanguagesRoot() / fsPath;
    }

} // namespace

const std::filesystem::path& BundledLanguagesRoot() {
    static const std::filesystem::path kRoot = DataDir() / "languages";
    return kRoot;
}

std::vector<BundledLanguageFile> BundledLanguageFiles() {
    std::vector<BundledLanguageFile> out;
    const std::filesystem::path&     root = BundledLanguagesRoot();
    std::error_code                  ec;
    for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(root, ec)) {
        std::error_code entryEc;
        if (!entry.is_regular_file(entryEc) || entryEc || entry.path().extension() != ".janet") {
            continue;
        }
        BundledLanguageFile file{.path = entry.path().lexically_relative(root).generic_string()};
        if (!ReadWholeFile(entry.path(), file.content)) {
            throw std::runtime_error("cannot read bundled language file: " + entry.path().string());
        }
        out.push_back(std::move(file));
    }
    std::sort(out.begin(), out.end(),
              [](const BundledLanguageFile& a, const BundledLanguageFile& b) { return a.path < b.path; });
    return out;
}

bool BundledLanguageFileExists(std::string_view path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(BundledLanguagesRoot() / std::filesystem::path(path), ec);
}

std::string ReadLanguageFile(std::string_view path) {
    std::string content;
    if (ReadWholeFile(Resolve(path), content)) {
        return content;
    }
    throw std::runtime_error("language file not found: " + std::string(path));
}

std::string QueryText::Locate(std::size_t offset) const {
    const Segment* segment = nullptr;
    for (const Segment& candidate : segments) {
        if (candidate.offset <= offset) {
            segment = &candidate;
        }
    }
    if (segment == nullptr) {
        return "<no query source>";
    }
    return segment->path + ":" + std::to_string(querydata::LineOfOffset(text.substr(segment->offset), offset - segment->offset));
}

QueryText CompileQueryFiles(const std::vector<std::string>& paths) {
    QueryText out;
    for (const std::string& path : paths) {
        const std::string& compiled = CompiledQuery(path);
        out.segments.push_back({.path = path, .offset = out.text.size()});
        out.text += compiled;
        if (!out.text.empty() && out.text.back() != '\n') {
            out.text += '\n';
        }
    }
    return out;
}

} // namespace ned::editor
