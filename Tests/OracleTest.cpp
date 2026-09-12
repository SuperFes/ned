#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "Editor/CodeFold.h"
#include "Editor/Mode.h"
#include "Editor/SyntaxTheme.h"

// Phase 0 of the parsing-engine work (Docs/ParsingEngine.md): the oracle.
//
// A checked-in snapshot of what the CURRENT tree-sitter-backed Mode
// capabilities produce, over a small corpus of real files, so that every
// later phase has something to be validated against. Without it, "the trait
// engine reproduces 53 of 55 fold nodes with zero hand-written rules" is an
// assertion nobody can check.
//
// This is deliberately a *characterization* test, not a correctness one. It
// does not claim the current output is right -- several entries in
// ROADMAP.md say parts of it are not. It claims only that a change to the
// output is visible in a diff and had to be looked at on purpose.
//
// Regenerate after an intentional change:
//
//     NED_BLESS_ORACLE=1 ./build/ned_tests "[Oracle]"
//
// then read the diff before committing it. Blessing without reading is the
// one way this test becomes worthless.
//
// Two rendering rules carry the whole thing:
//
//  - Enum NAMES, never numbers. Reordering SyntaxClass must not churn every
//    golden file, and a semantic change must not hide behind a stable index.
//  - Byte ranges AND the captured text. Offsets alone are unreadable in a
//    diff; text alone cannot tell two same-named captures apart.

namespace {

namespace fs = std::filesystem;
namespace codefold = ned::editor::codefold;

fs::path OracleDir() { return fs::path(NED_REPO_ROOT) / "Tests" / "Oracle"; }

bool Blessing() { return std::getenv("NED_BLESS_ORACLE") != nullptr; }

std::string ReadFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in);
    std::ostringstream content;
    content << in.rdbuf();
    return content.str();
}

std::string KindName(ned::editor::SymbolKind kind) {
    using ned::editor::SymbolKind;
    switch (kind) {
        case SymbolKind::Callable:  return "Callable";
        case SymbolKind::TypeLike:  return "TypeLike";
        case SymbolKind::Data:      return "Data";
        case SymbolKind::Namespace: return "Namespace";
    }
    return "?";
}

std::string KindName(ned::editor::LocalCaptureKind kind) {
    using ned::editor::LocalCaptureKind;
    switch (kind) {
        case LocalCaptureKind::Scope:      return "Scope";
        case LocalCaptureKind::Definition: return "Definition";
        case LocalCaptureKind::Reference:  return "Reference";
    }
    return "?";
}

// The captured bytes, made safe for a single line of a golden file. Long
// spans (a whole class body) are elided in the middle: the head and tail are
// what identify the construct, and a fold range's interior is exactly the
// part most likely to churn for reasons this test shouldn't care about.
std::string Excerpt(std::string_view text, std::size_t start, std::size_t end) {
    if (start > text.size() || end > text.size() || start >= end) return "";
    std::string_view raw = text.substr(start, end - start);

    std::string out;
    auto        append = [&out](std::string_view part) {
        for (const char c : part) {
            switch (c) {
                case '\n': out += "\\n"; break;
                case '\t': out += "\\t"; break;
                case '\r': out += "\\r"; break;
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                default:   out += c;
            }
        }
    };

    static constexpr std::size_t kHead = 28;
    static constexpr std::size_t kTail = 12;
    if (raw.size() <= kHead + kTail + 5) {
        append(raw);
    }
    else {
        append(raw.substr(0, kHead));
        out += " … ";
        append(raw.substr(raw.size() - kTail));
    }
    return out;
}

// One fact, pre-rendered. Sorting these rather than each capability's own
// output is what makes the file order-independent -- FoldFunction's own doc
// comment says its order is unspecified.
struct Fact {
    std::string kind;
    std::size_t start = 0;
    std::size_t end   = 0;
    std::string detail;
    std::string text;

    bool operator<(const Fact& other) const {
        return std::tie(kind, start, end, detail, text) <
               std::tie(other.kind, other.start, other.end, other.detail, other.text);
    }
};

