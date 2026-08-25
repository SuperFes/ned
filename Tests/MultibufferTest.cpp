#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "Editor/Multibuffer.h"
#include "Text/BufferList.h"

using ned::editor::multibuffer::BuildMultibuffer;
using ned::editor::multibuffer::ClearMultibufferIndexFor;
using ned::editor::multibuffer::ClearRegistryForTesting;
using ned::editor::multibuffer::CommitExcerptChanges;
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

// --- Editable excerpts (editable-multibuffer follow-up) --------------------

namespace {

struct TempFile {
    std::filesystem::path path;
    explicit TempFile(const std::string& name, const std::string& content) :
        path(std::filesystem::temp_directory_path() / name) {
        std::ofstream file(path);
        file << content;
    }
    ~TempFile() {
        std::filesystem::remove(path);
    }
};

} // namespace

TEST_CASE("BuildMultibuffer resolves a byte-exact ExcerptRange for an editable excerpt", "[Multibuffer]") {
    RegistryResetGuard guard;
    BufferList         bufferList;
    TempFile           source("ned-multibuffer-test-editable.txt", "line1\nline2\nline3\n");

    std::vector<ExcerptSource> excerpts;
    excerpts.push_back(ExcerptSource{source.path, 2, 2, "a.cpp:2", "line2\n", {}, /*editable=*/true});

    Buffer& multibuffer = BuildMultibuffer(bufferList, "*test multibuffer*", excerpts);

    REQUIRE_FALSE(multibuffer.ReadOnly());
    REQUIRE(multibuffer.ExcerptRanges().size() == 1);
    const Buffer::ExcerptRange& range = multibuffer.ExcerptRanges()[0];
    REQUIRE(range.editable);
    REQUIRE(range.sourcePath == source.path);
    REQUIRE(range.sourceStartByte == 6);  // start of "line2" in "line1\nline2\nline3\n"
    REQUIRE(range.sourceEndByte == 12);   // through its own trailing newline
    REQUIRE(range.originalText == "line2\n");

    // The range covers exactly the body, excluding the header line above it.
    REQUIRE(multibuffer.Text().substr(range.start, range.end - range.start) == "line2\n");
    const std::size_t headerOffset = multibuffer.Text().find("a.cpp:2");
    REQUIRE(range.start > headerOffset);
}

TEST_CASE("An editable excerpt with sourceStartLine == 0 is not made editable", "[Multibuffer]") {
    RegistryResetGuard guard;
    BufferList         bufferList;

    std::vector<ExcerptSource> excerpts;
    excerpts.push_back(ExcerptSource{"/repo/a.cpp", 0, 0, "a.cpp", "no single source line\n", {}, /*editable=*/true});

    Buffer& multibuffer = BuildMultibuffer(bufferList, "*test multibuffer*", excerpts);

    REQUIRE(multibuffer.ExcerptRanges().empty());
    REQUIRE(multibuffer.ReadOnly()); // no editable excerpt actually resolved -- stays read-only
}

TEST_CASE("An editable excerpt whose source line can't be resolved degrades to non-editable, not a crash", "[Multibuffer]") {
    RegistryResetGuard guard;
    BufferList         bufferList;

    std::vector<ExcerptSource> excerpts;
    excerpts.push_back(
        ExcerptSource{"/nonexistent/path.cpp", 5, 5, "path.cpp:5", "stale line\n", {}, /*editable=*/true});

    Buffer& multibuffer = BuildMultibuffer(bufferList, "*test multibuffer*", excerpts);

    REQUIRE(multibuffer.ExcerptRanges().empty());
    REQUIRE(multibuffer.ReadOnly());
}

TEST_CASE("BuildMultibuffer with no editable excerpts leaves the composite buffer read-only, unchanged", "[Multibuffer]") {
    RegistryResetGuard guard;
    BufferList         bufferList;

    std::vector<ExcerptSource> excerpts;
    excerpts.push_back(ExcerptSource{"/repo/a.cpp", 10, 12, "a.cpp:10-12", "line 10\nline 11\nline 12\n"});

    Buffer& multibuffer = BuildMultibuffer(bufferList, "*test multibuffer*", excerpts);

    REQUIRE(multibuffer.ReadOnly());
    REQUIRE(multibuffer.ExcerptRanges().empty());
}

