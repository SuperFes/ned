#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "Editor/Multibuffer.h"
#include "Editor/MultibufferFoldSettings.h"
#include "Editor/MultibufferLimits.h"
#include "Text/BufferList.h"

using ned::editor::multibuffer::BuildMultibuffer;
using ned::editor::multibuffer::ClearMultibufferIndexFor;
using ned::editor::multibuffer::ClearRegistryForTesting;
using ned::editor::multibuffer::CommitExcerptChanges;
using ned::editor::multibuffer::CommitResult;
using ned::editor::multibuffer::ExcerptBodyRanges;
using ned::editor::multibuffer::ExcerptSource;
using ned::editor::multibuffer::ExcerptSourcePaths;
using ned::editor::multibuffer::FoldableExcerptBlocks;
using ned::editor::multibuffer::MultibufferIndexFor;
using ned::editor::multibuffer::NextExcerptBodyStart;
using ned::editor::multibuffer::PreviousExcerptBodyStart;
using ned::editor::multibuffer::ReadExcerptText;
using ned::editor::multibuffer::RevertExcerptAtOffset;
using ned::editor::multibuffer::RevertExcerptsForFileAtOffset;
using ned::text::Buffer;
using ned::text::BufferList;

namespace {

// The registry is keyed by raw Buffer* identity with no automatic per-Buffer
// cleanup (see ClearRegistryForTesting's own doc comment) -- without this, a
// Buffer destroyed at the end of one TEST_CASE can leave a stale entry that
// a later TEST_CASE's freshly allocated Buffer spuriously "inherits" if the
// allocator reuses the same address, exactly the RegistryResetGuard
// convention VcsRunnerTest.cpp/VcsProviderRegistryTest.cpp already use for
// ProviderRegistry's own global static state.
struct RegistryResetGuard {
    RegistryResetGuard() {
        ClearRegistryForTesting();
    }
    ~RegistryResetGuard() {
        ClearRegistryForTesting();
    }
};

// Auto-collapse-on-build follow-up: the three thresholds are process-wide
// state (Editor/MultibufferFoldSettings.h); every test that sets one must
// restore the real defaults for the next test, the same RAII convention
// RegistryResetGuard above already follows for its own global state.
struct FoldSettingsResetGuard {
    ~FoldSettingsResetGuard() {
        ned::editor::SetMultibufferAutoCollapseLineThreshold(40);
        ned::editor::SetMultibufferAutoCollapseByteThreshold(2000);
        ned::editor::SetMultibufferAutoCollapseExcerptCap(100);
    }
};

// Multibuffer-gaps follow-up: the excerpt cap is process-wide state too
// (Editor/MultibufferLimits.h) -- same RAII restore convention as
// FoldSettingsResetGuard above.
struct LimitsResetGuard {
    ~LimitsResetGuard() {
        ned::editor::SetMultibufferMaxExcerpts(500);
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

TEST_CASE("ReadExcerptText resolves a live huge Buffer's content via its bounded line index",
          "[Multibuffer][HugeFile]") {
    // huge-file-navigation-verification follow-up: ReadExcerptText used to
    // call buffer.Text() unconditionally on an already-open source buffer --
    // a real, unguarded full-document materialization found while verifying
    // this stage, worse for a huge buffer than anything the "already fine"
    // hypothesis assumed. Fixed to resolve via ITextStorage's own bounded
    // line index instead (see Multibuffer.cpp's ITextStorage overload of
    // LineRangeToByteRange). A line deep in a 2000-line file, on a real huge
    // (piece-table-backed) buffer via a lowered HugeFileThreshold(), is the
    // point of this test -- proves the round-trip isn't accidentally only
    // correct for content already near the start.
    struct ThresholdGuard {
        ~ThresholdGuard() {
            ned::text::SetHugeFileThreshold(1024ull * 1024 * 1024);
        }
    } guard;

    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned-multibuffer-test-huge.txt";
    std::string                 content;
    for (int i = 0; i < 2000; ++i) {
        content += "line " + std::to_string(i) + "\n";
    }
    {
        std::ofstream file(path, std::ios::binary);
        file << content;
    }
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::filesystem::remove(path);
        }
    } cleanup{path};