std::vector<Fact> CollectFacts(const ned::editor::Mode& mode, const std::string& text) {
    std::vector<Fact> facts;
    auto add = [&](std::string kind, std::size_t s, std::size_t e, std::string detail = "") {
        facts.push_back(Fact{std::move(kind), s, e, std::move(detail), Excerpt(text, s, e)});
    };

    if (mode.highlight) {
        for (const auto& span : mode.highlight(text, {})) {
            add("highlight", span.startByte, span.endByte, ned::editor::SyntaxClassName(span.syntaxClass));
        }
    }
    if (mode.fold) {
        // Through FoldableBlocks, not mode.fold directly: that is what every
        // real consumer calls, and it is where the "a fold spans more than one
        // line" rule lives. Recording the raw source instead would snapshot
        // ranges -- a single-line `()` parameter list, an empty `[]` -- that
        // never become fold affordances for anyone.
        for (const auto& [s, e] : codefold::FoldableBlocks(mode, text)) add("fold", s, e);
    }
    if (mode.symbolKind) {
        for (const auto& m : mode.symbolKind(text)) add("symbol", m.startByte, m.endByte, KindName(m.kind) + " " + m.name);
    }
    if (mode.testDiscovery) {
        for (const auto& m : mode.testDiscovery(text)) add("test", m.startByte, m.endByte, m.name);
    }
    if (mode.importTargets) {
        for (const auto& t : mode.importTargets(text)) add("import", t.startByte, t.endByte, t.target);
    }
    if (mode.localScopes) {
        for (const auto& c : mode.localScopes(text)) {
            std::string detail = KindName(c.kind);
            if (!c.qualifier.empty()) detail += "." + c.qualifier;
            add("local", c.startByte, c.endByte, detail);
        }
    }
    if (mode.embeddedRegions) {
        for (const auto& r : mode.embeddedRegions(text)) add("embedded", r.startByte, r.endByte, r.language);
    }
    if (mode.indentColumn) {
        // Per line, since IndentFunction is the one capability that isn't a
        // whole-document query. Only lines it actually has an opinion on are
        // recorded, so an unchanged file's golden doesn't carry a row per line.
        std::size_t lineStart = 0;
        while (lineStart <= text.size()) {
            const std::size_t nl      = text.find('\n', lineStart);
            const std::size_t lineEnd = (nl == std::string::npos) ? text.size() : nl;
            if (const std::optional<int> column = mode.indentColumn(text, lineStart, lineEnd); column) {
                add("indent", lineStart, lineEnd, std::to_string(*column));
            }
            if (nl == std::string::npos) break;
            lineStart = nl + 1;
        }
    }

    std::sort(facts.begin(), facts.end());
    return facts;
}

std::string Render(const std::string& file, const ned::editor::Mode& mode, const std::vector<Fact>& facts) {
    std::ostringstream out;
    out << "# " << file << "  (" << mode.name << ")\n";
    out << "# " << facts.size() << " facts\n";
    for (const Fact& f : facts) {
        out << f.kind << ' ' << f.start << ".." << f.end;
        if (!f.detail.empty()) out << "  [" << f.detail << ']';
        if (!f.text.empty()) out << "  \"" << f.text << '"';
        out << '\n';
    }
    return out.str();
}

// Explicit rather than ModeForPath: this test must not depend on whatever
// ModeOverrides' process-wide table happens to hold.
const std::map<std::string, std::function<ned::editor::Mode()>>& ModeByExtension() {
    static const std::map<std::string, std::function<ned::editor::Mode()>> kModes = {
        {".json", ned::editor::JsonMode},  {".cpp", ned::editor::CppMode},
        {".py", ned::editor::PythonMode},  {".clj", ned::editor::ClojureMode},
        {".html", ned::editor::HtmlMode},  {".c", ned::editor::CMode},
        {".go", ned::editor::GoMode},      {".rs", ned::editor::RustMode},
        {".java", ned::editor::JavaMode},  {".cs", ned::editor::CSharpMode},
        {".js", ned::editor::JavaScriptMode},
        {".ts", ned::editor::TypeScriptMode},
        {".kt", ned::editor::KotlinMode},   {".php", ned::editor::PhpMode},
        {".yaml", ned::editor::YamlMode},  {".css", ned::editor::CssMode},
        {".sh", ned::editor::BashMode},    {".toml", ned::editor::TomlMode},
    };
    return kModes;
}

} // namespace

TEST_CASE("The parsing oracle matches its checked-in snapshot", "[Oracle]") {
    std::vector<fs::path> corpus;
    for (const auto& entry : fs::directory_iterator(OracleDir() / "corpus")) {
        if (entry.is_regular_file()) corpus.push_back(entry.path());
    }
    std::sort(corpus.begin(), corpus.end());
    REQUIRE_FALSE(corpus.empty());

    for (const fs::path& source : corpus) {
        const std::string name = source.filename().string();
        INFO("corpus file: " << name);

        const auto factory = ModeByExtension().find(source.extension().string());
        REQUIRE(factory != ModeByExtension().end()); // a corpus file with no mode is a mistake, not a skip

        const ned::editor::Mode mode     = factory->second();
        const std::string       text     = ReadFile(source);
        const std::string       rendered = Render(name, mode, CollectFacts(mode, text));

        const fs::path golden = OracleDir() / "expected" / (name + ".oracle");
        if (Blessing()) {
            std::ofstream out(golden, std::ios::binary | std::ios::trunc);
            REQUIRE(out);
            out << rendered;
            continue;
        }

        INFO("golden: " << golden.string() << "  (regenerate: NED_BLESS_ORACLE=1 ./build/ned_tests \"[Oracle]\")");
        REQUIRE(fs::exists(golden));
        CHECK(ReadFile(golden) == rendered);
    }
}

TEST_CASE("The oracle renders names rather than enum indices", "[Oracle]") {
    // Guards the guard. If these ever became numbers, a reordered enum would
    // churn every golden file and a real semantic change could hide behind a
    // stable index -- the exact failure this snapshot exists to prevent.
    CHECK(KindName(ned::editor::SymbolKind::Callable) == "Callable");
    CHECK(KindName(ned::editor::SymbolKind::Namespace) == "Namespace");
    CHECK(KindName(ned::editor::LocalCaptureKind::Definition) == "Definition");
    CHECK(ned::editor::SyntaxClassName(ned::editor::SyntaxClass::Comment) == "comment");

    CHECK(Excerpt("hello world", 0, 5) == "hello");
    CHECK(Excerpt("a\nb", 0, 3) == "a\\nb");
    CHECK(Excerpt("abc", 5, 9).empty()); // out of range is empty, never a crash
}
