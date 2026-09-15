#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "Editor/FormatConfigParse.h"

// format.janet has no generator and no schema browser -- Docs/FormattingRules.md's
// "Schema" section is the only way to find out what it accepts, mirroring
// ThemeKeyDocsTest.cpp's exact reasoning for Docs/Themes.md. Held against
// FormatConfigParse.h's real key lists in both directions.

namespace {

std::string ReadFormattingDocs() {
    const std::filesystem::path path = std::filesystem::path(NED_REPO_ROOT) / "Docs" / "FormattingRules.md";
    std::ifstream               in(path);
    REQUIRE(in); // the docs are part of the deliverable; a missing file is a failure
    std::ostringstream content;
    content << in.rdbuf();
    return content.str();
}

// The fenced key-reference block, so an incidental mention of a key
// elsewhere in the prose cannot stand in for a real reference entry.
std::string KeyReferenceSection(const std::string& docs) {
    static constexpr std::string_view kBegin = "<!-- format-keys:begin -->";
    static constexpr std::string_view kEnd   = "<!-- format-keys:end -->";

    const std::size_t begin = docs.find(kBegin);
    const std::size_t end   = docs.find(kEnd);
    REQUIRE(begin != std::string::npos);
    REQUIRE(end != std::string::npos);
    REQUIRE(begin < end);
    return docs.substr(begin, end - begin);
}

// Unlike theme keys (underscored), format.janet keys are hyphenated.
std::set<std::string> BacktickedTokens(const std::string& text) {
    static const std::regex kToken(R"(`([a-z0-9_-]+)`)");
    std::set<std::string>   tokens;
    for (auto it = std::sregex_iterator(text.begin(), text.end(), kToken); it != std::sregex_iterator(); ++it) {
        tokens.insert((*it)[1].str());
    }
    return tokens;
}

std::vector<std::string> AllRealKeys() {
    std::vector<std::string> keys = ned::editor::FormatConfigKeys();
    for (const std::string& key : ned::editor::FormatConfigIndentEntryKeys()) {
        keys.push_back(key);
    }
    for (const std::string& key : ned::editor::FormatConfigSpaceEntryKeys()) {
        keys.push_back(key);
    }
    for (const std::string& key : ned::editor::FormatConfigBreakEntryKeys()) {
        keys.push_back(key);
    }
    for (const std::string& key : ned::editor::FormatConfigBlankEntryKeys()) {
        keys.push_back(key);
    }
    return keys;
}

} // namespace

TEST_CASE("Every format.janet key is documented", "[FormatKeyDocs]") {
    const std::set<std::string> documented = BacktickedTokens(KeyReferenceSection(ReadFormattingDocs()));

    std::vector<std::string> missing;
    for (const std::string& key : AllRealKeys()) {
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

TEST_CASE("Every documented format.janet key still exists", "[FormatKeyDocs]") {
    const std::vector<std::string> keys = AllRealKeys();
    const std::set<std::string>    real(keys.begin(), keys.end());

    std::vector<std::string> stale;
    for (const std::string& token : BacktickedTokens(KeyReferenceSection(ReadFormattingDocs()))) {
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