    BufferList bufferList;
    ned::text::SetHugeFileThreshold(4); // well under this file's real size
    Buffer& open = bufferList.OpenFile(path);
    REQUIRE(open.Content().IsHuge());

    REQUIRE(ReadExcerptText(bufferList, path, 1, 1) == "line 0\n");
    REQUIRE(ReadExcerptText(bufferList, path, 1501, 1501) == "line 1500\n");
    // Past the end degrades to "", the same contract the disk-fallback path
    // above already guarantees.
    REQUIRE(ReadExcerptText(bufferList, path, 5000, 5001) == "");
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
    REQUIRE(range.sourceStartByte == 6); // start of "line2" in "line1\nline2\nline3\n"
    REQUIRE(range.sourceEndByte == 12);  // through its own trailing newline
    REQUIRE(range.originalText == "line2\n");

    // The range covers exactly the body, excluding the header line above it.
    REQUIRE(multibuffer.Text().substr(range.start, range.end - range.start) == "line2\n");
    const std::size_t headerOffset = multibuffer.Text().find("a.cpp:2");
    REQUIRE(range.start > headerOffset);
}

TEST_CASE("BuildMultibuffer resolves a byte-exact editable ExcerptRange against a live huge source Buffer",
          "[Multibuffer][HugeFile]") {
    // huge-file-navigation-verification follow-up: the editable-excerpt
    // resolution path shares the exact same previously-unguarded
    // buffer.Text() bug ReadExcerptText's own huge-file test above found and
    // fixed -- verified separately here since it's a distinct call site
    // (BuildMultibuffer's own inline resolution, not a ReadExcerptText
    // call) with its own byte-range bookkeeping.
    RegistryResetGuard guard;
    struct ThresholdGuard {
        ~ThresholdGuard() {
            ned::text::SetHugeFileThreshold(1024ull * 1024 * 1024);
        }
    } thresholdGuard;

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "ned-multibuffer-test-huge-editable.txt";
    std::string content;
    for (int i = 0; i < 2000; ++i) {
        content += "line " + std::to_string(i) + "\n";
    }
    {
        std::ofstream file(path, std::ios::binary);
        file << content;
    }
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::filesystem::remove(path);
        }
    } cleanup{path};

    BufferList bufferList;
    ned::text::SetHugeFileThreshold(4); // well under this file's real size
    Buffer& open = bufferList.OpenFile(path);
    REQUIRE(open.Content().IsHuge());

    std::vector<ExcerptSource> excerpts;
    excerpts.push_back(ExcerptSource{path, 1501, 1501, "huge.txt:1501", "line 1500\n", {}, /*editable=*/true});

    Buffer& multibuffer = BuildMultibuffer(bufferList, "*test multibuffer huge*", excerpts);

    REQUIRE_FALSE(multibuffer.ReadOnly());
    REQUIRE(multibuffer.ExcerptRanges().size() == 1);
    const Buffer::ExcerptRange& range = multibuffer.ExcerptRanges()[0];
    REQUIRE(range.editable);
    REQUIRE(range.originalText == "line 1500\n");
    REQUIRE(open.Content().Substring(range.sourceStartByte, range.sourceEndByte - range.sourceStartByte) ==
            "line 1500\n");
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

    Buffer&                     multibuffer = BuildMultibuffer(bufferList, "*test multibuffer*", excerpts);
    const Buffer::ExcerptRange& range       = multibuffer.ExcerptRanges()[0];

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

// Auto-collapse-on-build follow-up.

