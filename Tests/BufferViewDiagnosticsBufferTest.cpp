#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include "Editor/Commands.h"
#include "Editor/Dispatcher.h"
#include "Editor/Keymap.h"
#include "Editor/Mode.h"
#include "Editor/Lsp/Manager.h"
#include "Editor/Lsp/ServerConfig.h"
#include "Editor/Multibuffer.h"
#include "Editor/MultibufferLimits.h"
#include "Editor/PromptHistory.h"
#include "Editor/Register.h"
#include "TestEvents.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"
#include "UI/ActiveBuffer.h"
#include "UI/BufferView.h"
#include "UI/EventLoop.h"
#include "UI/Theme.h"

using ned::editor::multibuffer::ClearRegistryForTesting;
using ned::editor::multibuffer::MultibufferIndexFor;
using ned::text::Buffer;
using ned::ui::BufferView;

namespace {

// Mirrors MultibufferTest.cpp's own RegistryResetGuard -- without this, a
// Buffer destroyed at the end of one TEST_CASE can leave a stale registry
// entry a later TEST_CASE's freshly allocated Buffer spuriously "inherits"
// if the allocator reuses the same address (confirmed live: made this
// file's own diagnostics-multibuffer tests order-dependent).
struct RegistryResetGuard {
    RegistryResetGuard() {
        ClearRegistryForTesting();
    }
    ~RegistryResetGuard() {
        ClearRegistryForTesting();
    }
};

// Mirrors BufferViewDiffGutterTest.cpp's own Fixture exactly.
struct Fixture {
    RegistryResetGuard         registryResetGuard;
    ned::text::Buffer          buffer{"scratch"};
    ned::text::KillRing        killRing;
    ned::editor::RegisterTable registers;
    ned::editor::PromptHistory promptHistory;
    ned::text::BufferList      bufferList;

    ned::editor::CommandRegistry registry{[] {
        ned::editor::CommandRegistry r;
        ned::editor::RegisterBuiltinCommands(r);
        return r;
    }()};
    ned::editor::Keymap          keymap = ned::editor::BuildDefaultGlobalKeymap();
    ned::editor::Dispatcher      dispatcher{registry, ned::editor::KeymapStack({&keymap})};
    ned::editor::Mode            mode  = ned::editor::FundamentalMode();
    ned::ui::Theme               theme = ned::ui::DarkTheme();

    std::string           statusMessage;
    ned::ui::ActiveBuffer activeBuffer{buffer};

    BufferView View() {
        return BufferView(activeBuffer, killRing, registers, promptHistory, bufferList, dispatcher, statusMessage,
                          mode, theme);
    }
};

} // namespace

