#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include "Editor/Multibuffer.h"
#include "Text/BufferList.h"

using ned::editor::multibuffer::BuildMultibuffer;
using ned::editor::multibuffer::ClearMultibufferIndexFor;
using ned::editor::multibuffer::ClearRegistryForTesting;
using ned::editor::multibuffer::ExcerptSource;
using ned::editor::multibuffer::MultibufferIndexFor;
using ned::editor::multibuffer::ReadExcerptText;
using ned::text::Buffer;
using ned::text::BufferList;

namespace {

// The registry is keyed by raw Buffer* identity with no automatic per-Buffer
// cleanup (see ClearRegistryForTesting's own doc comment) -- without this, a
// Buffer destroyed at the end of one TEST_CASE can leave a stale entry that
// a later TEST_CASE's freshly allocated Buffer spuriously "inherits" if the
// allocator reuses the same address, exactly the RegistryResetGuard
// convention VcsRunnerTest.cpp/VcsProviderRegistryTest.cpp already use for
// VcsProviderRegistry's own global static state.
struct RegistryResetGuard {
    RegistryResetGuard() {
        ClearRegistryForTesting();
    }
    ~RegistryResetGuard() {
        ClearRegistryForTesting();
    }
};

// Mirrors BuildMultibuffer's own private MakeRuleLine exactly (78 columns
// of U+2500) -- kept here rather than exposed from Multibuffer.h so tests
// verify the real rendered width, not just "some non-empty rule exists".
std::string RuleLine() {
    std::string rule;
    for (int i = 0; i < 78; ++i) {
        rule += "─";
    }
    return rule;
}

} // namespace

TEST_CASE("BuildMultibuffer stitches excerpts, framed by a rule line above/below each one", "[Multibuffer]") {
    RegistryResetGuard guard;
    BufferList         bufferList;

    std::vector<ExcerptSource> excerpts;
    excerpts.push_back(ExcerptSource{"/repo/a.cpp", 10, 12, "a.cpp:10-12", "line 10\nline 11\nline 12\n"});
    excerpts.push_back(ExcerptSource{"/repo/b.cpp", 3, 3, "b.cpp:3-3", "line 3"}); // missing trailing newline

    Buffer& multibuffer = BuildMultibuffer(bufferList, "*test multibuffer*", excerpts);

    REQUIRE(multibuffer.ReadOnly());
    const std::string rule = RuleLine();
    REQUIRE(multibuffer.Text() == rule + "\n" +
                                      "a.cpp:10-12\n"
                                      "line 10\nline 11\nline 12\n"
                                      "\n" +
                                      rule + "\n" +
                                      "b.cpp:3-3\n"
                                      "line 3\n"
                                      "\n" +
                                      rule + "\n");
}

TEST_CASE("MultibufferIndexFor / SpanAtOffset map composite offsets back to their source excerpt", "[Multibuffer]") {
    RegistryResetGuard guard;
    BufferList         bufferList;

    std::vector<ExcerptSource> excerpts;
    excerpts.push_back(ExcerptSource{"/repo/a.cpp", 10, 12, "a.cpp:10-12", "line 10\nline 11\nline 12\n"});
    excerpts.push_back(ExcerptSource{"/repo/b.cpp", 3, 3, "b.cpp:3-3", "line 3\n"});

    Buffer& multibuffer = BuildMultibuffer(bufferList, "*test multibuffer*", excerpts);

    auto* index = MultibufferIndexFor(multibuffer);
    REQUIRE(index != nullptr);
    REQUIRE(index->Spans().size() == 2);

    // The very first line is now the leading rule, outside every span --
    // the first excerpt's own span starts right after it.
    REQUIRE(index->SpanAtOffset(0) == nullptr);
    const std::size_t firstHeaderOffset = multibuffer.Text().find("a.cpp:10-12");
    const auto*       firstSpan         = index->SpanAtOffset(firstHeaderOffset);
    REQUIRE(firstSpan != nullptr);
    REQUIRE(firstSpan->sourcePath == "/repo/a.cpp");
    REQUIRE(firstSpan->sourceStartLine == 10);
    REQUIRE(firstSpan->sourceEndLine == 12);

    // An offset inside the first excerpt's body still resolves to the same span.
    const std::size_t bodyOffset = multibuffer.Text().find("line 11");
    const auto*       bodySpan   = index->SpanAtOffset(bodyOffset);
    REQUIRE(bodySpan == firstSpan);

    // The rule line between excerpts belongs to neither span.
    const std::size_t middleRuleOffset = multibuffer.Text().find(RuleLine(), firstHeaderOffset);
    REQUIRE(middleRuleOffset != std::string::npos);
    REQUIRE(index->SpanAtOffset(middleRuleOffset) == nullptr);

    const std::size_t secondExcerptOffset = multibuffer.Text().find("b.cpp:3-3");
    const auto*       secondSpan          = index->SpanAtOffset(secondExcerptOffset);
    REQUIRE(secondSpan != nullptr);
    REQUIRE(secondSpan->sourcePath == "/repo/b.cpp");
    REQUIRE(secondSpan->sourceStartLine == 3);

    // Past the end of the composite content resolves to nothing.
    REQUIRE(index->SpanAtOffset(multibuffer.Text().size() + 100) == nullptr);
}