TEST_CASE("A composite buffer with an editable excerpt accepts typing in the body and rejects it in the chrome",
         "[Multibuffer]") {
    RegistryResetGuard guard;
    BufferList         bufferList;
    TempFile           source("ned-multibuffer-test-editable-typing.txt", "line1\nline2\nline3\n");

    std::vector<ExcerptSource> excerpts;
    excerpts.push_back(ExcerptSource{source.path, 2, 2, "a.cpp:2", "line2\n", {}, /*editable=*/true});

    Buffer& multibuffer = BuildMultibuffer(bufferList, "*test multibuffer*", excerpts);
    const Buffer::ExcerptRange& range = multibuffer.ExcerptRanges()[0];

    multibuffer.SetPoint(range.start);
    multibuffer.InsertAtPoint("X");
    REQUIRE(multibuffer.Text().substr(range.start, 7) == "Xline2\n");

    multibuffer.SetPoint(0); // the leading rule line -- protected chrome
    multibuffer.InsertAtPoint("Y");
    REQUIRE(multibuffer.Text().substr(0, 1) != "Y");
}

// --- CommitExcerptChanges (editable-multibuffer follow-up) -----------------

namespace {

// Same "bump the mtime explicitly" convention AutoRevertTest.cpp's own
// ExternalWrite uses, so this test never depends on filesystem mtime
// granularity being finer than the test's own runtime.
void ExternalWrite(const std::filesystem::path& path, const std::string& content) {
    std::ofstream(path, std::ios::trunc) << content;
    std::filesystem::last_write_time(path, std::filesystem::last_write_time(path) + std::chrono::seconds(2));
}

// Replaces one excerpt's whole body with newBody, driven purely through
// ordinary Buffer edits on the composite (exercising Phase 1/2's relocation
// and enforcement together, not a backdoor).
void EditExcerptBody(Buffer& multibuffer, std::size_t rangeIndex, const std::string& newBody) {
    const Buffer::ExcerptRange range = multibuffer.ExcerptRanges()[rangeIndex]; // copy -- about to relocate
    multibuffer.DeleteRange(range.start, range.end - range.start);
    multibuffer.SetPoint(range.start);
    multibuffer.InsertAtPoint(newBody);
}

} // namespace

TEST_CASE("CommitExcerptChanges writes an edited excerpt's new text back to its live source Buffer", "[Multibuffer]") {
    RegistryResetGuard guard;
    BufferList         bufferList;
    TempFile           source("ned-multibuffer-test-commit.txt", "line1\nline2\nline3\n");

    Buffer& multibuffer = BuildMultibuffer(
        bufferList, "*test multibuffer*",
        {ExcerptSource{source.path, 2, 2, "a.cpp:2", "line2\n", {}, /*editable=*/true}});

    EditExcerptBody(multibuffer, 0, "changed\n");

    const auto result = CommitExcerptChanges(bufferList, multibuffer);
    REQUIRE(result.committedExcerpts == 1);
    REQUIRE(result.skipped.empty());

    Buffer* committed = bufferList.FindByPath(source.path);
    REQUIRE(committed != nullptr);
    REQUIRE(committed->Text() == "line1\nchanged\nline3\n");
}

TEST_CASE("An unedited excerpt is left untouched by commit", "[Multibuffer]") {
    RegistryResetGuard guard;
    BufferList         bufferList;
    TempFile           editedSource("ned-multibuffer-test-commit-edited.txt", "edit me\n");
    TempFile           untouchedSource("ned-multibuffer-test-commit-untouched.txt", "leave me\n");

    Buffer& multibuffer = BuildMultibuffer(
        bufferList, "*test multibuffer*",
        {ExcerptSource{editedSource.path, 1, 1, "a.cpp:1", "edit me\n", {}, /*editable=*/true},
         ExcerptSource{untouchedSource.path, 1, 1, "b.cpp:1", "leave me\n", {}, /*editable=*/true}});

    EditExcerptBody(multibuffer, 0, "edited\n");

    const auto result = CommitExcerptChanges(bufferList, multibuffer);
    REQUIRE(result.committedExcerpts == 1);
    REQUIRE(result.skipped.empty());

    // The untouched excerpt's source was never even opened -- no write, no I/O.
    REQUIRE(bufferList.FindByPath(untouchedSource.path) == nullptr);
}