TEST_CASE("RequestDiagnosticsBuffer stitches every open buffer's Code diagnostics into one *diagnostics* buffer",
          "[BufferView][Diagnostics]") {
    Fixture fixture;

    Buffer& a = fixture.bufferList.CreateBuffer("a.cpp");
    a.SetPath("/repo/a.cpp");
    a.InsertAtPoint("int main() {\n    return bogus;\n}\n");
    const std::size_t bogusStart = a.Text().find("bogus");
    a.SetDiagnostics({
        Buffer::Diagnostic{.startByte = bogusStart,
                           .endByte   = bogusStart + 5,
                           .severity  = Buffer::Diagnostic::Severity::Error,
                           .message   = "use of undeclared identifier 'bogus'"},
    });

    Buffer& b = fixture.bufferList.CreateBuffer("b.cpp");
    b.SetPath("/repo/b.cpp");
    b.InsertAtPoint("int x;\n");
    b.SetDiagnostics({
        Buffer::Diagnostic{
            .startByte = 4, .endByte = 5, .severity = Buffer::Diagnostic::Severity::Warning, .message = "unused variable 'x'"},
    });

    BufferView view = fixture.View();
    view.RequestDiagnosticsBufferForTesting();

    Buffer* results = fixture.bufferList.Find("*diagnostics*");
    REQUIRE(results != nullptr);
    // Editable-multibuffer follow-up: diagnostics excerpts are editable, so
    // the composite is no longer whole-buffer read-only -- chrome (headers/
    // rules) stays protected via ExcerptRanges()' own point-level
    // enforcement instead.
    REQUIRE_FALSE(results->ReadOnly());
    REQUIRE(results->ExcerptRanges().size() == 2);
    REQUIRE(results->ExcerptRanges()[0].editable);
    REQUIRE(results->ExcerptRanges()[1].editable);

    auto* index = MultibufferIndexFor(*results);
    REQUIRE(index != nullptr);
    REQUIRE(index->Spans().size() == 2);

    // Grouped per file, path-sorted -- a.cpp's own entry precedes b.cpp's.
    REQUIRE(results->Text().find("a.cpp:2") < results->Text().find("b.cpp:1"));

    // The composite buffer carries its own real Diagnostic entries rather
    // than a bespoke LineTint -- the whole point being that the ordinary
    // gutter/underline/severity-color pipeline lights up unmodified.
    REQUIRE(results->Diagnostics().size() == 2);
    const Buffer::Diagnostic& first = results->Diagnostics()[0];
    REQUIRE(first.severity == Buffer::Diagnostic::Severity::Error);
    REQUIRE(first.message == "use of undeclared identifier 'bogus'");

    // startByte/endByte were translated to land on the real "bogus" token
    // inside the composite buffer's own copy of that source line.
    const std::string underlined = results->Content().Substring(first.startByte, first.endByte - first.startByte);
    REQUIRE(underlined == "bogus");

    const Buffer::Diagnostic& second      = results->Diagnostics()[1];
    const std::string         underlinedX = results->Content().Substring(second.startByte, second.endByte - second.startByte);
    REQUIRE(underlinedX == "x");
}

TEST_CASE("RequestDiagnosticsBuffer ignores prose-origin diagnostics and buffers with none",
          "[BufferView][Diagnostics]") {
    Fixture fixture;

    Buffer& clean = fixture.bufferList.CreateBuffer("clean.cpp");
    clean.SetPath("/repo/clean.cpp");
    clean.InsertAtPoint("int x;\n");

    Buffer& prose = fixture.bufferList.CreateBuffer("notes.org");
    prose.SetPath("/repo/notes.org");
    prose.InsertAtPoint("teh quick fox\n");
    prose.SetDiagnostics({
        Buffer::Diagnostic{.startByte = 0,
                           .endByte   = 3,
                           .severity  = Buffer::Diagnostic::Severity::Hint,
                           .origin    = Buffer::Diagnostic::Origin::Prose,
                           .message   = "spelling"},
    });

    BufferView view = fixture.View();
    view.RequestDiagnosticsBufferForTesting();

    Buffer* results = fixture.bufferList.Find("*diagnostics*");
    REQUIRE(results != nullptr);
    REQUIRE(results->Diagnostics().empty());
    REQUIRE(fixture.statusMessage == "No diagnostics.");
}

