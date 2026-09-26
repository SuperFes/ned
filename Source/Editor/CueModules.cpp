#include "CueModules.h"

#include <fstream>
#include <iterator>
#include <system_error>

namespace ned::editor {

std::string ParseCueModulePath(std::string_view moduleFile) {
    std::size_t start = 0;
    while (start < moduleFile.size()) {
        const std::size_t end  = moduleFile.find('\n', start);
        std::string_view  line = moduleFile.substr(start, end == std::string_view::npos ? end : end - start);
        start                  = end == std::string_view::npos ? moduleFile.size() : end + 1;
        const std::size_t text = line.find_first_not_of(" \t");
        if (text == std::string_view::npos || !line.substr(text).starts_with("module:")) {
            continue;
        }
        line                    = line.substr(text + 7);
        const std::size_t open  = line.find('"');
        const std::size_t close = open == std::string_view::npos ? open : line.find('"', open + 1);
        if (close == std::string_view::npos) {
            return {};
        }
        const std::string_view path = line.substr(open + 1, close - open - 1);
        return std::string(path.substr(0, path.find('@')));
    }
    return {};
}

CueImportRoot CueImportRootFor(std::string_view importPath, const std::filesystem::path& searchStart) {
    if (importPath.find(':') != std::string_view::npos) {
        return {{}, std::string(importPath), {}};
    }
    std::error_code ec;
    for (std::filesystem::path dir = searchStart; !dir.empty(); dir = dir.parent_path()) {
        const std::filesystem::path cueMod = dir / "cue.mod";
        if (std::ifstream in{cueMod / "module.cue"}) {
            const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
            const std::string module = ParseCueModulePath(text);
            if (!module.empty() && importPath.starts_with(module)) {
                if (importPath.size() == module.size()) {
                    return {module, {}, dir};
                }
                if (importPath[module.size()] == '/') {
                    return {module + "/", std::string(importPath.substr(module.size() + 1)), dir};
                }
            }
            for (const char* vendored : {"gen", "pkg", "usr"}) {
                if (std::filesystem::is_directory(cueMod / vendored / importPath, ec)) {
                    return {{}, std::string(importPath), cueMod / vendored};
                }
            }
            return {{}, std::string(importPath), {}};
        }
        if (dir == dir.parent_path()) {
            break;
        }
    }
    return {{}, std::string(importPath), {}};
}

} // namespace ned::editor