TEST_CASE("BuildMultibuffer tags header and rule lines with their own LineTint", "[Multibuffer]") {
    RegistryResetGuard guard;
    BufferList         bufferList;

    std::vector<ExcerptSource> excerpts;
    excerpts.push_back(ExcerptSource{"/repo/a.cpp", 1, 1, "a.cpp:1", "context line\n"});

    Buffer& multibuffer = BuildMultibuffer(bufferList, "*test multibuffer*", excerpts);
    auto*   index       = MultibufferIndexFor(multibuffer);
    REQUIRE(index != nullptr);

    // Line 0: leading rule. Line 1: header. Line 2: body (no tint
    // requested -- ExcerptSource::lineTints was left empty). Line 3: the
    // trailing blank spacer line. Line 4: closing rule.
    REQUIRE(index->TintForLine(0) == ned::editor::multibuffer::LineTint::Rule);
    REQUIRE(index->TintForLine(1) == ned::editor::multibuffer::LineTint::Header);
    REQUIRE(index->TintForLine(2) == ned::editor::multibuffer::LineTint::None);
    REQUIRE(index->TintForLine(3) == ned::editor::multibuffer::LineTint::None);
    REQUIRE(index->TintForLine(4) == ned::editor::multibuffer::LineTint::Rule);
}

TEST_CASE("MultibufferIndexFor returns nullptr for a buffer that was never built as a multibuffer", "[Multibuffer]") {
    RegistryResetGuard guard;
    BufferList         bufferList;
    Buffer&            plain = bufferList.CreateBuffer("plain scratch");
    REQUIRE(MultibufferIndexFor(plain) == nullptr);
}

TEST_CASE("ClearMultibufferIndexFor removes a registered index and is a safe no-op otherwise", "[Multibuffer]") {
    RegistryResetGuard guard;
    BufferList         bufferList;
    Buffer&            plain = bufferList.CreateBuffer("plain scratch");
    ClearMultibufferIndexFor(plain); // never registered -- must not throw/crash

    Buffer& multibuffer = BuildMultibuffer(bufferList, "*test multibuffer*", {ExcerptSource{"/repo/a.cpp", 1, 1, "a.cpp:1", "x\n"}});
    REQUIRE(MultibufferIndexFor(multibuffer) != nullptr);

    ClearMultibufferIndexFor(multibuffer);
    REQUIRE(MultibufferIndexFor(multibuffer) == nullptr);
}

TEST_CASE("ReadExcerptText prefers a live open buffer's content over disk", "[Multibuffer]") {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "ned-multibuffer-test-read-excerpt.txt";
    {
        std::ofstream file(path);
        file << "disk line 1\ndisk line 2\ndisk line 3\n";
    }
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::filesystem::remove(path);
        }
    } cleanup{path};

    BufferList bufferList;

    // No buffer open for path yet -- falls back to disk.
    REQUIRE(ReadExcerptText(bufferList, path, 2, 2) == "disk line 2\n");
    REQUIRE(ReadExcerptText(bufferList, path, 1, 3) == "disk line 1\ndisk line 2\ndisk line 3\n");

    // Once the file is open with unsaved edits, those take precedence.
    Buffer& open = bufferList.OpenFile(path);
    open.SetPoint(0);
    open.InsertAtPoint("edited line 1\n");
    REQUIRE(ReadExcerptText(bufferList, path, 1, 1) == "edited line 1\n");
}

TEST_CASE("ReadExcerptText degrades to an empty string rather than throwing on failure", "[Multibuffer]") {
    BufferList bufferList;
    REQUIRE(ReadExcerptText(bufferList, "/nonexistent/path/that/should/not/exist.txt", 1, 5) == "");

    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned-multibuffer-test-short-file.txt";
    {
        std::ofstream file(path);
        file << "only one line\n";
    }
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::filesystem::remove(path);
        }
    } cleanup{path};

    // Requesting lines past the end of the file degrades to "", not a crash.
    REQUIRE(ReadExcerptText(bufferList, path, 5, 10) == "");
}
