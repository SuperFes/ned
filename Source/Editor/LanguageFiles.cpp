#include "LanguageFiles.h"

#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

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

} // namespace

std::optional<std::string_view> FindEmbeddedLanguageFile(std::string_view path) {
    for (const EmbeddedLanguageFile& file : EmbeddedLanguageFiles()) {
        if (file.path == path) {
            return file.content;
        }
    }
    return std::nullopt;
}

std::string ReadLanguageFile(std::string_view path) {
    if (const std::optional<std::string_view> embedded = FindEmbeddedLanguageFile(path)) {
        return std::string(*embedded);
    }
    const std::filesystem::path fsPath(path);
    if (fsPath.is_absolute()) {
        std::ifstream in(fsPath, std::ios::binary);
        if (in) {
            std::ostringstream buffer;
            buffer << in.rdbuf();
            return buffer.str();
        }
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