// multibuffer-visit-unification follow-up: project-search-visit-result
// (C-c C-v) used to be a straight-regex "path:line:" parse with no
// MultibufferIndexFor check at all, so it was a silent no-op on a
// *diagnostics* excerpt -- neither the "▸ a.cpp:2" header line (no
// trailing colon after the line number) nor the bare source-line body
// ("    return bogus;", no path prefix) matches that regex. Both
// project-search-visit-result and vcs-visit-result now delegate to the same
// BufferView::VisitResultUnderPoint, so either chord works here.
TEST_CASE("C-c C-v jumps to source from a *diagnostics* multibuffer excerpt", "[BufferView][Diagnostics]") {
    Fixture fixture;

    Buffer& a = fixture.bufferList.CreateBuffer("a.cpp");
    a.SetPath("/repo/a.cpp");
    a.InsertAtPoint("int main() {\n    return bogus;\n}\n");
    const std::size_t bogusStart = a.Text().find("bogus");
    a.SetDiagnostics({
        Buffer::Diagnostic{.startByte = bogusStart,
                           .endByte   = bogusStart + 5,
                           .severity  = Buffer::Diagnostic::Severity::Error,
                           .message   = "use of undeclared identifier 'bogus'"},
    });

    BufferView view = fixture.View();
    view.RequestDiagnosticsBufferForTesting();

    Buffer* results = fixture.bufferList.Find("*diagnostics*");
    REQUIRE(results != nullptr);

    // Point on the excerpt's body line, not its header -- proves the jump
    // comes from MultibufferIndex::SpanAtOffset, not a lucky regex match.
    const std::size_t bodyOffset = results->Text().find("return bogus");
    REQUIRE(bodyOffset != std::string::npos);
    results->SetPoint(bodyOffset);
    fixture.activeBuffer.Set(*results);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Ctrl('v'));

    REQUIRE(&fixture.activeBuffer.Get() == &a);
    REQUIRE(a.Content().ByteOffsetToLine(a.Point()) == 1); // 0-indexed line 1 == source line 2
}

// project-wide-diagnostics follow-up. The list used to cover only open
// buffers, which meant it showed whatever happened to be on screen rather
// than what is wrong with the project. A server that checks the whole thing
// reports most of its findings about files nobody has opened.
//
// The delivery path (a real publishDiagnostics frame reaching the store) has
// its own end-to-end coverage in LspManagerTest; this is about what the
// *diagnostics* multibuffer then does with it.
TEST_CASE("RequestDiagnosticsBuffer includes files that were never opened", "[BufferView][Diagnostics]") {
    using ned::editor::lsp::Manager;

    Fixture            fixture;
    ned::ui::EventLoop eventLoop;
    Manager            manager(fixture.bufferList, eventLoop);

    const std::filesystem::path root = std::filesystem::temp_directory_path() / "ned-diagnostics-buffer-project";
    std::filesystem::create_directories(root);
    const std::filesystem::path unopened = root / "never-opened.cpp";
    std::ofstream(unopened) << "int main() {\n    return \"nope\";\n}\n";

    manager.SetProjectDiagnosticsForTesting(
        unopened, "cpp",
        {Manager::ProjectDiagnostic{.start    = {.line = 1, .character = 11},
                                    .end      = {.line = 1, .character = 17},
                                    .severity = Buffer::Diagnostic::Severity::Error,
                                    .message  = "unopened-file finding"}});
    REQUIRE(manager.ProjectDiagnostics().size() == 1);

    // One open buffer with its own diagnostic, so the two sources have to
    // merge rather than one replacing the other. Named to sort after the
    // temp path, so the ordering assertion below is not accidental.
    Buffer& open = fixture.bufferList.CreateBuffer("zzz-open.cpp");
    open.SetPath("/zzz-repo/zzz-open.cpp"); // a diagnostic needs a path to name a file by
    open.InsertAtPoint("int other() { return 0; }\n");
    open.SetDiagnostics({Buffer::Diagnostic{.startByte = 4,
                                            .endByte   = 9,
                                            .severity  = Buffer::Diagnostic::Severity::Warning,
                                            .message   = "open-buffer finding"}});

    BufferView view = fixture.View();
    view.SetLspManager(&manager);
    view.RequestDiagnosticsBufferForTesting();

    Buffer* results = fixture.bufferList.Find("*diagnostics*");
    REQUIRE(results != nullptr);
    const std::string text = results->Text();
    REQUIRE(text.find(unopened.string()) != std::string::npos);  // the header names the file
    REQUIRE(text.find("return \"nope\";") != std::string::npos); // the body is its real line, read from disk
    REQUIRE(text.find("/zzz-repo/zzz-open.cpp") != std::string::npos); // merged with the open buffer's own

    // Both are re-applied as real diagnostics on the composite, so the
    // ordinary gutter/underline/next-error pipeline lights up for each.
    REQUIRE(results->Diagnostics().size() == 2);
    bool sawUnopened = false;
    for (const Buffer::Diagnostic& diagnostic : results->Diagnostics()) {
        if (diagnostic.message == "unopened-file finding") {
            sawUnopened = true;
            // Translated into composite space against the line's own text,
            // not left at the server's {line, character}.
            REQUIRE(diagnostic.endByte > diagnostic.startByte);
        }
    }
    REQUIRE(sawUnopened);

    SECTION("ned/set-project-diagnostics off scopes the list back to open buffers") {
        struct Restore {
            ~Restore() {
                ned::editor::lsp::SetProjectDiagnosticsEnabled(true);
            }
        } restore;
        ned::editor::lsp::SetProjectDiagnosticsEnabled(false);

        // Not bufferList.Find("*diagnostics*"): CreateBuffer uniquifies the
        // name, so a rebuild is a *new* buffer and Find would hand back the
        // one the first build left behind. RequestDiagnosticsBuffer makes
        // its result active, which is the handle that always means "the
        // list as of the last build".
        view.RequestDiagnosticsBufferForTesting();
        const std::string scoped = fixture.activeBuffer.Get().Text();
        REQUIRE(scoped.find(unopened.string()) == std::string::npos);
        REQUIRE(scoped.find("/zzz-repo/zzz-open.cpp") != std::string::npos);
    }

    std::filesystem::remove_all(root);
}

