#include "Php.h"

#include <algorithm>
#include <fstream>
#include <system_error>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace ned::editor::php {

namespace {

    // Appends every "psr-4" prefix -> directory-list pair found under
    // composer.json's "autoload"/"autoload-dev" table -- composer allows a
    // prefix's own value to be either a single directory string or an array
    // of them (checked several directories for the same prefix), both
    // normalized to a vector here.
    void CollectPsr4(const nlohmann::json&                                          autoloadSection,
                     std::vector<std::pair<std::string, std::vector<std::string>>>& out) {
        if (!autoloadSection.is_object()) {
            return;
        }
        const auto psr4 = autoloadSection.find("psr-4");
        if (psr4 == autoloadSection.end() || !psr4->is_object()) {
            return;
        }
        for (const auto& [prefix, dirs] : psr4->items()) {
            std::vector<std::string> dirList;
            if (dirs.is_string()) {
                dirList.push_back(dirs.get<std::string>());
            }
            else if (dirs.is_array()) {
                for (const auto& dir : dirs) {
                    if (dir.is_string()) {
                        dirList.push_back(dir.get<std::string>());
                    }
                }
            }
            if (!dirList.empty()) {
                out.emplace_back(prefix, std::move(dirList));
            }
        }
    }

} // namespace

std::optional<std::filesystem::path> ResolvePsr4Namespace(const std::string&           namespacePath,
                                                          const std::filesystem::path& projectRoot) {
    std::ifstream composerFile(projectRoot / "composer.json");
    if (!composerFile) {
        return std::nullopt;
    }
    nlohmann::json root;
    try {
        composerFile >> root;
    }
    catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
    if (!root.is_object()) {
        return std::nullopt;
    }

    std::vector<std::pair<std::string, std::vector<std::string>>> prefixes;
    if (const auto autoload = root.find("autoload"); autoload != root.end()) {
        CollectPsr4(*autoload, prefixes);
    }
    if (const auto autoloadDev = root.find("autoload-dev"); autoloadDev != root.end()) {
        CollectPsr4(*autoloadDev, prefixes);
    }
    if (prefixes.empty()) {
        return std::nullopt;
    }

    // Longest-prefix-wins, PSR-4's own resolution rule -- a project mapping
    // both "App\\" and "App\\Tests\\" must prefer the more specific one for
    // a namespace under the latter.
    std::sort(prefixes.begin(), prefixes.end(),
              [](const auto& a, const auto& b) { return a.first.size() > b.first.size(); });

    // A fully-qualified "use \Foo\Bar;" leading backslash is equivalent to
    // the unqualified form for PSR-4 prefix matching.
    std::string ns = namespacePath;
    if (!ns.empty() && ns.front() == '\\') {
        ns.erase(ns.begin());
    }

    for (const auto& [prefix, dirs] : prefixes) {
        // Composer's spec requires a trailing backslash on the prefix key;
        // tolerate a project that omitted it rather than silently never
        // matching.
        std::string normalizedPrefix = prefix;
        if (!normalizedPrefix.empty() && normalizedPrefix.back() != '\\') {
            normalizedPrefix += '\\';
        }
        if (ns.size() <= normalizedPrefix.size() || ns.compare(0, normalizedPrefix.size(), normalizedPrefix) != 0) {
            continue;
        }
        std::string remainder = ns.substr(normalizedPrefix.size());
        std::replace(remainder.begin(), remainder.end(), '\\', '/');

        for (const std::string& dir : dirs) {
            const std::filesystem::path candidate = projectRoot / dir / (remainder + ".php");
            std::error_code             existsError;
            if (std::filesystem::exists(candidate, existsError)) {
                return candidate;
            }
        }
    }
    return std::nullopt;
}

} // namespace ned::editor::php
