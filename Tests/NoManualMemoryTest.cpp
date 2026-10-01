#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <regex>
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