TEST_CASE("BuildMultibuffer auto-collapses an excerpt whose body passes the line-count threshold", "[Multibuffer]") {
    RegistryResetGuard     guard;
    FoldSettingsResetGuard settingsGuard;
    ned::editor::SetMultibufferAutoCollapseLineThreshold(2);

    BufferList bufferList;
    Buffer&    multibuffer = BuildMultibuffer(
        bufferList, "*test multibuffer*",
        {ExcerptSource{"/repo/small.cpp", 1, 2, "small.cpp:1-2", "line1\nline2\n"},
         ExcerptSource{"/repo/big.cpp", 1, 3, "big.cpp:1-3", "line1\nline2\nline3\n"}});

    const std::string text = multibuffer.Text();
    REQUIRE_FALSE(multibuffer.FoldMarkerAt(text.find("small.cpp:1-2")).has_value());              // 2 lines, not > 2
    REQUIRE(multibuffer.FoldMarkerAt(text.find("big.cpp:1-3")) == Buffer::FoldMarker::Collapsed); // 3 lines > 2
}

TEST_CASE("BuildMultibuffer auto-collapses an excerpt whose body passes the byte-length threshold, even at one line",
          "[Multibuffer]") {
    RegistryResetGuard     guard;
    FoldSettingsResetGuard settingsGuard;
    ned::editor::SetMultibufferAutoCollapseByteThreshold(10);

    BufferList        bufferList;
    const std::string longLine    = std::string(50, 'x') + "\n"; // one line, well past the byte threshold
    Buffer&           multibuffer = BuildMultibuffer(bufferList, "*test multibuffer*",
                                                     {ExcerptSource{"/repo/min.js", 1, 1, "min.js:1", longLine}});

    REQUIRE(multibuffer.FoldMarkerAt(multibuffer.Text().find("min.js:1")) == Buffer::FoldMarker::Collapsed);
}

TEST_CASE("BuildMultibuffer auto-collapses every excerpt past the excerpt-count cap, regardless of its own size",
          "[Multibuffer]") {
    RegistryResetGuard     guard;
    FoldSettingsResetGuard settingsGuard;
    ned::editor::SetMultibufferAutoCollapseExcerptCap(1);

    BufferList bufferList;
    Buffer&    multibuffer = BuildMultibuffer(bufferList, "*test multibuffer*",
                                              {ExcerptSource{"/repo/a.cpp", 1, 1, "a.cpp:1", "x\n"},
                                               ExcerptSource{"/repo/b.cpp", 1, 1, "b.cpp:1", "x\n"}});

    const std::string text = multibuffer.Text();
    REQUIRE_FALSE(multibuffer.FoldMarkerAt(text.find("a.cpp:1")).has_value());                // 1st excerpt, at the cap
    REQUIRE(multibuffer.FoldMarkerAt(text.find("b.cpp:1")) == Buffer::FoldMarker::Collapsed); // 2nd, past it
}

TEST_CASE("An excerpt with no header line is never auto-collapsed, even past every threshold", "[Multibuffer]") {
    RegistryResetGuard     guard;
    FoldSettingsResetGuard settingsGuard;
    ned::editor::SetMultibufferAutoCollapseLineThreshold(1);

    BufferList bufferList;
    // headerText left empty -- nothing would stay visible to mark a fold if
    // this collapsed (see ExcerptSpan::bodyStartByte's own doc comment).
    Buffer& multibuffer = BuildMultibuffer(bufferList, "*test multibuffer*",
                                           {ExcerptSource{"/repo/a.cpp", 1, 3, "", "line1\nline2\nline3\n"}});

    REQUIRE(multibuffer.FoldMarkers().empty());
}

TEST_CASE("FoldableExcerptBlocks excludes an excerpt with no header line", "[Multibuffer]") {
    RegistryResetGuard guard;
    BufferList         bufferList;
    Buffer&            multibuffer = BuildMultibuffer(bufferList, "*test multibuffer*",
                                                      {ExcerptSource{"/repo/a.cpp", 1, 1, "a.cpp:1", "x\n"},
                                                       ExcerptSource{"/repo/b.cpp", 1, 1, "", "y\n"}});

    auto* index = MultibufferIndexFor(multibuffer);
    REQUIRE(index != nullptr);
    const auto blocks = FoldableExcerptBlocks(*index);
    REQUIRE(blocks.size() == 1); // only the headered excerpt offered
}