// The affordance that makes the wider list worth having: an excerpt whose
// file has no buffer at all still jumps, by opening it. Distinct from the
// sibling jump test above, where the target buffer was already resident and
// the jump only had to switch to it.
TEST_CASE("C-c C-v opens a file that was never opened, from a project-diagnostics excerpt",
          "[BufferView][Diagnostics]") {
    using ned::editor::lsp::Manager;

    Fixture            fixture;
    ned::ui::EventLoop eventLoop;
    Manager            manager(fixture.bufferList, eventLoop);

    const std::filesystem::path root = std::filesystem::temp_directory_path() / "ned-diagnostics-jump-project";
    std::filesystem::create_directories(root);
    const std::filesystem::path unopened = root / "never-opened.cpp";
    std::ofstream(unopened) << "int main() {\n    return \"nope\";\n}\n";

    manager.SetProjectDiagnosticsForTesting(unopened, "cpp",
                                            {Manager::ProjectDiagnostic{.start    = {.line = 1, .character = 11},
                                                                        .end      = {.line = 1, .character = 17},
                                                                        .severity = Buffer::Diagnostic::Severity::Error,
                                                                        .message  = "returning a string"}});
    REQUIRE(fixture.bufferList.FindByPath(unopened) == nullptr); // genuinely not open yet

    BufferView view = fixture.View();
    view.SetLspManager(&manager);
    view.RequestDiagnosticsBufferForTesting();

    Buffer& results    = fixture.activeBuffer.Get();
    const std::size_t bodyOffset = results.Text().find("return \"nope\"");
    REQUIRE(bodyOffset != std::string::npos);
    results.SetPoint(bodyOffset);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Ctrl('v'));

    Buffer* opened = fixture.bufferList.FindByPath(unopened);
    REQUIRE(opened != nullptr); // the jump had to open it
    REQUIRE(&fixture.activeBuffer.Get() == opened);
    REQUIRE(opened->Content().ByteOffsetToLine(opened->Point()) == 1); // 0-indexed line 1 == source line 2

    std::filesystem::remove_all(root);
}

