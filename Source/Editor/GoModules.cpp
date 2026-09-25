#include "GoModules.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <optional>
#include <sstream>
#include <system_error>
#include <utility>

#include "Editor/Process/ChildProcess.h"

namespace ned::editor {

namespace {

    // A go.mod line's tokens, with `//` comments dropped and quoted strings
    // ("..." or `...`) unquoted.
    std::vector<std::string> Tokens(std::string_view line) {
        std::vector<std::string> tokens;
        std::size_t              i = 0;
        while (i < line.size()) {
            const char c = line[i];
            if (std::isspace(static_cast<unsigned char>(c)) != 0) {
                ++i;
                continue;
            }
            if (line.substr(i, 2) == "//") {
                break;
            }
            if (c == '"' || c == '`') {
                const std::size_t close = line.find(c, i + 1);
                const std::size_t end   = close == std::string_view::npos ? line.size() : close;
                tokens.emplace_back(line.substr(i + 1, end - i - 1));
                i = end + 1;
                continue;
            }
            std::size_t end = i;
            while (end < line.size() && std::isspace(static_cast<unsigned char>(line[end])) == 0) {
                ++end;
            }
            tokens.emplace_back(line.substr(i, end - i));
            i = end;
        }
        return tokens;
    }

    void ApplyDirective(GoModFile& file, const std::string& verb, const std::vector<std::string>& args) {
        if (verb == "module" && !args.empty()) {
            file.module = args[0];
        }
        else if (verb == "require" && args.size() >= 2) {
            file.requirements.push_back({args[0], args[1]});
        }
        else if (verb == "replace") {
            // path [version] => newPath [newVersion]
            const auto arrow = std::find(args.begin(), args.end(), "=>");
            if (arrow == args.end() || arrow == args.begin() || arrow + 1 == args.end()) {
                return;
            }
            GoModReplace replace;
            replace.path = args[0];
            if (arrow - args.begin() == 2) {
                replace.version = args[1];
            }
            replace.newPath = *(arrow + 1);
            if (arrow + 2 != args.end()) {
                replace.newVersion = *(arrow + 2);
            }
            file.replaces.push_back(std::move(replace));
        }
    }

    // Whether prefix names path or one of its parent directories.
    bool IsPathPrefix(std::string_view path, std::string_view prefix) {
        return !prefix.empty() && path.starts_with(prefix) &&
               (path.size() == prefix.size() || path[prefix.size()] == '/');
    }

    GoImportRoot Split(std::string_view importPath, std::string_view prefix, std::filesystem::path root) {
        if (importPath.size() == prefix.size()) {
            return {std::string(prefix), {}, std::move(root)};
        }
        return {std::string(prefix) + "/", std::string(importPath.substr(prefix.size() + 1)), std::move(root)};
    }

    bool IsLocalReplacement(std::string_view newPath) {
        return newPath.starts_with("./") || newPath.starts_with("../") || newPath.starts_with("/");
    }

    std::filesystem::path ModuleCacheDirectory(std::string_view path, std::string_view version) {
        const std::filesystem::path cache = GoModuleCache();
        if (cache.empty() || version.empty()) {
            return {};
        }
        return cache / (EscapeGoModulePath(path) + "@" + std::string(version));
    }

    std::optional<std::pair<std::filesystem::path, GoModFile>> NearestGoMod(const std::filesystem::path& start) {
        std::error_code ec;
        for (std::filesystem::path dir = start; !dir.empty(); dir = dir.parent_path()) {
            const std::filesystem::path candidate = dir / "go.mod";
            if (std::filesystem::is_regular_file(candidate, ec)) {
                std::ifstream      in(candidate, std::ios::binary);
                std::ostringstream text;
                text << in.rdbuf();
                return std::make_pair(dir, ParseGoMod(text.str()));
            }
            if (dir == dir.parent_path()) {
                break;
            }
        }
        return std::nullopt;
    }

    struct GoEnvironment {
        std::filesystem::path root;
        std::filesystem::path moduleCache;
    };

