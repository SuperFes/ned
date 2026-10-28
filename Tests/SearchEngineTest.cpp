#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <stop_token>
#include <fstream>
#include <string>
#include <vector>

#include "Editor/Project/SearchEngine.h"

using ned::editor::LiveContent;
using ned::editor::SearchDirectory;
using ned::editor::SearchFiles;
using ned::editor::SearchMatch;
using ned::editor::SearchOptions;
using ned::editor::SearchPattern;
using ned::editor::SearchPatternError;
using ned::editor::SearchRoot;
using ned::editor::StreamSearchDirectory;

namespace {

std::filesystem::path FreshDir(const char* name) {
    const std::filesystem::path dir = std::filesystem::absolute(std::filesystem::temp_directory_path() / name);
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

LiveContent Live(const std::filesystem::path& path, std::string text) {
    return LiveContent{path.lexically_normal(), std::filesystem::weakly_canonical(path), std::move(text), std::nullopt};
}

} // namespace

TEST_CASE("SearchPattern rejects invalid syntax and matches anywhere in a line", "[SearchEngine]") {
    REQUIRE_THROWS_AS(SearchPattern("(unclosed"), SearchPatternError);

    const SearchPattern pattern("ne+dle");
    REQUIRE(pattern.Matches("a needle here"));
    REQUIRE_FALSE(pattern.Matches("nothing"));
}

TEST_CASE("SearchDirectory searches live content in place of its file", "[SearchEngine]") {
    const std::filesystem::path dir = FreshDir("ned_search_engine_live");
    {
        std::ofstream(dir / "a.txt") << "stale needle\n";
    }
    const std::vector<LiveContent> live{Live(dir / "a.txt", "first\nfresh needle\n")};

    const std::vector<SearchMatch> matches = SearchDirectory(dir, SearchPattern("needle"), live);

    REQUIRE(matches.size() == 1);
    REQUIRE(matches[0].file == dir / "a.txt");
    REQUIRE(matches[0].lineNumber == 2);
    REQUIRE(matches[0].lineText == "fresh needle");

    std::filesystem::remove_all(dir);
}

TEST_CASE("SearchDirectory returns pre-scanned matches without scanning the text", "[SearchEngine]") {
    const std::filesystem::path dir = FreshDir("ned_search_engine_prescanned");
    {
        std::ofstream(dir / "a.txt") << "needle on disk\n";
    }
    LiveContent entry = Live(dir / "a.txt", "needle in text\n");
    entry.preScanned  = std::vector<SearchMatch>{SearchMatch{dir / "a.txt", 7, "needle pre-scanned"}};
    const std::vector<LiveContent> live{entry};

    const std::vector<SearchMatch> matches = SearchDirectory(dir, SearchPattern("needle"), live);

    REQUIRE(matches.size() == 1);
    REQUIRE(matches[0].lineNumber == 7);
    REQUIRE(matches[0].lineText == "needle pre-scanned");

    std::filesystem::remove_all(dir);
}

TEST_CASE("SearchDirectory appends unwalked live content in path order under the walk's rules", "[SearchEngine]") {
    const std::filesystem::path dir     = FreshDir("ned_search_engine_unwalked");
    const std::filesystem::path outside = FreshDir("ned_search_engine_unwalked_outside");
    {
        std::ofstream(dir / "disk.txt") << "needle\n";
    }
    // Out of path order on purpose; the hidden and out-of-root entries are dropped.
    const std::vector<LiveContent> live{Live(dir / "z.txt", "needle z\n"), Live(dir / ".hidden" / "h.txt", "needle h\n"),
                                        Live(outside / "o.txt", "needle o\n"), Live(dir / "m.txt", "needle m\n")};

    const std::vector<SearchMatch> matches = SearchDirectory(dir, SearchPattern("needle"), live);

    REQUIRE(matches.size() == 3);
    REQUIRE(matches[0].file == dir / "disk.txt");
    REQUIRE(matches[1].file == dir / "m.txt");
    REQUIRE(matches[2].file == dir / "z.txt");

    std::filesystem::remove_all(dir);
    std::filesystem::remove_all(outside);
}

TEST_CASE("SearchDirectory matches live content opened through a symlinked directory", "[SearchEngine]") {
    const std::filesystem::path dir = FreshDir("ned_search_engine_symlink");
    std::filesystem::create_directory(dir / "real");
    {
        std::ofstream(dir / "real" / "a.txt") << "stale needle\n";
    }
    // The walk doesn't follow the link, so it reaches the file only as real/a.txt.
    std::filesystem::create_directory_symlink(dir / "real", dir / "link");
    const std::vector<LiveContent> live{Live(dir / "link" / "a.txt", "fresh needle\n")};

    const std::vector<SearchMatch> matches = SearchDirectory(dir, SearchPattern("needle"), live);

    REQUIRE(matches.size() == 1);
    REQUIRE(matches[0].file == dir / "real" / "a.txt");
    REQUIRE(matches[0].lineText == "fresh needle");

    std::filesystem::remove_all(dir);
}

TEST_CASE("SearchRoot places a path under either the root's spelling or its canonical form", "[SearchEngine]") {
    const std::filesystem::path dir = FreshDir("ned_search_engine_root");
    std::filesystem::create_directory(dir / "real");
    std::filesystem::create_directory_symlink(dir / "real", dir / "link");

    const SearchRoot root(dir / "link");
    REQUIRE(root.Valid());
    REQUIRE(root.Relative(dir / "link" / "a.txt", dir / "real" / "a.txt") == std::filesystem::path("a.txt"));
    REQUIRE(root.Relative(dir / "real" / "b" / "c.txt", dir / "real" / "b" / "c.txt") == std::filesystem::path("b/c.txt"));
    REQUIRE_FALSE(root.Relative(dir / "other.txt", dir / "other.txt").has_value());

    std::filesystem::remove_all(dir);
}

TEST_CASE("SearchDirectory under a symlinked root uses live content opened through the real path",
          "[SearchEngine]") {
    const std::filesystem::path dir = FreshDir("ned_search_engine_symlinked_root");
    std::filesystem::create_directory(dir / "real");
    {
        std::ofstream(dir / "real" / "a.txt") << "stale needle\n";
    }
    std::filesystem::create_directory_symlink(dir / "real", dir / "link");
    const std::vector<LiveContent> live{Live(dir / "real" / "a.txt", "fresh needle\n"),
                                        Live(dir / "real" / "new.txt", "unsaved needle\n")};

    const std::vector<SearchMatch> matches = SearchDirectory(dir / "link", SearchPattern("needle"), live);

    REQUIRE(matches.size() == 2);
    REQUIRE(matches[0].file == dir / "link" / "a.txt");
    REQUIRE(matches[0].lineText == "fresh needle");
    REQUIRE(matches[1].file == dir / "link" / "new.txt");
    REQUIRE(matches[1].lineText == "unsaved needle");

    std::filesystem::remove_all(dir);
}

TEST_CASE("SearchFiles uses live content, drops duplicates and sourceless paths, and keeps list order", "[SearchEngine]") {
    const std::filesystem::path dir = FreshDir("ned_search_engine_files");
    {
        std::ofstream(dir / "b.txt") << "needle b\n";
    }
    const std::vector<LiveContent> live{Live(dir / "unsaved.txt", "needle unsaved\n")};

    const std::vector<SearchMatch> matches = SearchFiles({dir / "unsaved.txt", dir / "missing.txt", dir / "b.txt", dir / "b.txt"},
                                                         SearchPattern("needle"), live);

    REQUIRE(matches.size() == 2);
    REQUIRE(matches[0].file == dir / "unsaved.txt");
    REQUIRE(matches[1].file == dir / "b.txt");

    std::filesystem::remove_all(dir);
}

TEST_CASE("StreamSearchDirectory sends non-empty runs that add up to SearchDirectory's result", "[SearchEngine]") {
    const std::filesystem::path dir = FreshDir("ned_search_engine_stream");
    for (int d = 0; d < 8; ++d) {
        const std::filesystem::path sub = dir / ("d" + std::to_string(d));
        std::filesystem::create_directories(sub);
        for (int f = 0; f < 40; ++f) {
            std::ofstream out(sub / ("f" + std::to_string(f) + ".txt"));
            out << "none\n";
            if (f % 3 == 0) {
                out << "needle " << d << ' ' << f << "\nneedle again\n";
            }
        }
    }
    const std::vector<LiveContent> live{Live(dir / "d1" / "f1.txt", "live needle\n"), Live(dir / "unsaved.txt", "needle\n")};
    const SearchPattern            pattern("needle");
    const std::vector<SearchMatch> expected = SearchDirectory(dir, pattern, live);
    REQUIRE(expected.size() == 8 * 14 * 2 + 2);

    for (const unsigned int threads : {1u, 8u}) {
        std::vector<SearchMatch> streamed;
        std::size_t              runs = 0;
        StreamSearchDirectory(dir, pattern, live, SearchOptions{.threads = threads},
                              [&](std::vector<SearchMatch> run) {
                                  REQUIRE_FALSE(run.empty());
                                  ++runs;
                                  streamed.insert(streamed.end(), run.begin(), run.end());
                              });
        REQUIRE(runs >= 1);
        REQUIRE(streamed.size() == expected.size());
        for (std::size_t i = 0; i < expected.size(); ++i) {
            REQUIRE(streamed[i].file == expected[i].file);
            REQUIRE(streamed[i].lineNumber == expected[i].lineNumber);
            REQUIRE(streamed[i].lineText == expected[i].lineText);
        }
    }

    std::filesystem::remove_all(dir);
}

TEST_CASE("StreamSearchDirectory sends nothing more once stopped", "[SearchEngine]") {
    const std::filesystem::path dir = FreshDir("ned_search_engine_stream_stop");
    for (int f = 0; f < 200; ++f) {
        std::ofstream(dir / ("f" + std::to_string(f) + ".txt")) << "needle\n";
    }

    std::stop_source stop;
    std::size_t      runs = 0;
    StreamSearchDirectory(
        dir, SearchPattern("needle"), {}, SearchOptions{.threads = 4},
        [&](std::vector<SearchMatch>) {
            ++runs;
            stop.request_stop();
        },
        stop.get_token());
    REQUIRE(runs == 1);

    std::filesystem::remove_all(dir);
}

TEST_CASE("StreamSearchDirectory reads .editorconfig charsets only when its options say to", "[SearchEngine]") {
    const std::filesystem::path dir = FreshDir("ned_search_engine_stream_editorconfig");
    std::ofstream(dir / ".editorconfig", std::ios::binary) << "root = true\n[*.txt]\ncharset = latin1\n";
    std::ofstream(dir / "old.txt", std::ios::binary) << "caf\xE9 needle\n";

    const auto count = [&](bool editorConfig) {
        std::size_t matches = 0;
        StreamSearchDirectory(dir, SearchPattern("caf\xC3\xA9 needle"), {}, SearchOptions{.threads = 1, .editorConfig = editorConfig},
                              [&](std::vector<SearchMatch> run) { matches += run.size(); });
        return matches;
    };
    REQUIRE(count(true) == 1);
    REQUIRE(count(false) == 0);

    std::filesystem::remove_all(dir);
}

TEST_CASE("SearchDirectory walks each directory in name order, depth first", "[SearchEngine]") {
    const std::filesystem::path dir = FreshDir("ned_search_engine_order");
    // Created out of order: the result must not depend on what the filesystem lists first.
    for (const char* name : {"z.txt", "m.txt", "b/y.txt", "a.txt", "b/c.txt"}) {
        std::filesystem::create_directories((dir / name).parent_path());
        std::ofstream(dir / name) << "needle\n";
    }

    std::vector<std::filesystem::path> files;
    for (const SearchMatch& match : SearchDirectory(dir, "needle")) {
        files.push_back(match.file.lexically_relative(dir));
    }
    REQUIRE(files == std::vector<std::filesystem::path>{"a.txt", "b/c.txt", "b/y.txt", "m.txt", "z.txt"});

    std::filesystem::remove_all(dir);
}