// The excerpt cap is applied before any file is read, so the work is
// proportional to what is displayed rather than to whatever a server
// happened to report -- otherwise rendering the 500 excerpts the cap keeps
// could cost a read of every file in the store.
TEST_CASE("The project-diagnostics list is bounded by the excerpt cap, and reads lazily",
          "[BufferView][Diagnostics]") {
    using ned::editor::lsp::Manager;

    struct CapGuard {
        ~CapGuard() {
            ned::editor::SetMultibufferMaxExcerpts(500);
        }
    } capGuard;

    Fixture            fixture;
    ned::ui::EventLoop eventLoop;
    Manager            manager(fixture.bufferList, eventLoop);

    const std::filesystem::path root = std::filesystem::temp_directory_path() / "ned-diagnostics-cap-project";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    // Ten files, each with one diagnostic; the cap keeps three.
    for (int i = 0; i < 10; ++i) {
        const std::filesystem::path path = root / ("f" + std::to_string(i) + ".cpp");
        std::ofstream(path) << "line zero\nline one\nline two\n";
        manager.SetProjectDiagnosticsForTesting(path, "cpp",
                                                {Manager::ProjectDiagnostic{.start    = {.line = 1, .character = 5},
                                                                            .end      = {.line = 1, .character = 8},
                                                                            .severity = Buffer::Diagnostic::Severity::Error,
                                                                            .message  = "finding " + std::to_string(i)}});
    }
    REQUIRE(manager.ProjectDiagnostics().size() == 10);

    ned::editor::SetMultibufferMaxExcerpts(3);

    BufferView view = fixture.View();
    view.SetLspManager(&manager);
    view.RequestDiagnosticsBufferForTesting();

    Buffer&           results = fixture.activeBuffer.Get();
    const std::string text    = results.Text();

    // Three kept, in path order, and the seven that were not are neither
    // rendered nor read.
    REQUIRE(results.Diagnostics().size() == 3);
    REQUIRE(text.find("f0.cpp") != std::string::npos);
    REQUIRE(text.find("f2.cpp") != std::string::npos);
    REQUIRE(text.find("f9.cpp") == std::string::npos);

    // The body is the diagnostic's own line, streamed from disk -- line
    // index 1, not the first line the reader happens to reach.
    REQUIRE(text.find("line one") != std::string::npos);
    REQUIRE(text.find("line zero") == std::string::npos);

    // And the columns resolve against that line, not the whole file.
    for (const Buffer::Diagnostic& diagnostic : results.Diagnostics()) {
        REQUIRE(diagnostic.endByte > diagnostic.startByte);
    }

    // The status line counts what the project has, not what fit.
    REQUIRE(fixture.statusMessage.find("10 diagnostics") != std::string::npos);
    REQUIRE(fixture.statusMessage.find("showing 3") != std::string::npos);

    std::filesystem::remove_all(root);
}

// A file the server reported on that has since gone (or become unreadable)
// keeps its row rather than vanishing from the list: the finding is still a
// fact about the project, and the store's own stamp already marks the header.
TEST_CASE("A project-diagnostics file that cannot be read still lists", "[BufferView][Diagnostics]") {
    using ned::editor::lsp::Manager;

    Fixture            fixture;
    ned::ui::EventLoop eventLoop;
    Manager            manager(fixture.bufferList, eventLoop);

    const std::filesystem::path root = std::filesystem::temp_directory_path() / "ned-diagnostics-missing-project";
    std::filesystem::create_directories(root);
    const std::filesystem::path gone = root / "deleted-since.cpp";
    std::ofstream(gone) << "int main() { return 0; }\n";

    manager.SetProjectDiagnosticsForTesting(gone, "cpp",
                                            {Manager::ProjectDiagnostic{.start    = {.line = 0, .character = 4},
                                                                        .end      = {.line = 0, .character = 8},
                                                                        .severity = Buffer::Diagnostic::Severity::Error,
                                                                        .message  = "reported before it vanished"}});
    std::filesystem::remove(gone);

    BufferView view = fixture.View();
    view.SetLspManager(&manager);
    view.RequestDiagnosticsBufferForTesting();

    const std::string text = fixture.activeBuffer.Get().Text();
    REQUIRE(text.find(gone.string()) != std::string::npos);
    REQUIRE(text.find("(file changed since)") != std::string::npos); // the stamp no longer matches

    std::filesystem::remove_all(root);
}