// Multibuffer-gaps follow-up.

TEST_CASE("BuildMultibuffer stitches at most MultibufferMaxExcerpts excerpts and names the rest", "[Multibuffer]") {
    RegistryResetGuard guard;
    LimitsResetGuard   limitsGuard;
    ned::editor::SetMultibufferMaxExcerpts(2);

    BufferList bufferList;
    Buffer&    multibuffer = BuildMultibuffer(bufferList, "*test multibuffer*",
                                              {ExcerptSource{"/repo/a.cpp", 1, 1, "a.cpp:1", "a\n"},
                                               ExcerptSource{"/repo/b.cpp", 1, 1, "b.cpp:1", "b\n"},
                                               ExcerptSource{"/repo/c.cpp", 1, 1, "c.cpp:1", "c\n"},
                                               ExcerptSource{"/repo/d.cpp", 1, 1, "d.cpp:1", "d\n"}});

    const std::string text = multibuffer.Text();
    REQUIRE(text.find("a.cpp:1") != std::string::npos);
    REQUIRE(text.find("b.cpp:1") != std::string::npos);
    REQUIRE(text.find("c.cpp:1") == std::string::npos);
    REQUIRE(text.find("d.cpp:1") == std::string::npos);
    REQUIRE(text.find("… 2 more not shown") != std::string::npos);

    // The note line is outside every span, exactly like a rule line -- so
    // Enter/click on it can't be mistaken for a real excerpt.
    const auto* index = MultibufferIndexFor(multibuffer);
    REQUIRE(index != nullptr);
    REQUIRE(index->Spans().size() == 2);
    REQUIRE(index->SpanAtOffset(text.find("… 2 more not shown")) == nullptr);
}

TEST_CASE("A caller that capped its own excerpt-building reports the real total via totalAvailable", "[Multibuffer]") {
    RegistryResetGuard guard;
    LimitsResetGuard   limitsGuard;
    ned::editor::SetMultibufferMaxExcerpts(10); // well above what's handed over

    BufferList bufferList;
    // The find-references shape: 200 matches, only 2 excerpts ever built.
    Buffer& multibuffer = BuildMultibuffer(bufferList, "*test multibuffer*",
                                           {ExcerptSource{"/repo/a.cpp", 1, 1, "a.cpp:1", "a\n"},
                                            ExcerptSource{"/repo/b.cpp", 1, 1, "b.cpp:1", "b\n"}},
                                           /*totalAvailable=*/200);

    REQUIRE(multibuffer.Text().find("… 198 more not shown") != std::string::npos);
}

TEST_CASE("A totalAvailable smaller than the excerpts handed over never understates what was dropped",
          "[Multibuffer]") {
    RegistryResetGuard guard;
    LimitsResetGuard   limitsGuard;
    ned::editor::SetMultibufferMaxExcerpts(1);

    BufferList bufferList;
    Buffer&    multibuffer = BuildMultibuffer(bufferList, "*test multibuffer*",
                                              {ExcerptSource{"/repo/a.cpp", 1, 1, "a.cpp:1", "a\n"},
                                               ExcerptSource{"/repo/b.cpp", 1, 1, "b.cpp:1", "b\n"},
                                               ExcerptSource{"/repo/c.cpp", 1, 1, "c.cpp:1", "c\n"}},
                                              /*totalAvailable=*/1);

    REQUIRE(multibuffer.Text().find("… 2 more not shown") != std::string::npos);
}

TEST_CASE("A zero cap means unlimited, and an uncapped build carries no note line", "[Multibuffer]") {
    RegistryResetGuard guard;
    LimitsResetGuard   limitsGuard;
    ned::editor::SetMultibufferMaxExcerpts(0);

    BufferList bufferList;
    Buffer&    multibuffer = BuildMultibuffer(bufferList, "*test multibuffer*",
                                              {ExcerptSource{"/repo/a.cpp", 1, 1, "a.cpp:1", "a\n"},
                                               ExcerptSource{"/repo/b.cpp", 1, 1, "b.cpp:1", "b\n"},
                                               ExcerptSource{"/repo/c.cpp", 1, 1, "c.cpp:1", "c\n"}});

    const std::string text = multibuffer.Text();
    REQUIRE(text.find("c.cpp:1") != std::string::npos);
    REQUIRE(text.find("more not shown") == std::string::npos);
}

