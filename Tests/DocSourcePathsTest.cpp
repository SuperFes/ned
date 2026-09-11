#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// CLAUDE.md and ROADMAP.md navigate by file path -- "see `Editor/Lsp/Manager.h`"
// is how both documents point at the thing they are describing, and CLAUDE.md
// is loaded into context at the start of every session. A path that no longer
// resolves is worse than no path: it sends a reader (or an agent) looking for a
// file that isn't there, and it reads as authoritative while doing it.
//
// This is not hypothetical. Commit e225510 ("drop the Lsp prefix from
// Editor/Lsp's files and symbols") and its siblings renamed essentially every
// file under Editor/Lsp, Dap, Acp, Vcs, TestRun, Project and Coverage --
// LspManager.cpp -> Lsp/Manager.cpp, ProjectTrust.h -> Project/Trust.h, and so
// on. Neither document was updated, so between that commit and this test 35
// path references across the two were silently dangling.
//
// Same shape as ThemeKeyDocsTest: hold the docs against the real tree, because
// the docs are load-bearing rather than explanatory.

namespace {

namespace fs = std::filesystem;

fs::path RepoRoot() { return fs::path(NED_REPO_ROOT); }

std::string ReadFile(const fs::path& path) {
    std::ifstream in(path);
    REQUIRE(in); // a tracked doc named here is part of the deliverable
    std::ostringstream content;
    content << in.rdbuf();
    return content.str();
}

// Every real file under the directories the docs actually reference, indexed
// both by full repo-relative path and by bare basename -- the docs use both
// forms ("`Source/UI/Widget.h`", "`Widget.h`") and both need checking.
struct SourceIndex {
    std::set<std::string>                        relativePaths;
    std::map<std::string, std::vector<std::string>> byBasename;
};

const SourceIndex& Index() {
    static const SourceIndex index = [] {
        SourceIndex built;
        for (const char* top : {"Source", "Tests", "Tools", "CMake", "Docs"}) {
            const fs::path dir = RepoRoot() / top;
            if (!fs::exists(dir)) continue;
            for (const auto& entry : fs::recursive_directory_iterator(dir)) {
                if (!entry.is_regular_file()) continue;
                const std::string rel = fs::relative(entry.path(), RepoRoot()).generic_string();
                built.relativePaths.insert(rel);
                built.byBasename[entry.path().filename().string()].push_back(rel);
            }
        }
        return built;
    }();
    return index;
}

// Paths that are deliberately not files in this repo.
bool IsExternal(const std::string& basename) {
    static const std::set<std::string> kExternal = {
        // Third-party headers reached through an include path, not a repo path.
        "api.h", "parser.h", "pcre2.h", "c_api.h", "json.hpp",
        "gc_cpp.h", "gc_pthread_redirects.h",
        // Generated at configure time into the build tree, not checked in.
        "NedVersion.h",
    };
    if (kExternal.count(basename) > 0) return true;

    // Prose that quotes a filename as an *example* rather than referencing one.
    // Docs/BufferViewDecomposition.md discusses a suffix-stripping bug by
    // showing its bad output: "a blind strip produced `ner.h`". Backticked, but
    // not a reference -- and the sentence stops making sense if it resolves.
    static const std::set<std::string> kProseExamples = {"ner.h"};
    return kProseExamples.count(basename) > 0;
}

// The docs write "`Foo.h/.cpp`" to mean the pair. Normalize that to "Foo.h" so
// the shorthand is checked rather than skipped.
std::string NormalizePairShorthand(std::string token) {
    static constexpr std::string_view kPair = ".h/.cpp";
    if (token.size() > kPair.size() && token.compare(token.size() - kPair.size(), kPair.size(), kPair) == 0)
        token = token.substr(0, token.size() - kPair.size()) + ".h";
    return token;
}

bool Resolves(const std::string& token) {
    const SourceIndex& index    = Index();
    const std::string  basename = token.substr(token.find_last_of('/') + 1);

    if (IsExternal(basename)) return true;

    // A slash-qualified reference must match a real path's tail, so that
    // "Editor/Lsp/LspManager.cpp" fails even though "Manager.cpp" exists.
    if (token.find('/') != std::string::npos) {
        for (const std::string& rel : index.relativePaths)
            if (rel.size() >= token.size() && rel.compare(rel.size() - token.size(), token.size(), token) == 0)
                return true;
        return false;
    }
    return index.byBasename.count(basename) > 0;
}

std::vector<std::string> DanglingPathsIn(const std::string& doc) {
    // Backticked tokens that look like a repo source path. Deliberately limited
    // to .h/.cpp: a bare "highlights.scm" or "locals.scm" in these docs names
    // tree-sitter's own upstream file convention, not a file in this tree.
    static const std::regex kPath(R"(`([A-Za-z][A-Za-z0-9_/]*(?:\.h/\.cpp|\.h|\.cpp))`)");

    std::set<std::string> dangling;
    for (auto it = std::sregex_iterator(doc.begin(), doc.end(), kPath); it != std::sregex_iterator(); ++it) {
        const std::string token = NormalizePairShorthand((*it)[1].str());
        if (!Resolves(token)) dangling.insert((*it)[1].str());
    }
    return {dangling.begin(), dangling.end()};
}

std::string Describe(const std::vector<std::string>& dangling) {
    std::ostringstream out;
    for (const std::string& d : dangling) out << "\n    " << d;
    return out.str();
}

} // namespace

