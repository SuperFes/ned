#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <string>

// Ownership goes through std::unique_ptr/std::shared_ptr and the standard
// containers; a new or delete expression in editor code is a lifetime someone
// has to get right by hand.
TEST_CASE("No source file uses a new or delete expression", "[MemorySafety]") {
    namespace fs          = std::filesystem;
    const fs::path   root = fs::path(NED_REPO_ROOT) / "Source";
    const std::regex newOrDelete(R"((^|[^\w.>:])(new\s+[A-Za-z_:(]|delete\b(\s*\[\s*\])?\s*[A-Za-z_(*]))");
    const std::regex blockComment(R"(^\s*(/\*|\*))");
    const std::regex stringLiteral(R"("([^"\\]|\\.)*")");

    std::string offenders;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        const fs::path& path = entry.path();
        if (!entry.is_regular_file() || (path.extension() != ".h" && path.extension() != ".cpp")) {
            continue;
        }
        std::ifstream in(path);
        std::string   line;
        for (std::size_t number = 1; std::getline(in, line); ++number) {
            line = std::regex_replace(line, stringLiteral, "\"\"");
            if (const auto comment = line.find("//"); comment != std::string::npos) {
                line.erase(comment);
            }
            if (std::regex_search(line, blockComment)) {
                continue;
            }
            if ((line.find("new") != std::string::npos || line.find("delete") != std::string::npos) &&
                std::regex_search(line, newOrDelete)) {
                offenders += fs::relative(path, root).generic_string() + ":" + std::to_string(number) + "\n";
            }
        }
    }
    INFO("Own it with std::unique_ptr/std::make_unique or a container instead:\n"
         << offenders);
    REQUIRE(offenders.empty());
}

// Notcurses frees every plane when it stops for a suspend, so a widget that
// keeps an ncplane* across frames must release it from main's onSuspend
// first. A new holder belongs on this list only once it does.
TEST_CASE("Every widget keeping an ncplane across frames is released on suspend", "[MemorySafety]") {
    namespace fs                     = std::filesystem;
    const fs::path              root = fs::path(NED_REPO_ROOT) / "Source";
    const std::regex            member(R"(^\s*(mutable\s+)?ncplane\s*\*\s*\w+\s*(=|;))");
    const std::set<std::string> released = {"UI/AcpPanel/InlineImages.h", "UI/EventLoop.h", "UI/Minimap.h"};

    std::set<std::string> holders;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".h") {
            continue;
        }
        std::ifstream in(entry.path());
        std::string   line;
        while (std::getline(in, line)) {
            if (std::regex_search(line, member)) {
                holders.insert(fs::relative(entry.path(), root).generic_string());
                break;
            }
        }
    }
    CHECK(holders == released);
}