// project-replace-review follow-up: CommitTarget::Disk -- the sed-flavored
// half of the one replace path, folded in here rather than kept as a second
// endpoint. The file-preservation coverage below moved over from
// ProjectReplaceTest.cpp's ReplaceMatches tests along with it.

TEST_CASE("A disk-target commit rewrites the source file and opens no buffer for it", "[Multibuffer]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_multibuffer_commit_disk";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    const std::filesystem::path file = dir / "a.txt";
    {
        std::ofstream(file) << "one cat here\ntwo\n";
    }

    RegistryResetGuard guard;
    BufferList         bufferList;
    Buffer&            composite = BuildMultibuffer(bufferList, "*review*",
                                                    {ExcerptSource{file, 1, 1, "a.txt:1", "one cat here\n", {}, /*editable=*/true}});

    const std::size_t bodyStart = composite.Text().find("one cat here");
    composite.DeleteRange(bodyStart, std::string("one cat here").size());
    composite.InsertAt(bodyStart, "one dog here");

    const CommitResult result = CommitExcerptChanges(bufferList, composite, /*projectUndo=*/nullptr, /*onlyPath=*/nullptr,
                                                     ned::editor::multibuffer::CommitTarget::Disk);
    REQUIRE(result.committedExcerpts == 1);
    REQUIRE(result.filesWritten == 1);
    REQUIRE(result.buffersCommitted == 0);
    REQUIRE(bufferList.FindByPath(file) == nullptr); // no buffer opened for it

    std::ifstream     check(file);
    const std::string written((std::istreambuf_iterator<char>(check)), std::istreambuf_iterator<char>());
    REQUIRE(written == "one dog here\ntwo\n");

    std::filesystem::remove_all(dir);
}