    // `go env GOROOT GOMODCACHE`, asked once: both answers come from the same
    // toolchain, and go env also knows settings written by `go env -w`.
    const GoEnvironment& AskedGoEnvironment() {
        static std::mutex                   mutex;
        static std::optional<GoEnvironment> asked;
        const std::lock_guard<std::mutex>   lock(mutex);
        if (asked) {
            return *asked;
        }
        asked.emplace();
        if (!process::ResolveExecutable("go")) {
            return *asked;
        }
        try {
            process::ChildProcess proc({"go", "env", "GOROOT", "GOMODCACHE"});
            std::string           output;
            while (const std::optional<std::string> chunk = proc.ReadSome(std::chrono::milliseconds(2000))) {
                if (chunk->empty()) {
                    break;
                }
                output += *chunk;
            }
            proc.Kill();
            proc.WaitForExit();
            std::istringstream lines(output);
            std::string        line;
            if (std::getline(lines, line)) {
                asked->root = line;
            }
            if (std::getline(lines, line)) {
                asked->moduleCache = line;
            }
        }
        catch (const std::exception&) {
            // No toolchain answer: the standard library and module cache stay unresolved.
        }
        return *asked;
    }

    std::filesystem::path EnvironmentPath(const char* name) {
        const char* value = std::getenv(name);
        return value != nullptr ? std::filesystem::path(value) : std::filesystem::path{};
    }

} // namespace

GoModFile ParseGoMod(std::string_view text) {
    GoModFile   file;
    std::string block;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t              end    = text.find('\n', start);
        const std::string_view         line   = text.substr(start, end == std::string_view::npos ? end : end - start);
        const std::vector<std::string> tokens = Tokens(line);
        start                                 = end == std::string_view::npos ? text.size() + 1 : end + 1;
        if (tokens.empty()) {
            continue;
        }
        if (!block.empty()) {
            if (tokens[0] == ")") {
                block.clear();
            }
            else {
                ApplyDirective(file, block, tokens);
            }
            continue;
        }
        if (tokens.size() >= 2 && tokens[1] == "(") {
            block = tokens[0];
            continue;
        }
        ApplyDirective(file, tokens[0], std::vector<std::string>(tokens.begin() + 1, tokens.end()));
    }
    return file;
}

std::string EscapeGoModulePath(std::string_view path) {
    std::string escaped;
    escaped.reserve(path.size());
    for (const char c : path) {
        if (std::isupper(static_cast<unsigned char>(c)) != 0) {
            escaped += '!';
            escaped += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        else {
            escaped += c;
        }
    }
    return escaped;
}

GoImportRoot GoImportRootFor(std::string_view importPath, const std::filesystem::path& searchStart) {
    if (const auto goMod = NearestGoMod(searchStart)) {
        const auto& [directory, file] = *goMod;
        if (IsPathPrefix(importPath, file.module)) {
            return Split(importPath, file.module, directory);
        }

        const GoModRequire* required = nullptr;
        for (const GoModRequire& requirement : file.requirements) {
            if (IsPathPrefix(importPath, requirement.path) &&
                (required == nullptr || requirement.path.size() > required->path.size())) {
                required = &requirement;
            }
        }
        const GoModReplace* replaced = nullptr;
        for (const GoModReplace& replace : file.replaces) {
            if (!IsPathPrefix(importPath, replace.path)) {
                continue;
            }
            if (required != nullptr && (replace.path != required->path ||
                                        (!replace.version.empty() && replace.version != required->version))) {
                continue;
            }
            if (replaced == nullptr || replace.path.size() > replaced->path.size()) {
                replaced = &replace;
            }
        }
        if (replaced != nullptr) {
            std::filesystem::path root = IsLocalReplacement(replaced->newPath)
                                             ? (directory / replaced->newPath).lexically_normal()
                                             : ModuleCacheDirectory(replaced->newPath, replaced->newVersion);
            return Split(importPath, replaced->path, std::move(root));
        }
        if (required != nullptr) {
            return Split(importPath, required->path, ModuleCacheDirectory(required->path, required->version));
        }
    }

    // The standard library's paths are the ones whose first element has no dot.
    const std::string_view first = importPath.substr(0, importPath.find('/'));
    if (!first.empty() && first.find('.') == std::string_view::npos) {
        const std::filesystem::path goRoot = GoRoot();
        return {{}, std::string(importPath), goRoot.empty() ? goRoot : goRoot / "src"};
    }
    return {{}, std::string(importPath), {}};
}

std::filesystem::path GoRoot() {
    if (std::filesystem::path root = EnvironmentPath("GOROOT"); !root.empty()) {
        return root;
    }
    return AskedGoEnvironment().root;
}

std::filesystem::path GoModuleCache() {
    if (std::filesystem::path cache = EnvironmentPath("GOMODCACHE"); !cache.empty()) {
        return cache;
    }
    return AskedGoEnvironment().moduleCache;
}

} // namespace ned::editor
