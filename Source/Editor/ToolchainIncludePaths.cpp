#include "ToolchainIncludePaths.h"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <sstream>
#include <utility>

#include <nlohmann/json.hpp>

#include "Process/ChildProcess.h"

namespace ned::editor {

namespace {

    std::optional<std::pair<std::string, std::string>> CompilerAndLanguageFlagFor(const std::string& language) {
        if (language == "cpp") {
            return std::make_pair(std::string("c++"), std::string("-xc++"));
        }
        if (language == "c") {
            return std::make_pair(std::string("cc"), std::string("-xc"));
        }
        return std::nullopt;
    }

    // GCC and Clang both emit exactly this block on stderr for `-v`:
    //   #include "..." search starts here:
    //   #include <...> search starts here:
    //    /usr/include/c++/13
    //    ...
    //   End of search list.
    // Only the "<...>" (angle-form) list is collected -- that's the one
    // relevant to an angle-form #include target; the quote-form list above
    // it is a strict subset (every compiler searches the including file's
    // own directory plus everything in the angle list) already covered by
    // ResolveFileLink's own baseDirectory/ProjectRoot search.
    std::vector<std::filesystem::path> ParseAngleIncludeSearchPaths(const std::string& probeOutput) {
        std::vector<std::filesystem::path> paths;
        std::istringstream                 stream(probeOutput);
        std::string                        line;
        bool                                inList = false;
        while (std::getline(stream, line)) {
            if (!inList) {
                if (line.find("#include <...> search starts here:") != std::string::npos) {
                    inList = true;
                }
                continue;
            }
            if (line.find("End of search list.") != std::string::npos) {
                break;
            }
            const std::size_t start = line.find_first_not_of(" \t");
            if (start != std::string::npos) {
                paths.emplace_back(line.substr(start));
            }
        }
        return paths;
    }

    std::mutex& TtlMutex() {
        static std::mutex mutex;
        return mutex;
    }

    int& TtlStorage() {
        static int ttlSeconds = 86400;
        return ttlSeconds;
    }

    // Absent means "can't determine" (thrown, not returned) -- mirrors
    // Editor/Backup.cpp's BackupsDirectory()/Editor/Session.cpp's
    // FilePlacesPath() exactly, just under $XDG_CACHE_HOME instead of
    // $XDG_STATE_HOME: this is disposable/regenerable data, not user config
    // or editor state, per this codebase's own XDG convention.
    std::filesystem::path CacheDirectory() {
        if (const char* xdgCacheHome = std::getenv("XDG_CACHE_HOME"); xdgCacheHome && *xdgCacheHome) {
            return std::filesystem::path(xdgCacheHome) / "ned";
        }
        if (const char* home = std::getenv("HOME"); home && *home) {
            return std::filesystem::path(home) / ".cache" / "ned";
        }
        throw std::runtime_error("ned: cannot determine cache directory (neither XDG_CACHE_HOME nor HOME is set)");
    }

    std::filesystem::path ToolchainIncludePathsCachePath() {
        return CacheDirectory() / "toolchain-include-paths.json";
    }

    std::int64_t NowUnixSeconds() {
        return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    }

} // namespace

std::optional<std::vector<std::filesystem::path>> QueryToolchainIncludePaths(const std::string& language) {
    const auto compilerAndFlag = CompilerAndLanguageFlagFor(language);
    if (!compilerAndFlag) {
        return std::nullopt;
    }
    const auto& [compiler, languageFlag] = *compilerAndFlag;

    if (!process::ResolveExecutable(compiler)) {
        return std::nullopt;
    }

    try {
        process::ChildProcess proc({compiler, "-E", "-v", languageFlag, "/dev/null"}, process::StderrMode::MergeWithStdout);
        std::string            output;
        for (;;) {
            const std::string chunk = proc.ReadSome();
            if (chunk.empty()) {
                break; // EOF
            }
            output += chunk;
        }
        proc.WaitForExit();

        std::vector<std::filesystem::path> paths = ParseAngleIncludeSearchPaths(output);
        return paths.empty() ? std::nullopt : std::optional(std::move(paths));
    }
    catch (const std::exception&) {
        return std::nullopt;
    }
}

std::vector<std::filesystem::path> ToolchainIncludePathsForLanguage(const std::string& language) {
    const int          ttl = IncludePathCacheTtlSeconds();
    const std::int64_t now = NowUnixSeconds();

    std::filesystem::path cachePath;
    bool                  haveCachePath = false;
    nlohmann::json         cache         = nlohmann::json::object();
    try {
        cachePath     = ToolchainIncludePathsCachePath();
        haveCachePath = true;
        std::ifstream file(cachePath, std::ios::binary);
        if (file) {
            nlohmann::json parsed = nlohmann::json::parse(file, nullptr, false);
            if (!parsed.is_discarded() && parsed.is_object()) {
                cache = std::move(parsed);
            }
        }
    }
    catch (const std::exception&) {
        cache = nlohmann::json::object();
    }

    if (ttl > 0 && cache.contains(language) && cache[language].is_object() && cache[language].contains("probedAt") &&
        cache[language]["probedAt"].is_number_integer() && cache[language].contains("paths") && cache[language]["paths"].is_array()) {
        const std::int64_t probedAt = cache[language]["probedAt"].get<std::int64_t>();
        if (now - probedAt < ttl) {
            std::vector<std::filesystem::path> paths;
            for (const nlohmann::json& entry : cache[language]["paths"]) {
                if (entry.is_string()) {
                    paths.emplace_back(entry.get<std::string>());
                }
            }
            return paths;
        }
    }

    std::vector<std::filesystem::path> result = QueryToolchainIncludePaths(language).value_or(std::vector<std::filesystem::path>{});

    if (haveCachePath) {
        try {
            nlohmann::json pathArray = nlohmann::json::array();
            for (const std::filesystem::path& path : result) {
                pathArray.push_back(path.string());
            }
            cache[language] = nlohmann::json{{"probedAt", now}, {"paths", pathArray}};

            std::filesystem::create_directories(cachePath.parent_path());
            const std::filesystem::path tempPath = cachePath.string() + ".ned-tmp";
            {
                std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
                out << cache.dump(2);
            }
            std::filesystem::rename(tempPath, cachePath);
        }
        catch (const std::exception&) {
            // Best-effort -- a cache write failure must not prevent the freshly probed result from being returned.
        }
    }

    return result;
}

void ClearToolchainIncludePathCache() {
    try {
        std::filesystem::remove(ToolchainIncludePathsCachePath());
    }
    catch (const std::exception&) {
    }
}

void SetIncludePathCacheTtlSeconds(int seconds) {
    const std::lock_guard<std::mutex> lock(TtlMutex());
    TtlStorage() = seconds;
}

int IncludePathCacheTtlSeconds() {
    const std::lock_guard<std::mutex> lock(TtlMutex());
    return TtlStorage();
}

} // namespace ned::editor