TEST_CASE("A disk-target commit preserves the rewritten file's permissions and hard links", "[Multibuffer]") {
    // file-attribute-preservation follow-up: this writes the user's own files
    // with the temp-then-rename pattern Buffer::SaveToFile uses, and needs the
    // same care -- see Text/FilePreservation.h. Moved here from
    // ProjectReplaceTest.cpp when ReplaceMatches was folded into this path.
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_multibuffer_commit_disk_preserve";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);

    const std::filesystem::path script = dir / "run.sh";
    {
        std::ofstream(script) << "echo cat\n";
    }
    std::filesystem::permissions(script, std::filesystem::perms::owner_all);

    const std::filesystem::path linked = dir / "linked.txt";
    const std::filesystem::path alias  = dir / "alias.txt";
    {
        std::ofstream(linked) << "a cat here\n";
    }
    std::filesystem::create_hard_link(linked, alias);

    RegistryResetGuard guard;
    BufferList         bufferList;
    Buffer&            composite =
        BuildMultibuffer(bufferList, "*review*",
                         {ExcerptSource{script, 1, 1, "run.sh:1", "echo cat\n", {}, /*editable=*/true},
                          ExcerptSource{linked, 1, 1, "linked.txt:1", "a cat here\n", {}, /*editable=*/true}});

    for (const char* before : {"echo cat", "a cat here"}) {
        const std::string original(before);
        const std::string replaced = original.substr(0, original.find("cat")) + "dog" +
                                     original.substr(original.find("cat") + 3);
        const std::size_t start    = composite.Text().find(original);
        REQUIRE(start != std::string::npos);
        composite.DeleteRange(start, original.size());
        composite.InsertAt(start, replaced);
    }

    const CommitResult result = CommitExcerptChanges(bufferList, composite, nullptr, nullptr,
                                                     ned::editor::multibuffer::CommitTarget::Disk);
    REQUIRE(result.filesWritten == 2);

    const auto read = [](const std::filesystem::path& p) {
        std::ifstream f(p, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    };
    REQUIRE(read(script) == "echo dog\n");
    REQUIRE((std::filesystem::status(script).permissions() & std::filesystem::perms::owner_exec) !=
            std::filesystem::perms::none);
    REQUIRE(read(linked) == "a dog here\n");
    REQUIRE(std::filesystem::equivalent(linked, alias)); // hard link survived the rewrite
    REQUIRE(read(alias) == "a dog here\n");

    std::filesystem::remove_all(dir);
}

TEST_CASE("A disk-target commit applies into a modified open buffer instead of writing behind it", "[Multibuffer]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_multibuffer_commit_disk_modified";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    const std::filesystem::path file = dir / "a.txt";
    {
        std::ofstream(file) << "one cat here\n";
    }

    RegistryResetGuard guard;
    BufferList         bufferList;
    Buffer&            open = bufferList.OpenOrCreateFile(file);
    open.SetPoint(open.Content().ByteLength());
    open.InsertAtPoint("a second, unsaved line\n"); // the reason writing the file would be wrong

    Buffer&           composite = BuildMultibuffer(bufferList, "*review*",
                                                   {ExcerptSource{file, 1, 1, "a.txt:1", "one cat here\n", {}, /*editable=*/true}});
    const std::size_t bodyStart = composite.Text().find("one cat here");
    composite.DeleteRange(bodyStart, std::string("one cat here").size());
    composite.InsertAt(bodyStart, "one dog here");

    const CommitResult result = CommitExcerptChanges(bufferList, composite, nullptr, nullptr,
                                                     ned::editor::multibuffer::CommitTarget::Disk);
    REQUIRE(result.buffersCommitted == 1);
    REQUIRE(result.filesWritten == 0);
    REQUIRE(open.Text() == "one dog here\na second, unsaved line\n"); // both survive

    std::ifstream     check(file);
    const std::string onDisk((std::istreambuf_iterator<char>(check)), std::istreambuf_iterator<char>());
    REQUIRE(onDisk == "one cat here\n"); // untouched -- the unsaved line was never at risk

    std::filesystem::remove_all(dir);
}

// project-replace-review follow-up: per-excerpt / per-file revert and the
// file-scoped commit.

TEST_CASE("RevertExcerptAtOffset puts one excerpt back and leaves its siblings alone", "[Multibuffer]") {
    RegistryResetGuard guard;
    BufferList         bufferList;
    Buffer&            source = bufferList.CreateBuffer("a.cpp");
    source.SetPath("/repo/a.cpp");
    source.InsertAtPoint("one\ntwo\n");

    Buffer& composite = BuildMultibuffer(bufferList, "*review*",
                                         {ExcerptSource{"/repo/a.cpp", 1, 1, "a.cpp:1", "one\n", {}, /*editable=*/true},
                                          ExcerptSource{"/repo/a.cpp", 2, 2, "a.cpp:2", "two\n", {}, /*editable=*/true}});

    const std::size_t first = composite.Text().find("one\n");
    composite.DeleteRange(first, 3);
    composite.InsertAt(first, "ONE");
    const std::size_t second = composite.Text().find("two\n");
    composite.DeleteRange(second, 3);
    composite.InsertAt(second, "TWO");

    REQUIRE(RevertExcerptAtOffset(composite, composite.Text().find("ONE")));
    REQUIRE(composite.Text().find("one\n") != std::string::npos);
    REQUIRE(composite.Text().find("TWO") != std::string::npos); // untouched

    // Already back to its original -- nothing left to do.
    REQUIRE_FALSE(RevertExcerptAtOffset(composite, composite.Text().find("one\n")));
}

TEST_CASE("RevertExcerptsForFileAtOffset reverts every excerpt of that file in one step", "[Multibuffer]") {
    RegistryResetGuard guard;
    BufferList         bufferList;
    Buffer&            a = bufferList.CreateBuffer("a.cpp");
    a.SetPath("/repo/a.cpp");
    a.InsertAtPoint("one\ntwo\n");
    Buffer& b = bufferList.CreateBuffer("b.cpp");
    b.SetPath("/repo/b.cpp");
    b.InsertAtPoint("three\n");

    Buffer& composite = BuildMultibuffer(bufferList, "*review*",
                                         {ExcerptSource{"/repo/a.cpp", 1, 1, "a.cpp:1", "one\n", {}, /*editable=*/true},
                                          ExcerptSource{"/repo/a.cpp", 2, 2, "a.cpp:2", "two\n", {}, /*editable=*/true},
                                          ExcerptSource{"/repo/b.cpp", 1, 1, "b.cpp:1", "three\n", {}, /*editable=*/true}});

    for (const char* word : {"one", "two", "three"}) {
        const std::size_t at = composite.Text().find(std::string(word) + "\n");
        composite.DeleteRange(at, std::string(word).size());
        composite.InsertAt(at, "X");
    }

    const std::size_t undoDepthBefore = composite.CurrentUndoSequence();
    REQUIRE(RevertExcerptsForFileAtOffset(composite, composite.Text().find("X")) == 2); // a.cpp's two, not b.cpp's
    REQUIRE(composite.Text().find("one\n") != std::string::npos);
    REQUIRE(composite.Text().find("two\n") != std::string::npos);
    REQUIRE(composite.Text().find("X\n") != std::string::npos); // b.cpp's excerpt still edited
    REQUIRE(composite.CurrentUndoSequence() != undoDepthBefore);

    composite.Undo(); // one step puts both back
    REQUIRE(composite.Text().find("one\n") == std::string::npos);
}

TEST_CASE("A file-scoped commit leaves every other file's excerpts pending", "[Multibuffer]") {
    RegistryResetGuard guard;
    BufferList         bufferList;
    Buffer&            a = bufferList.CreateBuffer("a.cpp");
    a.SetPath("/repo/a.cpp");
    a.InsertAtPoint("one\n");
    Buffer& b = bufferList.CreateBuffer("b.cpp");
    b.SetPath("/repo/b.cpp");
    b.InsertAtPoint("two\n");

    Buffer& composite = BuildMultibuffer(bufferList, "*review*",
                                         {ExcerptSource{"/repo/a.cpp", 1, 1, "a.cpp:1", "one\n", {}, /*editable=*/true},
                                          ExcerptSource{"/repo/b.cpp", 1, 1, "b.cpp:1", "two\n", {}, /*editable=*/true}});

    for (const char* word : {"one", "two"}) {
        const std::size_t at = composite.Text().find(std::string(word) + "\n");
        composite.DeleteRange(at, std::string(word).size());
        composite.InsertAt(at, "EDITED");
    }

    const std::filesystem::path onlyA{"/repo/a.cpp"};
    const CommitResult          result = CommitExcerptChanges(bufferList, composite, nullptr, &onlyA);
    REQUIRE(result.committedExcerpts == 1);
    REQUIRE(a.Text() == "EDITED\n");
    REQUIRE(b.Text() == "two\n"); // left pending, not skipped-with-a-reason
    REQUIRE(result.skipped.empty());
}

TEST_CASE("NextExcerptBodyStart / PreviousExcerptBodyStart step between excerpt bodies", "[Multibuffer]") {
    RegistryResetGuard guard;
    BufferList         bufferList;
    Buffer&            composite = BuildMultibuffer(bufferList, "*review*",
                                                    {ExcerptSource{"/repo/a.cpp", 1, 1, "a.cpp:1", "one\n"},
                                                     ExcerptSource{"/repo/b.cpp", 1, 1, "b.cpp:1", "two\n"}});

    const std::size_t firstBody  = composite.Text().find("one\n");
    const std::size_t secondBody = composite.Text().find("two\n");

    REQUIRE(NextExcerptBodyStart(composite, 0) == firstBody);
    REQUIRE(NextExcerptBodyStart(composite, firstBody) == secondBody);
    REQUIRE_FALSE(NextExcerptBodyStart(composite, secondBody).has_value());

    REQUIRE(PreviousExcerptBodyStart(composite, secondBody) == firstBody);
    REQUIRE_FALSE(PreviousExcerptBodyStart(composite, firstBody).has_value());
}

TEST_CASE("ExcerptBodyRanges covers bodies only, never an excerpt's header or the rules between them", "[Multibuffer]") {
    RegistryResetGuard guard;
    BufferList         bufferList;
    Buffer&            composite = BuildMultibuffer(bufferList, "*review*",
                                                    {ExcerptSource{"/repo/a.cpp", 1, 1, "a.cpp:1", "one\n"},
                                                     ExcerptSource{"/repo/b.cpp", 1, 1, "b.cpp:1", "two\n"}});

    const std::string                                      text   = composite.Text();
    const std::vector<std::pair<std::size_t, std::size_t>> ranges = ExcerptBodyRanges(composite);
    REQUIRE(ranges.size() == 2);

    // Every body byte is in scope; the header line naming the file is not --
    // which is the whole point (searching for "a.cpp" must not stop on it).
    REQUIRE(ranges[0].first == text.find("one\n"));
    REQUIRE(text.find("a.cpp:1") < ranges[0].first);
    REQUIRE(ranges[1].first == text.find("two\n"));
    REQUIRE(ranges[0].second <= ranges[1].first);
    REQUIRE(std::is_sorted(ranges.begin(), ranges.end()));
}

TEST_CASE("ExcerptBodyRanges follows the relocated ExcerptRanges once a buffer is editable", "[Multibuffer]") {
    RegistryResetGuard guard;
    BufferList         bufferList;
    Buffer&            a = bufferList.CreateBuffer("a.cpp");
    a.SetPath("/repo/a.cpp");
    a.InsertAtPoint("one\n");

    Buffer& composite = BuildMultibuffer(bufferList, "*review*",
                                         {ExcerptSource{"/repo/a.cpp", 1, 1, "a.cpp:1", "one\n", {}, /*editable=*/true},
                                          ExcerptSource{"/repo/a.cpp", 2, 2, "a.cpp:2", "two\n", {}, /*editable=*/true}});

    const std::size_t firstBody = composite.Text().find("one\n");
    composite.InsertAt(firstBody, "XXXX"); // grows the first body, shifting the second

    const std::vector<std::pair<std::size_t, std::size_t>> ranges = ExcerptBodyRanges(composite);
    REQUIRE(ranges.size() == 2);
    REQUIRE(ranges[0].first == firstBody);
    REQUIRE(ranges[0].second == firstBody + std::string("XXXXone\n").size());
    REQUIRE(ranges[1].first == composite.Text().find("two\n"));
}

TEST_CASE("ExcerptSourcePaths reports each source once, in first-appearance order", "[Multibuffer]") {
    RegistryResetGuard guard;
    BufferList         bufferList;
    Buffer&            composite = BuildMultibuffer(bufferList, "*review*",
                                                    {ExcerptSource{"/repo/b.cpp", 1, 1, "b.cpp:1", "one\n"},
                                                     ExcerptSource{"/repo/a.cpp", 1, 1, "a.cpp:1", "two\n"},
                                                     ExcerptSource{"/repo/b.cpp", 9, 9, "b.cpp:9", "three\n"}});

    const std::vector<std::filesystem::path> paths = ExcerptSourcePaths(composite);
    REQUIRE(paths.size() == 2);
    REQUIRE(paths[0] == std::filesystem::path("/repo/b.cpp"));
    REQUIRE(paths[1] == std::filesystem::path("/repo/a.cpp"));
}

TEST_CASE("ExcerptBodyRanges and ExcerptSourcePaths are empty for an ordinary buffer", "[Multibuffer]") {
    RegistryResetGuard guard;
    BufferList         bufferList;
    Buffer&            plain = bufferList.CreateBuffer("plain.txt");
    plain.InsertAtPoint("nothing to see\n");

    REQUIRE(ExcerptBodyRanges(plain).empty());
    REQUIRE(ExcerptSourcePaths(plain).empty());
}
