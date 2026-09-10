#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "UI/ThemeFile.h"

// A theme is written as (ned/theme-set "key" "value") calls in the user's own
// init.janet -- there is no theme file format and no generator any more -- so
// Docs/Themes.md's key reference is the *only* way to find out what can be
// set. That makes the docs load-bearing rather than explanatory, and a key
// added to ui::ThemeKeys() without a line in the reference is a key nobody
// can discover.
//
// So: hold the two against each other, in both directions. A missing key is
// undiscoverable; a documented key that no longer exists sends someone
// chasing a setting that silently does nothing.

namespace {

std::string ReadThemeDocs() {
    const std::filesystem::path path = std::filesystem::path(NED_REPO_ROOT) / "Docs" / "Themes.md";
    std::ifstream               in(path);
    REQUIRE(in); // the docs are part of the deliverable; a missing file is a failure
    std::ostringstream content;
    content << in.rdbuf();
    return content.str();
}

// The fenced key-reference block, so an incidental mention of a key
// elsewhere in the prose cannot stand in for a real reference entry.
std::string KeyReferenceSection(const std::string& docs) {
    static constexpr std::string_view kBegin = "<!-- theme-keys:begin -->";
    static constexpr std::string_view kEnd   = "<!-- theme-keys:end -->";

    const std::size_t begin = docs.find(kBegin);
    const std::size_t end   = docs.find(kEnd);
    REQUIRE(begin != std::string::npos);
    REQUIRE(end != std::string::npos);
    REQUIRE(begin < end);
    return docs.substr(begin, end - begin);
}

std::set<std::string> BacktickedTokens(const std::string& text) {
    static const std::regex kToken(R"(`([a-z0-9_]+)`)");
    std::set<std::string>   tokens;
    for (auto it = std::sregex_iterator(text.begin(), text.end(), kToken); it != std::sregex_iterator(); ++it) {
        tokens.insert((*it)[1].str());
    }
    return tokens;
}

} // namespace

TEST_CASE("Every settable theme key is documented", "[ThemeKeyDocs]") {
    const std::set<std::string> documented = BacktickedTokens(KeyReferenceSection(ReadThemeDocs()));

    std::vector<std::string> missing;
    for (const std::string& key : ned::ui::ThemeKeys()) {
        if (!documented.contains(key)) {
            missing.push_back(key);
        }
    }

    INFO("undocumented keys: " << [&] {
        std::string joined;
        for (const std::string& key : missing) {
            joined += key + " ";
        }
        return joined;
    }());
    REQUIRE(missing.empty());
}

TEST_CASE("Every documented theme key still exists", "[ThemeKeyDocs]") {
    const std::vector<std::string> keys = ned::ui::ThemeKeys();
    const std::set<std::string>    real(keys.begin(), keys.end());

    std::vector<std::string> stale;
    for (const std::string& token : BacktickedTokens(KeyReferenceSection(ReadThemeDocs()))) {
        if (!real.contains(token)) {
            stale.push_back(token);
        }
    }

    INFO("documented but not settable: " << [&] {
        std::string joined;
        for (const std::string& key : stale) {
            joined += key + " ";
        }
        return joined;
    }());
    REQUIRE(stale.empty());
}