TEST_CASE("CommitExcerptChanges applies multiple changed excerpts in one file in descending order", "[Multibuffer]") {
    RegistryResetGuard guard;
    BufferList         bufferList;
    TempFile           source("ned-multibuffer-test-commit-multi.txt", "aaa\nbbb\nccc\nddd\neee\n");

    Buffer& multibuffer = BuildMultibuffer(
        bufferList, "*test multibuffer*",
        {ExcerptSource{source.path, 2, 2, "a.cpp:2", "bbb\n", {}, /*editable=*/true},
         ExcerptSource{source.path, 4, 4, "a.cpp:4", "ddd\n", {}, /*editable=*/true}});

    // Shorter and longer replacements, in an order that would corrupt the
    // second edit's stored source offset if commit applied them ascending
    // instead of descending.
    EditExcerptBody(multibuffer, 0, "BB\n");
    EditExcerptBody(multibuffer, 1, "DDDDDD\n");

    const auto result = CommitExcerptChanges(bufferList, multibuffer);
    REQUIRE(result.committedExcerpts == 2);
    REQUIRE(result.skipped.empty());

    Buffer* committed = bufferList.FindByPath(source.path);
    REQUIRE(committed != nullptr);
    REQUIRE(committed->Text() == "aaa\nBB\nccc\nDDDDDD\neee\n");
}

TEST_CASE("CommitExcerptChanges wraps one source file's writes in a single undo group", "[Multibuffer]") {
    RegistryResetGuard guard;
    BufferList         bufferList;
    TempFile           source("ned-multibuffer-test-commit-undo.txt", "aaa\nbbb\nccc\nddd\neee\n");

    Buffer& multibuffer = BuildMultibuffer(
        bufferList, "*test multibuffer*",
        {ExcerptSource{source.path, 2, 2, "a.cpp:2", "bbb\n", {}, /*editable=*/true},
         ExcerptSource{source.path, 4, 4, "a.cpp:4", "ddd\n", {}, /*editable=*/true}});

    EditExcerptBody(multibuffer, 0, "BB\n");
    EditExcerptBody(multibuffer, 1, "DDDDDD\n");
    CommitExcerptChanges(bufferList, multibuffer);

    Buffer* committed = bufferList.FindByPath(source.path);
    REQUIRE(committed != nullptr);
    REQUIRE(committed->Text() == "aaa\nBB\nccc\nDDDDDD\neee\n");

    REQUIRE(committed->CanUndo());
    committed->Undo();
    REQUIRE(committed->Text() == "aaa\nbbb\nccc\nddd\neee\n"); // both edits reverted by one Undo()
}

TEST_CASE("CommitExcerptChanges skips a file changed on disk since build, and still commits the rest", "[Multibuffer]") {
    RegistryResetGuard guard;
    BufferList         bufferList;
    TempFile           conflicted("ned-multibuffer-test-commit-conflict.txt", "line1\nline2\nline3\n");
    TempFile           clean("ned-multibuffer-test-commit-clean.txt", "foo1\nfoo2\nfoo3\n");

    // Both sources must already be open *before* the external write below,
    // so their DiskTimestamp_ reflects the pre-conflict stat -- an
    // OpenOrCreateFile that opens a file for the first time at commit time
    // would stamp DiskTimestamp_ from whatever's on disk *then*, seeing no
    // conflict at all (a real gap this test exists to pin down).
    Buffer& conflictedBuffer = bufferList.OpenFile(conflicted.path);
    bufferList.OpenFile(clean.path);

    Buffer& multibuffer = BuildMultibuffer(
        bufferList, "*test multibuffer*",
        {ExcerptSource{conflicted.path, 2, 2, "a.cpp:2", "line2\n", {}, /*editable=*/true},
         ExcerptSource{clean.path, 2, 2, "b.cpp:2", "foo2\n", {}, /*editable=*/true}});

    ExternalWrite(conflicted.path, "line1\nEXTERNALLY CHANGED\nline3\n");

    EditExcerptBody(multibuffer, 0, "my edit\n");
    EditExcerptBody(multibuffer, 1, "my other edit\n");

    const auto result = CommitExcerptChanges(bufferList, multibuffer);
    REQUIRE(result.committedExcerpts == 1);
    REQUIRE(result.skipped.size() == 1);
    REQUIRE(result.skipped[0].first == conflicted.path);

    // The stale, already-open conflicted buffer is untouched by the skip --
    // no silent overwrite of a buffer that no longer matches disk.
    REQUIRE(conflictedBuffer.Text() == "line1\nline2\nline3\n");

    Buffer* cleanBuffer = bufferList.FindByPath(clean.path);
    REQUIRE(cleanBuffer != nullptr);
    REQUIRE(cleanBuffer->Text() == "foo1\nmy other edit\nfoo3\n");
}