TEST_CASE("CLAUDE.md's source-path references all resolve", "[Docs]") {
    // CLAUDE.md is deliberately NOT tracked in this repo -- it is ignored
    // globally, by the author's own choice -- so a fresh CI checkout does not
    // have one. Check it when it is there (it is the file loaded into an
    // agent's context every session, which is exactly why its paths must
    // resolve) and skip when it is not, rather than failing a build for the
    // absence of an untracked local file.
    const fs::path path = RepoRoot() / "CLAUDE.md";
    if (!fs::exists(path)) {
        SUCCEED("no CLAUDE.md in this checkout (untracked by design)");
        return;
    }
    const std::vector<std::string> dangling = DanglingPathsIn(ReadFile(path));
    INFO("CLAUDE.md names files that do not exist:" << Describe(dangling));
    CHECK(dangling.empty());
}

TEST_CASE("ROADMAP.md's source-path references all resolve", "[Docs]") {
    const std::vector<std::string> dangling = DanglingPathsIn(ReadFile(RepoRoot() / "ROADMAP.md"));
    INFO("ROADMAP.md names files that do not exist:" << Describe(dangling));
    CHECK(dangling.empty());
}

TEST_CASE("Docs/*.md source-path references all resolve", "[Docs]") {
    for (const auto& entry : fs::directory_iterator(RepoRoot() / "Docs")) {
        if (entry.path().extension() != ".md") continue;
        const std::vector<std::string> dangling = DanglingPathsIn(ReadFile(entry.path()));
        INFO(entry.path().filename().string() << " names files that do not exist:" << Describe(dangling));
        CHECK(dangling.empty());
    }
}

TEST_CASE("The dangling-path check would actually catch a rename", "[Docs]") {
    // Guards the guard: if Resolves() ever became permissive enough to accept
    // anything, the three cases above would pass vacuously and the drift they
    // exist to catch would come straight back.
    CHECK(Resolves("Source/UI/Widget.h"));
    CHECK(Resolves("UI/Widget.h"));
    CHECK(Resolves("Widget.h"));
    CHECK(Resolves("Editor/Lsp/Manager.cpp"));

    CHECK_FALSE(Resolves("Editor/Lsp/LspManager.cpp")); // the real pre-e225510 spelling
    CHECK_FALSE(Resolves("Editor/ProjectTrust.h"));
    CHECK_FALSE(Resolves("NoSuchFileAnywhere.h"));
    CHECK_FALSE(Resolves("UI/Widget.cpp/NotAFile.h"));

    CHECK(Resolves("tree_sitter/api.h")); // external, allowlisted
    CHECK(NormalizePairShorthand("Editor/Mode.h/.cpp") == "Editor/Mode.h");
}
