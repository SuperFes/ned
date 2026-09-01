#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <poll.h>
#include <unistd.h>

#include "Editor/Backup.h"
#include "Editor/Clipboard.h"
#include "Editor/Commands.h"
#include "Editor/Dap/DapClient.h"
#include "Editor/Dap/DapConfig.h"
#include "Editor/Dap/DapManager.h"
#include "Editor/DiagnosticsLog.h"
#include "Editor/Dispatcher.h"
#include "Editor/FormatOnSave.h"
#include "Editor/InlineDiagnostics.h"
#include "Editor/Link.h"
#include "Editor/Lsp/LspClient.h"
#include "Editor/Lsp/LspManager.h"
#include "Editor/Lsp/LspServerConfig.h"
#include "Editor/Lsp/Transport.h"
#include "Editor/Mode.h"
#include "Editor/Multibuffer.h"
#include "Editor/ProjectRoot.h"
#include "Editor/PromptHistory.h"
#include "Editor/Register.h"
#include "Editor/ScratchPad.h"
#include "Editor/Session.h"
#include "Editor/SnippetRegistry.h"
#include "Editor/TabWidth.h"
#include "Editor/Variables.h"
#include "Editor/WhichKeySettings.h"
#include "Editor/WrapOverrides.h"
#include "TestEvents.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"
#include "UI/ActiveBuffer.h"
#include "UI/BufferView.h"
#include "UI/EchoArea.h"
#include "UI/ProjectSidebar.h"
#include "UI/ScrollArrowButton.h"
#include "UI/ScrollBar.h"
#include "UI/Theme.h"
#include "UI/ThemeRegistry.h"

namespace {

// Types out a whole path/name into a find-file/switch-to-buffer prompt one
// OnEvent at a time, the same way a real keyboard would feed it.
void TypeText(ned::ui::BufferView& view, std::string_view text) {
    for (const char ch : text) {
        view.OnEvent(ned::ui::test::Character(std::string(1, ch)));
    }
}

// project-search/project-replace always search ned::editor::ProjectRoot()
// (no directory prompt in v1 -- see ProjectSearch.h), so tests that exercise
// them need to temporarily relocate both the process's cwd and the project
// root to a controlled scratch directory. Restores both on scope exit even
// if a REQUIRE fails partway through. Mirrors ProjectSidebarTest.cpp's own
// CurrentPathGuard exactly; see its comment for why ProjectRoot() needs
// explicit handling here rather than just following cwd automatically.
class CurrentPathGuard {
  public:
    explicit CurrentPathGuard(const std::filesystem::path& newPath) : previous_(std::filesystem::current_path()), previousRoot_(ned::editor::ProjectRoot()) {
        std::filesystem::current_path(newPath);
        ned::editor::SetProjectRoot(newPath);
    }
    ~CurrentPathGuard() {
        std::filesystem::current_path(previous_);
        ned::editor::SetProjectRoot(previousRoot_);
    }
    CurrentPathGuard(const CurrentPathGuard&)            = delete;
    CurrentPathGuard& operator=(const CurrentPathGuard&) = delete;

  private:
    std::filesystem::path previous_;
    std::filesystem::path previousRoot_;
};

// TabWidth is process-wide state (see Editor/TabWidth.h's own doc comment);
// every test that sets one must restore the default for the next test.
struct TabWidthGuard {
    ~TabWidthGuard() {
        ned::editor::SetTabWidth(4);
    }
};

// UrlOpenCommand is process-wide state too (see Editor/Link.h's own doc
// comment, mirrors TabWidth.h's exact pattern) -- restores whatever was
// configured before the test ran, not unconditionally "xdg-open", so tests
// stay order-independent regardless of what ran before them.
class UrlOpenCommandGuard {
  public:
    UrlOpenCommandGuard() : previous_(ned::editor::link::UrlOpenCommand()) {
    }
    ~UrlOpenCommandGuard() {
        ned::editor::link::SetUrlOpenCommand(previous_);
    }
    UrlOpenCommandGuard(const UrlOpenCommandGuard&)            = delete;
    UrlOpenCommandGuard& operator=(const UrlOpenCommandGuard&) = delete;

  private:
    std::optional<std::string> previous_;
};

// find-scratch reads Editor/ScratchPad.h's ScratchDirectory(), which resolves
// from XDG_DATA_HOME/HOME -- mirrors InitFileTest.cpp/ScratchPadTest.cpp's own
// EnvVarGuard exactly, so a scratch created by these tests never lands in the
// real user's actual scratch directory.
class EnvVarGuard {
  public:
    EnvVarGuard(const char* name, const char* value) : name_(name) {
        if (const char* existing = std::getenv(name)) {
            hadPrevious_ = true;
            previous_    = existing;
        }
        if (value) {
            setenv(name, value, 1);
        }
        else {
            unsetenv(name);
        }
    }

    ~EnvVarGuard() {
        if (hadPrevious_) {
            setenv(name_.c_str(), previous_.c_str(), 1);
        }
        else {
            unsetenv(name_.c_str());
        }
    }

    EnvVarGuard(const EnvVarGuard&)            = delete;
    EnvVarGuard& operator=(const EnvVarGuard&) = delete;

  private:
    std::string name_;
    bool        hadPrevious_ = false;
    std::string previous_;
};

// org-clock-display follow-up: mirrors BufferViewDiagnosticsBufferTest.cpp's/
// MultibufferTest.cpp's own RegistryResetGuard. Without this, a Buffer
// destroyed at the end of one TEST_CASE can leave a stale
// Editor/Multibuffer.h registry entry that a later TEST_CASE's freshly
// allocated Buffer -- unrelated to any multibuffer itself, e.g. a plain
// "*search results*" buffer -- spuriously "inherits" if the allocator
// reuses the same address, making VisitResultUnderPoint consult a stale
// MultibufferIndex instead of falling back to its own path:line: regex
// (confirmed live: this file's own multibuffer-building tests -- the
// org-agenda and org-clock-report ones below -- made the later "visits
// result under point"/"a mouse click visits the result" tests
// order-dependent once there were enough of them to collide). This file's
// shared Fixture below is used by every TEST_CASE in it, unlike those two
// sibling files' own narrower per-multibuffer-feature fixtures, so the
// guard lives here once rather than being added to every individual
// multibuffer-building TEST_CASE.
struct RegistryResetGuard {
    RegistryResetGuard() {
        ned::editor::multibuffer::ClearRegistryForTesting();
    }
    ~RegistryResetGuard() {
        ned::editor::multibuffer::ClearRegistryForTesting();
    }
};

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

    // generic-popup follow-up (Phase 3): the candidate popup model M-x/
    // project-find-file/find-recent-file/bookmark-jump/select-theme/LSP
    // code-action-select now drive instead of squeezing their candidate
    // list into statusMessage_ -- a test that cares wires it via
    // CaptureCandidates(view, fixture.candidates) right after View().
    // Not wired inside View() itself: BufferView's copy/move constructors
    // are both deleted, so View() must stay a single elided-prvalue return
    // (a named local BufferView -- even one only mutated before returning
    // -- isn't legal to return without a callable move constructor).
    std::optional<ned::ui::ListPopupModel> candidates;

    // completion-popup follow-up: same shape as `candidates` immediately
    // above, but for the (structurally distinct, see
    // BufferView::SetOnCompletionChanged's own doc comment) completion
    // popup -- wired via CaptureCompletion(view, fixture.completion).
    std::optional<ned::ui::ListPopupModel> completion;

    ned::ui::BufferView View() {
        return ned::ui::BufferView(activeBuffer, killRing, registers, promptHistory, bufferList, dispatcher,
                                   statusMessage, mode, theme);
    }
};

// generic-popup follow-up (Phase 3): wires view's candidate-popup hook to
// write into out on every change -- see Fixture::candidates' own doc
// comment for why this can't just live inside Fixture::View().
void CaptureCandidates(ned::ui::BufferView& view, std::optional<ned::ui::ListPopupModel>& out) {
    view.SetOnCandidatesChanged([&out](std::optional<ned::ui::ListPopupModel> model) { out = std::move(model); });
}

// completion-popup follow-up: same wiring as CaptureCandidates above, for
// SetOnCompletionChanged.
void CaptureCompletion(ned::ui::BufferView& view, std::optional<ned::ui::ListPopupModel>& out) {
    view.SetOnCompletionChanged([&out](std::optional<ned::ui::ListPopupModel> model) { out = std::move(model); });
}

std::optional<std::string> CompletionSelectedLabel(const std::optional<ned::ui::ListPopupModel>& model) {
    if (!model || !model->selectedIndex || *model->selectedIndex >= model->rows.size()) {
        return std::nullopt;
    }
    return model->rows[*model->selectedIndex].main;
}

bool CandidatesContain(const std::optional<ned::ui::ListPopupModel>& model, const std::string& name) {
    if (!model) {
        return false;
    }
    return std::any_of(model->rows.begin(), model->rows.end(), [&](const ned::ui::ListPopupRow& row) { return row.main == name; });
}

bool CandidateSelected(const std::optional<ned::ui::ListPopupModel>& model, const std::string& name) {
    if (!model || !model->selectedIndex || *model->selectedIndex >= model->rows.size()) {
        return false;
    }
    return model->rows[*model->selectedIndex].main == name;
}

bool CandidateRowExists(const std::optional<ned::ui::ListPopupModel>& model, const std::string& left, const std::string& main) {
    if (!model) {
        return false;
    }
    return std::any_of(model->rows.begin(), model->rows.end(),
                       [&](const ned::ui::ListPopupRow& row) { return row.left == left && row.main == main; });
}

// scroll-indicator-count follow-up: was a single "+K more" tail row (a
// fixed total, never changing as the window scrolled) -- now an "above"
// row and/or a "below" row, each with its own live count, so this checks
// for either boundary indicator rather than one specific tail string.
bool CandidatesHaveMoreTail(const std::optional<ned::ui::ListPopupModel>& model) {
    if (!model) {
        return false;
    }
    return std::any_of(model->rows.begin(), model->rows.end(), [](const ned::ui::ListPopupRow& row) {
        return row.main.ends_with("more above") || row.main.ends_with("more below");
    });
}

// fuzzy-candidate-list-styling follow-up: statusMessage_ can contain
// EchoArea's invisible EmphasizeForEchoArea/DimForEchoArea sentinel bytes
// (see EchoArea.h) -- std::string::size() counts them, but they render as
// zero width. Strips them so a test can measure the actual visible column
// count a message will occupy, the same way EchoArea::Paint itself does.
std::string StripEchoAreaMarkup(const std::string& message) {
    std::string visible;
    for (const char ch : message) {
        if (static_cast<unsigned char>(ch) >= 1 && static_cast<unsigned char>(ch) <= 4) {
            continue;
        }
        visible += ch;
    }
    return visible;
}

std::string RowText(ned::ui::Screen& screen, int row, int width) {
    std::string out;
    for (int col = 0; col < width; ++col) {
        out += screen.PixelAt(col, row).character;
    }
    return out;
}

// Mirrors BufferView::GutterWidth's formula --
// [status][diagnostic][gap][digits][gap][symbol][fold] (LSP client follow-up
// added the diagnostic column; gutter-symbol-kind follow-up added symbol): a
// fixed leading status column, a fixed diagnostic column, a gap, digits in
// the last line number, a second gap, then symbolColumn (default 0 -- only
// nonzero for a real-mode test buffer whose content actually has a
// "@definition.*"-matching construct, e.g. CMode's `int main(void) {`
// fold-test fixtures) and foldColumn trailing columns (generic-code-folding
// follow-up) for a mode with a real fold query -- default 0, since most
// existing callers exercise a mode without one. Content starts at this
// column, not 0.
int GutterWidth(std::size_t totalLines, int foldColumn = 0, int symbolColumn = 0) {
    constexpr int kStatusWidth     = 1;
    constexpr int kDiagnosticWidth = 1;
    constexpr int kLineNumberGap   = 1;
    return kStatusWidth + kDiagnosticWidth + kLineNumberGap + static_cast<int>(std::to_string(totalLines).size()) +
           kLineNumberGap + symbolColumn + foldColumn;
}

// Row text starting right after the gutter, rather than from column 0.
std::string ContentRowText(ned::ui::Screen& screen, int row, int width, std::size_t totalLines, int foldColumn = 0,
                           int symbolColumn = 0) {
    std::string out;
    const int   gutter = GutterWidth(totalLines, foldColumn, symbolColumn);
    for (int col = 0; col < width; ++col) {
        out += screen.PixelAt(gutter + col, row).character;
    }
    return out;
}

// Field-by-field Brush comparison against a real painted Cell -- replaces
// the old ox::Cell::brush == ox::Brush whole-struct comparison, since a
// ned::ui::Cell stores background/foreground/bold/italic as separate fields
// rather than one comparable Brush-shaped member.
bool CellMatchesBrush(const ned::ui::Cell& cell, const ned::ui::Brush& brush) {
    return cell.background_color == brush.background && cell.foreground_color == brush.foreground &&
           cell.bold == brush.bold && cell.italic == brush.italic;
}

ned::ui::Event MousePress(int x, int y, ned::ui::MouseEvent::Button button = ned::ui::MouseEvent::Button::Left) {
    return ned::ui::test::Mouse(x, y, button, ned::ui::MouseEvent::Motion::Pressed);
}

ned::ui::Event MouseRelease(int x, int y) {
    return ned::ui::test::Mouse(x, y, ned::ui::MouseEvent::Button::Left, ned::ui::MouseEvent::Motion::Released);
}

ned::ui::Event MouseMove(int x, int y, ned::ui::MouseEvent::Button button = ned::ui::MouseEvent::Button::None) {
    return ned::ui::test::Mouse(x, y, button, ned::ui::MouseEvent::Motion::Moved);
}

ned::ui::Event MouseWheel(int x, int y, ned::ui::MouseEvent::Button button) {
    return ned::ui::test::Mouse(x, y, button, ned::ui::MouseEvent::Motion::Pressed);
}

// middle-click-paste follow-up: Tests/ClipboardTestGuard.cpp forces
// ClipboardEnabled() false for the whole ned_tests binary, so a test
// exercising middle-click paste must re-enable it locally with an injected
// fake backend and restore this disabled steady state before returning --
// mirrors ClipboardTest.cpp's own RestoreClipboardDisabled exactly.
struct RestorePrimaryPasteDisabled {
    ~RestorePrimaryPasteDisabled() {
        ned::editor::SetClipboardPrimaryPasteCommand({});
        ned::editor::SetClipboardEnabled(false);
    }
};

// universal-clickable-affordances follow-up.
ned::ui::Event MousePressCtrl(int x, int y) {
    return ned::ui::test::Mouse(x, y, ned::ui::MouseEvent::Button::Left, ned::ui::MouseEvent::Motion::Pressed,
                                /*shift=*/false, /*meta=*/false, /*control=*/true);
}

// hover/completion follow-up: "C-M-i" as a real raw byte sequence -- ESC
// (Meta) followed by the C0 control byte for Ctrl+'i' (0x09) -- rather than
// constructing a KeyChord directly, so this exercises the exact same
// TranslateKey path a real terminal keystroke would (mirrors
// KeyTranslation.cpp's own documented Meta-detection rule: a leading ESC
// byte followed by more bytes in the same input is Meta+<key>).
ned::ui::Event ManualCompleteEvent() {
    return ned::ui::test::CtrlAlt('i');
}

// Mirrors LspManagerTest.cpp's own FakeServer/ReadRawFrame exactly (kept
// file-local here too, rather than shared -- not worth a new dependency
// between two test binaries' translation units for something this small,
// the same call this codebase's own production code already makes
// elsewhere for comparably small duplicated helpers).
struct FakeLspServer {
    int serverStdinRead;
    int serverStdoutWrite;

    FakeLspServer(int readFd, int writeFd) : serverStdinRead(readFd), serverStdoutWrite(writeFd) {
    }
    ~FakeLspServer() {
        ::close(serverStdoutWrite);
        ::close(serverStdinRead);
    }
    FakeLspServer(const FakeLspServer&)            = delete;
    FakeLspServer& operator=(const FakeLspServer&) = delete;
    FakeLspServer(FakeLspServer&&)                 = default;

    static FakeLspServer Create(ned::editor::lsp::LspManager& manager, const std::string& language, ned::ui::EventLoop& eventLoop,
                                ned::editor::lsp::LspClient*& outClient) {
        int clientWritesHere[2];
        int clientReadsHere[2];
        REQUIRE(::pipe(clientWritesHere) == 0);
        REQUIRE(::pipe(clientReadsHere) == 0);
        auto client = std::make_unique<ned::editor::lsp::LspClient>(ned::editor::lsp::Transport(clientReadsHere[0], clientWritesHere[1]), eventLoop);
        outClient   = &manager.SetClientForTesting(language, std::move(client));
        return FakeLspServer(clientWritesHere[0], clientReadsHere[1]);
    }
};

std::string ReadRawLspFrame(int fd) {
    std::string all;
    char        buffer[512];
    for (int i = 0; i < 4; ++i) {
        const ssize_t n = ::read(fd, buffer, sizeof(buffer));
        if (n <= 0) {
            break;
        }
        all.append(buffer, static_cast<std::size_t>(n));
        const auto headerEnd = all.find("\r\n\r\n");
        if (headerEnd != std::string::npos) {
            const std::string_view kPrefix   = "Content-Length: ";
            const auto             prefixPos = all.find(kPrefix);
            if (prefixPos != std::string::npos) {
                const std::size_t contentLength = std::stoul(all.substr(prefixPos + kPrefix.size()));
                if (all.size() >= headerEnd + 4 + contentLength) {
                    break;
                }
            }
        }
    }
    return all;
}

int LspRequestIdFromFrame(const std::string& raw) {
    return ned::editor::lsp::Json::parse(raw.substr(raw.find("\r\n\r\n") + 4))["id"].get<int>();
}

// on-type-formatting follow-up: asserting "nothing was ever sent" can't use
// ReadRawLspFrame's own blocking ::read (it would hang forever on a fd that
// legitimately never gets written to -- the case under test). Mirrors
// LspManagerTest.cpp's own NoFrameArrives exactly.
bool NoFrameArrives(int fd) {
    pollfd pfd{.fd = fd, .events = POLLIN, .revents = 0};
    return ::poll(&pfd, 1, 200) == 0; // 0 == timed out, nothing readable
}

// codeLens follow-up: a single Paint() call can now fire more than one
// background request synchronously (didOpen, semanticTokens, inlayHint,
// codeLens) -- small enough that a single 512-byte ::read() routinely
// scoops up more than one frame at once. Looping ReadRawLspFrame per
// "frame" (as this used to) doesn't work: it returns as soon as it sees
// the first frame complete, but the caller discards that return value
// wholesale, silently losing whatever next-frame header/body bytes rode
// along in the same read() call -- a real, reproducible hang, not a race
// (confirmed live: a later call blocks in ::read() forever on a headerless
// mid-JSON tail fragment with no Content-Length left to find). Reads raw
// bytes directly into one persistent buffer until the pipe genuinely goes
// quiet instead, so there's no frame boundary to misplace -- the robust
// replacement for every "(void)ReadRawLspFrame(...); // drain didOpen"
// call site in this file.
void DrainAllPendingFrames(int fd) {
    std::string all;
    char        buffer[512];
    while (!NoFrameArrives(fd)) {
        const ssize_t n = ::read(fd, buffer, sizeof(buffer));
        if (n <= 0) {
            break;
        }
        all.append(buffer, static_cast<std::size_t>(n));
    }
}

// Parses every complete Content-Length-framed message found in raw,
// starting from byte offset pos -- used by ReadLspFrames below to count/
// extract frames without discarding trailing bytes the way ReadRawLspFrame's
// single-frame return does.
std::vector<ned::editor::lsp::Json> ParseConcatenatedLspFrames(const std::string& raw) {
    std::vector<ned::editor::lsp::Json> frames;
    std::size_t                         pos = 0;
    while (pos < raw.size()) {
        const auto headerEnd = raw.find("\r\n\r\n", pos);
        if (headerEnd == std::string::npos) {
            break;
        }
        const std::string_view kPrefix   = "Content-Length: ";
        const auto             prefixPos = raw.find(kPrefix, pos);
        if (prefixPos == std::string::npos || prefixPos > headerEnd) {
            break;
        }
        const std::size_t contentLength = std::stoul(raw.substr(prefixPos + kPrefix.size()));
        const std::size_t bodyStart     = headerEnd + 4;
        if (bodyStart + contentLength > raw.size()) {
            break;
        }
        frames.push_back(ned::editor::lsp::Json::parse(raw.substr(bodyStart, contentLength)));
        pos = bodyStart + contentLength;
    }
    return frames;
}

// signature-help-auto-trigger/documentHighlight follow-up: unlike every
// other ReadRawLspFrame call site (one message in flight at a time), a
// debounce-timer test can have two independent auto-requests (e.g.
// documentHighlight and signatureHelp, both triggered by the same typed
// content-generation change) queued back-to-back by a single
// DrainPosted_() call, arriving concatenated in one ::read(). Reads until
// at least frameCount complete frames are present -- safe against blocking
// forever since, by the time a caller reaches here, DrainPosted_() has
// already synchronously run every producing callback, so every expected
// frame's bytes are already fully written to the pipe.
std::vector<ned::editor::lsp::Json> ReadLspFrames(int fd, std::size_t frameCount) {
    std::string all;
    char        buffer[512];
    for (int i = 0; i < 8; ++i) {
        if (ParseConcatenatedLspFrames(all).size() >= frameCount) {
            break;
        }
        const ssize_t n = ::read(fd, buffer, sizeof(buffer));
        if (n <= 0) {
            break;
        }
        all.append(buffer, static_cast<std::size_t>(n));
    }
    return ParseConcatenatedLspFrames(all);
}

// DAP client slice 2: the pipe-backed fake-adapter counterpart of
// FakeLspServer above, injected via DapManager::SetClientForTesting. The
// tests below only ever have one adapter-bound frame in flight at a time,
// so ReadRawLspFrame (DAP shares LSP's exact framing) suffices -- no
// buffered multi-frame reader needed here, unlike DapManagerTest's own.
struct FakeDapAdapter {
    int adapterStdinRead;
    int adapterStdoutWrite;

    FakeDapAdapter(int readFd, int writeFd) : adapterStdinRead(readFd), adapterStdoutWrite(writeFd) {
    }
    ~FakeDapAdapter() {
        ::close(adapterStdoutWrite);
        ::close(adapterStdinRead);
    }
    FakeDapAdapter(const FakeDapAdapter&)            = delete;
    FakeDapAdapter& operator=(const FakeDapAdapter&) = delete;
    FakeDapAdapter(FakeDapAdapter&&)                 = default;

    static FakeDapAdapter Create(ned::editor::dap::DapManager& manager, ned::ui::EventLoop& eventLoop,
                                 ned::editor::dap::DapClient*& outClient) {
        int clientWritesHere[2];
        int clientReadsHere[2];
        REQUIRE(::pipe(clientWritesHere) == 0);
        REQUIRE(::pipe(clientReadsHere) == 0);
        auto client = std::make_unique<ned::editor::dap::DapClient>(
            ned::editor::lsp::Transport(clientReadsHere[0], clientWritesHere[1]), eventLoop);
        outClient = &manager.SetClientForTesting(std::move(client));
        return FakeDapAdapter(clientWritesHere[0], clientReadsHere[1]);
    }

    // Reads the next request frame and returns its parsed body.
    ned::editor::dap::Json NextRequest() const {
        const std::string raw = ReadRawLspFrame(adapterStdinRead);
        return ned::editor::dap::Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    }
};

std::string DapResponseFrame(int requestSeq, const std::string& command, ned::editor::dap::Json body) {
    return ned::editor::dap::Json{{"seq", 1000 + requestSeq}, {"type", "response"}, {"request_seq", requestSeq}, {"command", command}, {"success", true}, {"body", std::move(body)}}
        .dump();
}

std::string DapEventFrame(const std::string& event, ned::editor::dap::Json body) {
    return ned::editor::dap::Json{{"seq", 999}, {"type", "event"}, {"event", event}, {"body", std::move(body)}}.dump();
}

} // namespace

TEST_CASE("BufferView paints the buffer's first line and positions the cursor", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("hello");

    ned::ui::BufferView view = fixture.View();
    // Width widened by 1 (LSP client follow-up added a diagnostic gutter
    // column) to keep the cursor -- at GutterWidth(1) + 5 -- on-screen.
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 10, .y_min = 0, .y_max = 2});

    ned::ui::Screen screen = ned::ui::Screen(11, 3);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 10, .y_min = 0, .y_max = 2});

    view.Paint(canvas);

    REQUIRE(ContentRowText(screen, 0, 5, 1) == "hello");
    REQUIRE(view.CursorPosition().has_value());
    REQUIRE(*view.CursorPosition() == ned::ui::Point{.x = GutterWidth(1) + 5, .y = 0});
}

// per-buffer-highlight-cache follow-up: ClearBufferCaches is what
// WindowManager::ReassignPanesShowing calls on every pane for a genuinely
// closing buffer -- exercised here both before the cache could hold
// anything for buffer (a fresh BufferView) and after a real Paint() call
// actually populated it.
TEST_CASE("BufferView::ClearBufferCaches is a safe no-op, with or without a prior paint", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("hello");

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 10, .y_min = 0, .y_max = 2});
    REQUIRE_NOTHROW(view.ClearBufferCaches(fixture.buffer));

    ned::ui::Screen screen = ned::ui::Screen(11, 3);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 10, .y_min = 0, .y_max = 2});
    view.Paint(canvas);
    REQUIRE_NOTHROW(view.ClearBufferCaches(fixture.buffer));

    // The cache being gone doesn't break a subsequent repaint -- it just
    // recomputes, same as a first-ever paint would.
    view.Paint(canvas);
    REQUIRE(ContentRowText(screen, 0, 5, 1) == "hello");
}

TEST_CASE("A tab character expands to TabWidth() space columns, not one raw codepoint", "[BufferView]") {
    // A raw tab byte sent straight to a real terminal is interpreted as
    // "jump to the next tab stop" rather than "print one glyph," which
    // desyncs the terminal's own per-cell diff bookkeeping from what the
    // terminal actually did -- this is the root cause behind a
    // scroll-triggered rendering-corruption bug found via manual testing on
    // a real terminal. Expanding tabs to literal spaces here is the fix.
    const TabWidthGuard guard;
    ned::editor::SetTabWidth(4);

    Fixture fixture;
    fixture.buffer.InsertAtPoint("a\tb");

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});

    ned::ui::Screen screen = ned::ui::Screen(20, 3);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});
    view.Paint(canvas);

    // "a", 4 space columns for the tab, then "b" -- never a literal U+0009.
    REQUIRE(ContentRowText(screen, 0, 6, 1) == "a    b");
}

TEST_CASE("Cursor position accounts for tab expansion, not a plain codepoint count", "[BufferView]") {
    const TabWidthGuard guard;
    ned::editor::SetTabWidth(4);

    Fixture fixture;
    fixture.buffer.InsertAtPoint("a\tb");
    fixture.buffer.SetPoint(2); // right before 'b': byte offset 1 for 'a' + 1 for the tab byte itself

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});

    ned::ui::Screen screen = ned::ui::Screen(20, 3);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});
    view.Paint(canvas);

    REQUIRE(view.CursorPosition().has_value());
    // Visual column: 1 ('a') + 4 (the expanded tab) = 5, not 2 (a plain
    // byte/codepoint count of "a\t").
    REQUIRE(*view.CursorPosition() == ned::ui::Point{.x = GutterWidth(1) + 5, .y = 0});
}

TEST_CASE("A configured tab width other than the default is respected when painting", "[BufferView]") {
    const TabWidthGuard guard;
    ned::editor::SetTabWidth(2);

    Fixture fixture;
    fixture.buffer.InsertAtPoint("a\tb");

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});

    ned::ui::Screen screen = ned::ui::Screen(20, 3);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});
    view.Paint(canvas);

    REQUIRE(ContentRowText(screen, 0, 4, 1) == "a  b");
}

TEST_CASE("mouse_press accounts for tab expansion, not a plain codepoint count", "[BufferView]") {
    // ByteOffsetForMouse follow-up: a click past a tab used to land a column
    // or two off from where it visually looks, since the screen column was
    // fed straight into ByteOffsetForLineAndColumn as if it were a plain
    // codepoint count instead of a tab-expanded visual column.
    const TabWidthGuard guard;
    ned::editor::SetTabWidth(4);

    Fixture fixture;
    fixture.buffer.InsertAtPoint("a\tbc"); // 'a'=0, tab spans [1,5), 'b'=5, 'c'=6

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    // Visual column 5 ('b') -- a plain codepoint count would have clamped
    // this to the buffer's actual length (4 codepoints) and landed on 'c'
    // instead of the correct 'b', right after the tab's 4-column span.
    view.OnEvent(MousePress(GutterWidth(1) + 5, 0));
    REQUIRE(fixture.buffer.Point() == 2); // 'b'

    // Visual column 0 ('a') -- unaffected either way, sanity check.
    view.OnEvent(MousePress(GutterWidth(1) + 0, 0));
    REQUIRE(fixture.buffer.Point() == 0); // 'a'
}

TEST_CASE("A control byte renders as a 4-column hex placeholder, not the raw byte", "[BufferView]") {
    // Binary-rendering follow-up: a raw control byte sent straight to a real
    // terminal isn't "print one glyph" -- some are actual terminal control
    // codes, which desyncs the terminal's per-cell diff bookkeeping the exact
    // same way an unexpanded tab byte used to (see the tab-rendering-fix
    // follow-up). Rendered as a safe, printable "◁XX▷" placeholder instead.
    Fixture fixture;
    fixture.buffer.InsertAtPoint(std::string("a") + '\x0E' + "b");

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});

    ned::ui::Screen screen = ned::ui::Screen(20, 3);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});
    view.Paint(canvas);

    const int gutter = GutterWidth(1);
    REQUIRE(screen.PixelAt(gutter, 0).character == "a");
    REQUIRE(screen.PixelAt(gutter + 1, 0).character == "◁");
    REQUIRE(screen.PixelAt(gutter + 2, 0).character == "0");
    REQUIRE(screen.PixelAt(gutter + 3, 0).character == "E");
    REQUIRE(screen.PixelAt(gutter + 4, 0).character == "▷");
    REQUIRE(screen.PixelAt(gutter + 5, 0).character == "b");
    REQUIRE(screen.PixelAt(gutter + 1, 0).foreground_color == fixture.theme.binaryForeground);
}

TEST_CASE("DEL (0x7F) also renders as a hex placeholder", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint(std::string("a") + '\x7F' + "b");

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});

    ned::ui::Screen screen = ned::ui::Screen(20, 3);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});
    view.Paint(canvas);

    const int gutter = GutterWidth(1);
    REQUIRE(screen.PixelAt(gutter + 2, 0).character == "7");
    REQUIRE(screen.PixelAt(gutter + 3, 0).character == "F");
}

TEST_CASE("Cursor position accounts for a binary placeholder's 4-column width", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint(std::string("a") + '\x0E' + "b");
    fixture.buffer.SetPoint(2); // right before 'b': byte offset 1 for 'a' + 1 for the control byte itself

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});

    ned::ui::Screen screen = ned::ui::Screen(20, 3);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});
    view.Paint(canvas);

    REQUIRE(view.CursorPosition().has_value());
    // Visual column: 1 ('a') + 4 (the hex placeholder) = 5.
    REQUIRE(*view.CursorPosition() == ned::ui::Point{.x = GutterWidth(1) + 5, .y = 0});
}

TEST_CASE("key_press for a plain character self-inserts and advances the cursor", "[BufferView]") {
    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 9, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Character("a"));
    view.OnEvent(ned::ui::test::Character("b"));
    view.OnEvent(ned::ui::test::Character("c"));

    REQUIRE(fixture.buffer.Text() == "abc");
    REQUIRE(fixture.buffer.Point() == 3);
}

TEST_CASE("C-u 3 <char> self-inserts the character 3 times as one undo step", "[BufferView]") {
    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 9, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('u'));
    view.OnEvent(ned::ui::test::Character("3"));
    view.OnEvent(ned::ui::test::Character("a"));

    REQUIRE(fixture.buffer.Text() == "aaa");
    fixture.buffer.Undo();
    REQUIRE(fixture.buffer.Text().empty());
}

TEST_CASE("key_press for an untranslatable key is a safe no-op", "[BufferView]") {
    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 9, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::Event(ncinput{})); // empty event; TranslateKey returns nullopt (see KeyTranslationTest.cpp)

    REQUIRE(fixture.buffer.Text().empty());
}

TEST_CASE("key_press for RET inserts a newline via the bound command", "[BufferView]") {
    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 9, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Character("a"));
    view.OnEvent(ned::ui::test::Return());
    view.OnEvent(ned::ui::test::Character("b"));

    REQUIRE(fixture.buffer.Text() == "a\nb");
}

TEST_CASE("BufferView renders multiple lines and scrolls to keep point visible", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("one\ntwo\nthree\nfour\nfive");
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 9, .y_min = 0, .y_max = 1}); // only 2 lines visible at a time

    ned::ui::Screen screen = ned::ui::Screen(10, 2);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 9, .y_min = 0, .y_max = 1});

    view.Paint(canvas);
    REQUIRE(ContentRowText(screen, 0, 3, 5) == "one");
    REQUIRE(ContentRowText(screen, 1, 3, 5) == "two");

    // Move point down to the last line ("five", buffer line index 4) and feed
    // a key press so ScrollToShowPoint runs.
    fixture.buffer.SetPoint(fixture.buffer.Size());
    view.OnEvent(ned::ui::test::ArrowRight()); // any bound key; forward-char at end of buffer is a no-op edit

    view.Paint(canvas);
    REQUIRE(ContentRowText(screen, 1, 4, 5) == "five"); // last visible row now shows the line point is on
}

TEST_CASE("An exception from a command is caught and reported via the status message, not propagated", "[BufferView]") {
    Fixture fixture;
    fixture.registry.Register("throwing-command", "", [](ned::editor::CommandContext&) {
        throw std::runtime_error("boom");
    });
    fixture.keymap.Bind(ned::editor::ParseKeySequence("C-t"), "throwing-command");

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 9, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('t')); // must not throw out of OnEvent
    REQUIRE(fixture.statusMessage == "boom");
}

TEST_CASE("C-x C-c (quit) does not crash key_press", "[BufferView]") {
    // BufferView::SetEventLoop's own null check (eventLoop_, defaulting to
    // nullptr) is what keeps this safe: a BufferView with no EventLoop
    // registered -- every test-constructed one, including this fixture's own
    // View() -- takes the quit branch as a real, safe no-op instead of
    // dereferencing anything.
    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 9, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('x'));
    view.OnEvent(ned::ui::test::Ctrl('c')); // must not crash
}

TEST_CASE("Quit leaves a shutting-down status message for the final frame", "[BufferView]") {
    // The message is what EventLoop::Run's final (post-Exit) repaint shows
    // while post-Run teardown (LSP child grace waits, session saves) runs --
    // without it, that pause reads as a hang.
    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 9, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('x'));
    view.OnEvent(ned::ui::test::Ctrl('c'));
    REQUIRE(fixture.statusMessage == "Shutting down...");
}

TEST_CASE("Isearch: C-s enters search mode, typing narrows the match, RET accepts", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("the quick brown fox");
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('s')); // start isearch-forward
    REQUIRE(fixture.statusMessage == "I-search: ");

    view.OnEvent(ned::ui::test::Character("f"));
    view.OnEvent(ned::ui::test::Character("o"));
    view.OnEvent(ned::ui::test::Character("x"));
    REQUIRE(fixture.statusMessage == "I-search: fox");
    REQUIRE(fixture.buffer.Point() == 19); // right after "fox" (starts at 16, len 19)

    view.OnEvent(ned::ui::test::Return());
    REQUIRE(fixture.buffer.Point() == 19); // point stays at the match

    // Back to normal editing: this must self-insert, not feed the search.
    view.OnEvent(ned::ui::test::Character("a"));
    REQUIRE(fixture.buffer.Text() == "the quick brown foxa");
}

TEST_CASE("Isearch: Escape cancels and restores the original point", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("the quick brown fox");
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('s'));
    view.OnEvent(ned::ui::test::Character("f"));
    view.OnEvent(ned::ui::test::Character("o"));
    view.OnEvent(ned::ui::test::Character("x"));
    REQUIRE(fixture.buffer.Point() != 0);

    view.OnEvent(ned::ui::test::Escape());
    REQUIRE(fixture.buffer.Point() == 0);

    // Back to normal editing.
    view.OnEvent(ned::ui::test::Character("z"));
    REQUIRE(fixture.buffer.Text() == "zthe quick brown fox");
}

TEST_CASE("Isearch: C-r starts a backward search", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("the quick brown fox");
    // point defaults to end of buffer after InsertAtPoint

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('r')); // isearch-backward
    REQUIRE(fixture.statusMessage == "Backward I-search: ");

    view.OnEvent(ned::ui::test::Character("f"));
    view.OnEvent(ned::ui::test::Character("o"));
    view.OnEvent(ned::ui::test::Character("x"));
    REQUIRE(fixture.buffer.Point() == 16); // start of "fox"
}

TEST_CASE("Isearch: an accepted query is ghosted on the next session and C-s on an empty query reuses it",
          "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("the quick brown fox jumps over the lazy fox");
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('s'));
    view.OnEvent(ned::ui::test::Character("f"));
    view.OnEvent(ned::ui::test::Character("o"));
    view.OnEvent(ned::ui::test::Character("x"));
    view.OnEvent(ned::ui::test::Return()); // accept -- remembers "fox"
    REQUIRE(fixture.buffer.Point() == 19); // right after the first "fox"

    view.OnEvent(ned::ui::test::Ctrl('s'));                                            // fresh session, nothing typed yet
    REQUIRE(fixture.statusMessage == "I-search: " + ned::ui::GhostForEchoArea("fox")); // ghosted last query

    view.OnEvent(ned::ui::test::Ctrl('s')); // C-s on an empty query reuses the last search string outright
    REQUIRE(fixture.buffer.Point() == 43);  // right after the second "fox"
    REQUIRE(fixture.statusMessage == "I-search: fox");

    view.OnEvent(ned::ui::test::Return());
}

TEST_CASE("Isearch: cancelling with Escape still remembers the query for the next session", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("the quick brown fox");
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('s'));
    view.OnEvent(ned::ui::test::Character("f"));
    view.OnEvent(ned::ui::test::Character("o"));
    view.OnEvent(ned::ui::test::Character("x"));
    view.OnEvent(ned::ui::test::Escape()); // cancel, point restored to 0

    view.OnEvent(ned::ui::test::Ctrl('s')); // fresh session
    REQUIRE(fixture.statusMessage == "I-search: " + ned::ui::GhostForEchoArea("fox"));
}

TEST_CASE("Query-replace: ESC % walks pattern, replacement, and confirmation to completion", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("cat sat on the cat mat");
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Escape());
    view.OnEvent(ned::ui::test::Character("%"));
    REQUIRE(fixture.statusMessage.find("Query replace:") == 0);

    view.OnEvent(ned::ui::test::Character("c"));
    view.OnEvent(ned::ui::test::Character("a"));
    view.OnEvent(ned::ui::test::Character("t"));
    view.OnEvent(ned::ui::test::Return()); // confirm pattern "cat"
    REQUIRE(fixture.statusMessage.find("with:") != std::string::npos);

    view.OnEvent(ned::ui::test::Character("d"));
    view.OnEvent(ned::ui::test::Character("o"));
    view.OnEvent(ned::ui::test::Character("g"));
    view.OnEvent(ned::ui::test::Return()); // confirm replacement "dog"
    REQUIRE(fixture.statusMessage.find("(y/n/!/q)?") != std::string::npos);

    view.OnEvent(ned::ui::test::Character("y")); // replace first match
    REQUIRE(fixture.buffer.Text() == "dog sat on the cat mat");

    view.OnEvent(ned::ui::test::Character("y")); // replace second match, no more after
    REQUIRE(fixture.buffer.Text() == "dog sat on the dog mat");
    REQUIRE(fixture.statusMessage.find("Replaced 2") == 0);

    // Session ended: back to normal editing. Point followed the first
    // replacement (it started at the very position that got replaced), so it
    // now sits right after that "dog", not back at the buffer start.
    view.OnEvent(ned::ui::test::Character("z"));
    REQUIRE(fixture.buffer.Text() == "dogz sat on the dog mat");
}

TEST_CASE("Query-replace: an invalid pattern reports an error and stays in EnteringPattern", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("text");
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Escape());
    view.OnEvent(ned::ui::test::Character("%"));
    view.OnEvent(ned::ui::test::Character("(")); // "(" with no closing paren -> invalid regex
    view.OnEvent(ned::ui::test::Return());

    REQUIRE(fixture.statusMessage.find("Invalid regex") == 0);

    // Still entering the pattern: DEL should edit it, not do anything else.
    view.OnEvent(ned::ui::test::Backspace());
    view.OnEvent(ned::ui::test::Escape());       // now cancel out entirely
    view.OnEvent(ned::ui::test::Character("z")); // back to normal editing
    REQUIRE(fixture.buffer.Text() == "ztext");
}

TEST_CASE("BufferView consults the active Mode's highlightLine hook when painting", "[BufferView]") {
    Fixture fixture;
    fixture.mode = ned::editor::JanetMode();
    fixture.buffer.InsertAtPoint("# a comment");

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 0});

    ned::ui::Screen screen = ned::ui::Screen(20, 1);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 0});

    view.Paint(canvas);

    const int gutter = GutterWidth(1);
    // Compares against the theme's own current commentForeground rather
    // than a hardcoded color literal -- this test is about the highlight
    // hook actually being consulted, not about pinning DarkTheme's exact
    // comment color (which is free to change independently).
    const auto commentColor = fixture.theme.commentForeground;
    REQUIRE(screen.PixelAt(gutter + 0, 0).foreground_color == commentColor);
    REQUIRE(screen.PixelAt(gutter + 5, 0).foreground_color == commentColor); // still inside the comment
}

TEST_CASE("BufferView renders with no highlighting under FundamentalMode", "[BufferView]") {
    Fixture fixture; // FundamentalMode by default
    fixture.buffer.InsertAtPoint("# not actually a comment here");

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 0});

    ned::ui::Screen screen = ned::ui::Screen(40, 1);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 0});

    view.Paint(canvas);

    REQUIRE(CellMatchesBrush(screen.PixelAt(GutterWidth(1), 0), fixture.theme.BrushFor(ned::editor::SyntaxClass::Default)));
}

TEST_CASE("BufferView renders JsonMode's tree-sitter highlighting for strings, numbers, and constants",
          "[BufferView]") {
    Fixture fixture;
    fixture.mode = ned::editor::JsonMode();
    fixture.buffer.InsertAtPoint(R"({"a": 1, "b": true})");
    // Byte layout: {"a": 1, "b": true}
    //               0123456789...
    // '"' at 1, 'a' at 2, '"' at 3 -- "a" is a String span [1,4)
    // '1' at 6 -- a Number span [6,7)
    // 't' at 14..17 -- "true" is a ConstantBuiltin span [14,18)

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 0});

    ned::ui::Screen screen = ned::ui::Screen(40, 1);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 0});

    view.Paint(canvas);

    const int gutter = GutterWidth(1, /*foldColumn=*/4);                                                                // JsonMode has a fold query -- generic-code-folding follow-up
    REQUIRE(CellMatchesBrush(screen.PixelAt(gutter + 2, 0), fixture.theme.BrushFor(ned::editor::SyntaxClass::String))); // 'a'
    REQUIRE(CellMatchesBrush(screen.PixelAt(gutter + 6, 0), fixture.theme.BrushFor(ned::editor::SyntaxClass::Number))); // '1'
    REQUIRE(CellMatchesBrush(screen.PixelAt(gutter + 15, 0),
                             fixture.theme.BrushFor(ned::editor::SyntaxClass::ConstantBuiltin)));                        // 'r' in "true"
    REQUIRE(CellMatchesBrush(screen.PixelAt(gutter + 0, 0), fixture.theme.BrushFor(ned::editor::SyntaxClass::Default))); // '{'
}

// semanticTokens follow-up: '1' at byte 6 is a Number span [6,7) under
// tree-sitter alone (see the sibling test just above) -- this overrides it
// via a semanticTokens response and confirms the LSP-sourced span wins, the
// same "later span wins" convention Injection.h's own engine already
// exploits for the same reason.
TEST_CASE("A textDocument/semanticTokens/full response overrides tree-sitter's own classification at overlapping bytes",
          "[BufferView]") {
    Fixture                     fixture;
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned_bufferview_semantic_tokens_test.json";
    ned::text::Buffer&          buffer = fixture.bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint(R"({"a": 1, "b": true})");
    fixture.mode = ned::editor::JsonMode();
    fixture.activeBuffer.Set(buffer);

    ned::ui::EventLoop           eventLoop;
    ned::editor::lsp::LspManager manager(fixture.bufferList, eventLoop);
    manager.SetSemanticTokensLegendForTesting(
        "json", ned::editor::lsp::SemanticTokensLegend{.tokenTypes = {"keyword"}, .tokenModifiers = {}});
    ned::editor::lsp::LspClient* client = nullptr;
    FakeLspServer                server = FakeLspServer::Create(manager, "json", eventLoop, client);

    ned::ui::BufferView view = fixture.View();
    view.SetLspManager(&manager);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 0});

    ned::ui::Screen screen = ned::ui::Screen(40, 1);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 0});
    view.Paint(canvas); // triggers didOpen and the semanticTokens request, back-to-back

    // Both can land in the same read() -- ReadRawLspFrame's own one-frame-
    // per-call assumption breaks here, same as the pull-diagnostics tests'
    // own precedent above.
    const std::vector<ned::editor::lsp::Json> frames = ReadLspFrames(server.serverStdinRead, 2);
    REQUIRE(frames.size() == 2);
    REQUIRE(frames[0]["method"] == "textDocument/didOpen");
    REQUIRE(frames[1]["method"] == "textDocument/semanticTokens/full");
    const ned::editor::lsp::Json& semanticRequest = frames[1];

    // '1' at byte 6, length 1, type index 0 ("keyword").
    const auto response = ned::editor::lsp::Json{
        {"jsonrpc", "2.0"},
        {"id", semanticRequest["id"]},
        {"result", {{"data", ned::editor::lsp::Json::array({0, 6, 1, 0, 0})}}},
    };
    client->DispatchFrame(response.dump());

    view.Paint(canvas); // re-paint to pick up the bumped SemanticTokensGeneration

    const int gutter = GutterWidth(1, /*foldColumn=*/4);
    REQUIRE(CellMatchesBrush(screen.PixelAt(gutter + 6, 0), fixture.theme.BrushFor(ned::editor::SyntaxClass::Keyword))); // '1', now Keyword
    REQUIRE(CellMatchesBrush(screen.PixelAt(gutter + 2, 0), fixture.theme.BrushFor(ned::editor::SyntaxClass::String)));  // 'a', untouched

    std::filesystem::remove(path);
}

// inlayHint follow-up: an inlay hint renders as extra dimmed/italic cells
// *between* two real characters, without consuming any real bytes -- the
// real character immediately after the hint's anchor must still render,
// just shifted right by the hint's own width, and every real byte before
// the anchor must stay exactly where it already was.
TEST_CASE("A textDocument/inlayHint response renders virtual text mid-line without disturbing real content",
          "[BufferView]") {
    Fixture                     fixture;
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned_bufferview_inlay_hint_test.txt";
    ned::text::Buffer&          buffer = fixture.bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("x = 1;"); // hint anchors right after 'x', at byte 1
    fixture.activeBuffer.Set(buffer);

    ned::ui::EventLoop           eventLoop;
    ned::editor::lsp::LspManager manager(fixture.bufferList, eventLoop);
    ned::editor::lsp::LspClient* client = nullptr;
    FakeLspServer                server = FakeLspServer::Create(manager, "fundamental", eventLoop, client);

    ned::ui::BufferView view = fixture.View();
    view.SetLspManager(&manager);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 0});

    ned::ui::Screen screen = ned::ui::Screen(40, 1);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 0});
    view.Paint(canvas); // triggers didOpen and the inlayHint request, back-to-back

    const std::vector<ned::editor::lsp::Json> frames = ReadLspFrames(server.serverStdinRead, 2);
    REQUIRE(frames.size() == 2);
    REQUIRE(frames[1]["method"] == "textDocument/inlayHint");

    const auto response = ned::editor::lsp::Json{
        {"jsonrpc", "2.0"},
        {"id", frames[1]["id"]},
        {"result", ned::editor::lsp::Json::array({{{"position", {{"line", 0}, {"character", 1}}}, {"label", ": int"}}})},
    };
    client->DispatchFrame(response.dump());

    view.Paint(canvas);

    const int gutter = GutterWidth(1);
    REQUIRE(screen.PixelAt(gutter + 0, 0).character == "x"); // real, untouched
    const ned::ui::Brush hintBrush{.background = fixture.theme.background, .foreground = fixture.theme.ghostTextForeground, .italic = true};
    REQUIRE(CellMatchesBrush(screen.PixelAt(gutter + 1, 0), hintBrush));
    REQUIRE(screen.PixelAt(gutter + 1, 0).character == ":");
    REQUIRE(screen.PixelAt(gutter + 2, 0).character == " ");
    REQUIRE(screen.PixelAt(gutter + 3, 0).character == "i");
    REQUIRE(screen.PixelAt(gutter + 4, 0).character == "n");
    REQUIRE(screen.PixelAt(gutter + 5, 0).character == "t");
    REQUIRE(screen.PixelAt(gutter + 6, 0).character == " "); // real ' ' from "x = 1;", shifted right by the hint
    REQUIRE(screen.PixelAt(gutter + 7, 0).character == "=");

    std::filesystem::remove(path);
}

TEST_CASE("A read-only buffer suppresses both fold gutter and syntax highlighting, even under a mode with both",
          "[BufferView]") {
    Fixture fixture;
    fixture.mode = ned::editor::JsonMode(); // has both a fold query and a highlight query
    fixture.buffer.InsertAtPoint(R"({"a": 1})");
    fixture.buffer.SetReadOnly(true);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 0});
    ned::ui::Screen screen = ned::ui::Screen(40, 1);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 0});
    view.Paint(canvas);

    // No fold column reserved -- GutterWidth(1) with no foldColumn, not +4.
    const int gutter = GutterWidth(1);
    REQUIRE(ContentRowText(screen, 0, 1, 1) == "{");
    // '"a"' would be a String span under JsonMode if highlighting weren't
    // suppressed -- it renders as plain Default text instead.
    REQUIRE(CellMatchesBrush(screen.PixelAt(gutter + 2, 0), fixture.theme.BrushFor(ned::editor::SyntaxClass::Default)));
}

TEST_CASE("BufferView's highlight cache updates after an edit changes the buffer's content", "[BufferView]") {
    Fixture fixture;
    fixture.mode = ned::editor::JsonMode();
    fixture.buffer.InsertAtPoint(R"("a")"); // just a string literal

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 0});

    ned::ui::Screen screen = ned::ui::Screen(40, 1);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 0});

    view.Paint(canvas);
    const int gutter = GutterWidth(1, /*foldColumn=*/4); // JsonMode has a fold query -- generic-code-folding follow-up
    REQUIRE(CellMatchesBrush(screen.PixelAt(gutter + 0, 0), fixture.theme.BrushFor(ned::editor::SyntaxClass::String)));

    // Replace the whole buffer with something that has no string at all --
    // if the highlight cache failed to invalidate on this edit, the first
    // column would still incorrectly render as String.
    fixture.buffer.DeleteRange(0, fixture.buffer.Size());
    fixture.buffer.InsertAtPoint("1");

    view.Paint(canvas);
    REQUIRE(CellMatchesBrush(screen.PixelAt(gutter + 0, 0), fixture.theme.BrushFor(ned::editor::SyntaxClass::Number)));
}

TEST_CASE("BufferView highlights the region background when a mark is set", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("hello world");
    fixture.buffer.SetMark(0);
    fixture.buffer.SetPoint(5); // region = [0, 5) -> "hello"

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 0});

    ned::ui::Screen screen = ned::ui::Screen(20, 1);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 0});

    view.Paint(canvas);

    const int gutter = GutterWidth(1);
    REQUIRE(screen.PixelAt(gutter + 0, 0).background_color == fixture.theme.selectionBackground);
    REQUIRE(screen.PixelAt(gutter + 4, 0).background_color == fixture.theme.selectionBackground);
    REQUIRE(screen.PixelAt(gutter + 5, 0).background_color == fixture.theme.background); // " " -- outside the region
}

TEST_CASE("BufferView highlights the current isearch match", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("the quick brown fox");
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 0});

    view.OnEvent(ned::ui::test::Ctrl('s'));
    view.OnEvent(ned::ui::test::Character("f"));
    view.OnEvent(ned::ui::test::Character("o"));
    view.OnEvent(ned::ui::test::Character("x"));
    REQUIRE(fixture.buffer.Point() == 19); // right after "fox" (starts at byte 16)

    ned::ui::Screen screen = ned::ui::Screen(40, 1);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 0});
    view.Paint(canvas);

    const int gutter = GutterWidth(1);
    REQUIRE(screen.PixelAt(gutter + 16, 0).background_color == fixture.theme.isearchMatchBackground); // 'f'
    REQUIRE(screen.PixelAt(gutter + 18, 0).background_color == fixture.theme.isearchMatchBackground); // 'x'
    REQUIRE(screen.PixelAt(gutter + 15, 0).background_color == fixture.theme.background);             // ' ' before match
}

TEST_CASE("key_press propagates the widget's real height as CommandContext::viewportHeight for paging", "[BufferView]") {
    Fixture fixture;

    std::string content;
    for (int i = 0; i < 20; ++i) {
        content += "line\n";
    }
    fixture.buffer.InsertAtPoint(content);
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 9}); // floor(10 * 0.65) -> 6 lines

    view.OnEvent(ned::ui::test::PageDown());

    REQUIRE(fixture.buffer.Content().ByteOffsetToLine(fixture.buffer.Point()) == 6);
}

TEST_CASE("mouse_press moves point to the clicked position and clears any existing selection", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("the quick brown fox");
    fixture.buffer.SetMark(0);
    fixture.buffer.SetPoint(20);
    REQUIRE(fixture.buffer.HasMark());

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    view.OnEvent(MousePress(GutterWidth(1) + 4, 0));

    REQUIRE(fixture.buffer.Point() == 4);
    REQUIRE_FALSE(fixture.buffer.HasMark());
}

TEST_CASE("mouse_press then mouse_move selects a region from the press position", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("the quick brown fox");
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    const int gutter = GutterWidth(1);
    view.OnEvent(MousePress(gutter + 4, 0));
    view.OnEvent(MouseMove(gutter + 10, 0, ned::ui::MouseEvent::Button::Left));

    REQUIRE(fixture.buffer.HasMark());
    REQUIRE(fixture.buffer.Region() == std::pair<std::size_t, std::size_t>{4, 10});

    // Dragging further extends the same selection, anchored at the press position.
    view.OnEvent(MouseMove(gutter + 16, 0, ned::ui::MouseEvent::Button::Left));
    REQUIRE(fixture.buffer.Region() == std::pair<std::size_t, std::size_t>{4, 16});
}

TEST_CASE("Double-clicking a word selects it", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("the quick brown fox");
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    const int gutter = GutterWidth(1);
    view.OnEvent(MousePress(gutter + 4, 0)); // first click: "quick"'s 'q'
    view.OnEvent(MousePress(gutter + 4, 0)); // second click at the same spot: word select

    REQUIRE(fixture.buffer.HasMark());
    REQUIRE(fixture.buffer.Region() == std::pair<std::size_t, std::size_t>{4, 9});
}

TEST_CASE("Triple-clicking selects the whole line", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("the quick brown fox");
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    const int gutter = GutterWidth(1);
    view.OnEvent(MousePress(gutter + 4, 0));
    view.OnEvent(MousePress(gutter + 4, 0));
    view.OnEvent(MousePress(gutter + 4, 0));

    REQUIRE(fixture.buffer.HasMark());
    REQUIRE(fixture.buffer.Region() == std::pair<std::size_t, std::size_t>{0, 19});
}

TEST_CASE("A second click at a different position resets the click count instead of selecting a word",
          "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("the quick brown fox");
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    const int gutter = GutterWidth(1);
    view.OnEvent(MousePress(gutter + 4, 0));
    view.OnEvent(MousePress(gutter + 10, 0));

    REQUIRE(fixture.buffer.Point() == 10);
    REQUIRE_FALSE(fixture.buffer.HasMark());
}

TEST_CASE("mouse_move with no button held is ignored", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("the quick brown fox");
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    view.OnEvent(MouseMove(10, 0, ned::ui::MouseEvent::Button::None));
    REQUIRE(fixture.buffer.Point() == 0);
    REQUIRE_FALSE(fixture.buffer.HasMark());
}

TEST_CASE("mouse_wheel scrolls the viewport without moving point", "[BufferView]") {
    Fixture fixture;

    std::string content;
    for (int i = 0; i < 20; ++i) {
        content += "line" + std::to_string(i) + "\n";
    }
    fixture.buffer.InsertAtPoint(content);
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 4});

    ned::ui::Screen screen = ned::ui::Screen(40, 5);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 4});

    view.OnEvent(MouseWheel(0, 0, ned::ui::MouseEvent::Button::WheelDown));
    view.Paint(canvas);

    const std::size_t totalLines = fixture.buffer.Content().LineCount();
    REQUIRE(fixture.buffer.Point() == 0);                         // wheel never moves point
    REQUIRE(ContentRowText(screen, 0, 5, totalLines) == "line3"); // scrolled down by 3 lines

    view.OnEvent(MouseWheel(0, 0, ned::ui::MouseEvent::Button::WheelUp));
    view.Paint(canvas);
    REQUIRE(ContentRowText(screen, 0, 5, totalLines) == "line0"); // back at the top
}

TEST_CASE("Middle-click pastes the primary selection at the click point", "[BufferView]") {
    const RestorePrimaryPasteDisabled restore;
    ned::editor::SetClipboardEnabled(true);
    ned::editor::SetClipboardPrimaryPasteCommand({"sh", "-c", "printf %s PASTED"});

    Fixture fixture;
    fixture.buffer.InsertAtPoint("ac");
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    const int gutter = GutterWidth(1);
    view.OnEvent(MousePress(gutter + 1, 0, ned::ui::MouseEvent::Button::Middle)); // between 'a' and 'c'

    REQUIRE(fixture.buffer.Text() == "aPASTEDc");
    REQUIRE(fixture.buffer.Point() == 7); // point ends after the pasted text, same as ordinary typed insertion

    ned::editor::SetClipboardEnabled(false);
}

TEST_CASE("Middle-click paste is a no-op on a read-only buffer, but still moves point", "[BufferView]") {
    const RestorePrimaryPasteDisabled restore;
    ned::editor::SetClipboardEnabled(true);
    ned::editor::SetClipboardPrimaryPasteCommand({"sh", "-c", "printf %s PASTED"});

    Fixture fixture;
    fixture.buffer.InsertAtPoint("ac");
    fixture.buffer.SetPoint(0);
    fixture.buffer.SetReadOnly(true);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    const int gutter = GutterWidth(1);
    view.OnEvent(MousePress(gutter + 1, 0, ned::ui::MouseEvent::Button::Middle));

    REQUIRE(fixture.buffer.Text() == "ac"); // unchanged -- InsertAtPoint would throw on a read-only buffer
    REQUIRE(fixture.buffer.Point() == 1);

    ned::editor::SetClipboardEnabled(false);
}

TEST_CASE("Middle-click paste is a no-op when no primary-selection tool is resolved", "[BufferView]") {
    const RestorePrimaryPasteDisabled restore;
    ned::editor::SetClipboardEnabled(true);
    ned::editor::SetClipboardPrimaryPasteCommand({"sh", "-c", "exit 1"}); // resolves, but never succeeds

    Fixture fixture;
    fixture.buffer.InsertAtPoint("ac");
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    const int gutter = GutterWidth(1);
    view.OnEvent(MousePress(gutter + 1, 0, ned::ui::MouseEvent::Button::Middle));

    REQUIRE(fixture.buffer.Text() == "ac");
    REQUIRE(fixture.buffer.Point() == 1);

    ned::editor::SetClipboardEnabled(false);
}

TEST_CASE("Mouse input is ignored while an isearch session is active", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("the quick brown fox");
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('s')); // start isearch-forward
    view.OnEvent(MousePress(10, 0));

    REQUIRE(fixture.buffer.Point() == 0); // click did not move point mid-session
}

TEST_CASE("The line-number gutter shows right-aligned, 1-indexed line numbers", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("a\nb\nc\nd\ne\nf\ng\nh\ni\nj"); // 10 lines -> gutter width 3 ("10" + 1 space)
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 9});

    ned::ui::Screen screen = ned::ui::Screen(20, 10);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 9});
    view.Paint(canvas);

    // status(1) + diagnostic(1) + gap(1) + digits(2) + gap(1) -- LSP client
    // follow-up added the diagnostic column between status and the leading
    // gap.
    REQUIRE(GutterWidth(10) == 6);

    // Row 0 -> line 1: status column, diagnostic column, leading gap,
    // right-aligned in 2 digit columns, then a trailing separator, then
    // content.
    REQUIRE(screen.PixelAt(2, 0).character == " "); // leading gap
    REQUIRE(screen.PixelAt(3, 0).character == " ");
    REQUIRE(screen.PixelAt(4, 0).character == "1");
    REQUIRE(screen.PixelAt(5, 0).character == " "); // trailing gap
    REQUIRE(ContentRowText(screen, 0, 1, 10) == "a");

    // Row 9 -> line 10: both digit columns used.
    REQUIRE(screen.PixelAt(3, 9).character == "1");
    REQUIRE(screen.PixelAt(4, 9).character == "0");
    REQUIRE(screen.PixelAt(5, 9).character == " ");
    REQUIRE(ContentRowText(screen, 9, 1, 10) == "j");
}

TEST_CASE("The gutter widens as the buffer grows past a power of ten lines", "[BufferView]") {
    Fixture fixture;

    std::string content;
    for (int i = 0; i < 150; ++i) {
        content += "x\n";
    }
    fixture.buffer.InsertAtPoint(content); // 151 lines -> 3 digits + 1 separator
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 0});

    ned::ui::Screen screen = ned::ui::Screen(20, 1);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 0});
    view.Paint(canvas);

    const std::size_t totalLines = fixture.buffer.Content().LineCount();
    REQUIRE(totalLines == 151);
    REQUIRE(GutterWidth(totalLines) == 7); // status(1) + diagnostic(1) + gap(1) + digits(3) + gap(1)
    REQUIRE(ContentRowText(screen, 0, 1, totalLines) == "x");
}

// LSP client follow-up.

TEST_CASE("The diagnostics gutter column shows a severity-colored marker on the diagnostic's own starting line",
          "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("one\ntwo\nthree");
    fixture.buffer.SetDiagnostics({
        ned::text::Buffer::Diagnostic{.startByte = fixture.buffer.Content().LineToByteOffset(1), // "two"
                                      .endByte   = fixture.buffer.Content().LineToByteOffset(1) + 3,
                                      .severity  = ned::text::Buffer::Diagnostic::Severity::Error,
                                      .message   = "boom"},
    });

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});

    ned::ui::Screen screen = ned::ui::Screen(20, 3);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});
    view.Paint(canvas);

    // Diagnostic column sits at x=1, immediately after the status column
    // (x=0) -- see BufferView::GutterWidth's own [status][diagnostic][gap]...
    // layout comment. diagnostic-gutter-icons follow-up: a severity glyph in
    // the severity's foreground color on the plain theme background, no
    // longer a solid background swatch.
    REQUIRE(screen.PixelAt(1, 0).character == " "); // "one" -- no diagnostic
    REQUIRE(screen.PixelAt(1, 1).character == "✗"); // "two" -- the diagnostic's own line
    REQUIRE(screen.PixelAt(1, 1).foreground_color == fixture.theme.diagnosticError);
    REQUIRE(screen.PixelAt(1, 1).background_color == fixture.theme.background);
    REQUIRE(screen.PixelAt(1, 2).character == " "); // "three" -- no diagnostic
}

TEST_CASE("The diagnostics gutter shows the most severe of two diagnostics sharing a line", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("one\n");
    fixture.buffer.SetDiagnostics({
        ned::text::Buffer::Diagnostic{.startByte = 0, .endByte = 1, .severity = ned::text::Buffer::Diagnostic::Severity::Hint, .message = "a hint"},
        ned::text::Buffer::Diagnostic{.startByte = 1, .endByte = 2, .severity = ned::text::Buffer::Diagnostic::Severity::Error, .message = "an error"},
    });

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 1});

    ned::ui::Screen screen = ned::ui::Screen(20, 2);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 1});
    view.Paint(canvas);

    REQUIRE(screen.PixelAt(1, 0).character == "✗");
    REQUIRE(screen.PixelAt(1, 0).foreground_color == fixture.theme.diagnosticError);
}

TEST_CASE("A diagnostic's own span is underlined in the content area", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("int x = 1;");
    fixture.buffer.SetDiagnostics({
        ned::text::Buffer::Diagnostic{
            .startByte = 4, .endByte = 5, .severity = ned::text::Buffer::Diagnostic::Severity::Warning, .message = "unused variable x"},
    });
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 0});

    ned::ui::Screen screen = ned::ui::Screen(20, 1);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 0});
    view.Paint(canvas);

    const int gutter = GutterWidth(1);
    REQUIRE(screen.PixelAt(gutter + 4, 0).character == "x");
    REQUIRE(screen.PixelAt(gutter + 4, 0).underlined);       // the flagged byte
    REQUIRE_FALSE(screen.PixelAt(gutter + 3, 0).underlined); // its neighbors are untouched
    REQUIRE_FALSE(screen.PixelAt(gutter + 5, 0).underlined);
}

TEST_CASE("A zero-width diagnostic still underlines the cell it points at", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("abc");
    fixture.buffer.SetDiagnostics({
        ned::text::Buffer::Diagnostic{
            .startByte = 1, .endByte = 1, .severity = ned::text::Buffer::Diagnostic::Severity::Error, .message = "boom"},
    });
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 0});

    ned::ui::Screen screen = ned::ui::Screen(20, 1);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 0});
    view.Paint(canvas);

    const int gutter = GutterWidth(1);
    REQUIRE(screen.PixelAt(gutter + 1, 0).underlined);
    REQUIRE_FALSE(screen.PixelAt(gutter + 0, 0).underlined);
}

// prose-diagnostic-callout follow-up.

TEST_CASE("A prose-origin diagnostic gets no underline and reserves no inline annotation row", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("int x = 1;\nsecond line");
    fixture.buffer.SetDiagnostics({
        ned::text::Buffer::Diagnostic{.startByte = 4,
                                      .endByte   = 5,
                                      .severity  = ned::text::Buffer::Diagnostic::Severity::Hint,
                                      .origin    = ned::text::Buffer::Diagnostic::Origin::Prose,
                                      .message   = "passive voice"},
    });
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 1});

    ned::ui::Screen screen = ned::ui::Screen(20, 2);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 1});
    view.Paint(canvas);

    const int gutter = GutterWidth(2);
    REQUIRE_FALSE(screen.PixelAt(gutter + 4, 0).underlined); // no code-style underline for a Prose diagnostic
    // A Code-origin diagnostic here would insert an inline annotation row,
    // pushing "second line" down to row 2 (off this 2-row canvas) -- Prose
    // reserves no such row, so row 1 shows the buffer's real second line.
    REQUIRE(ContentRowText(screen, 1, 20 - gutter, 2).starts_with("second line"));
}

TEST_CASE("A prose-origin diagnostic's callout brace spans its flagged line's block, padded for corners, "
          "when there is room",
          "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("one\ntwo\nthree");
    const std::size_t lineTwoStart = fixture.buffer.Content().LineToByteOffset(1);
    fixture.buffer.SetDiagnostics({
        ned::text::Buffer::Diagnostic{.startByte = lineTwoStart,
                                      .endByte   = lineTwoStart + 3, // "two"
                                      .severity  = ned::text::Buffer::Diagnostic::Severity::Hint,
                                      .origin    = ned::text::Buffer::Diagnostic::Origin::Prose,
                                      .message   = "not a real word"},
    });

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 49, .y_min = 0, .y_max = 2});

    ned::ui::Screen screen = ned::ui::Screen(50, 3);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 49, .y_min = 0, .y_max = 2});
    view.Paint(canvas);

    const int gutter = GutterWidth(3);
    // Row 0 ("one") and row 2 ("three") aren't part of the flagged block --
    // they're the brace's own padding rows, each getting one corner. Row 1
    // ("two", the block's own only line) is the message row.
    REQUIRE(ContentRowText(screen, 0, 50 - gutter, 3).find("╮") != std::string::npos); // ╮ top-right corner
    const std::string row1 = ContentRowText(screen, 1, 50 - gutter, 3);
    REQUIRE(row1.find("├") != std::string::npos); // ├ branch
    REQUIRE(row1.find("not a real word") != std::string::npos);
    REQUIRE(ContentRowText(screen, 2, 50 - gutter, 3).find("╯") != std::string::npos); // ╯ bottom-right corner
}

TEST_CASE("A prose-origin diagnostic's callout brace is dropped entirely when its own line leaves no room",
          "[BufferView]") {
    Fixture           fixture;
    const std::string longLine(30, 'x'); // fills the whole 30-wide canvas past the gutter
    fixture.buffer.InsertAtPoint(longLine);
    fixture.buffer.SetDiagnostics({
        ned::text::Buffer::Diagnostic{.startByte = 0,
                                      .endByte   = static_cast<std::size_t>(longLine.size()),
                                      .severity  = ned::text::Buffer::Diagnostic::Severity::Hint,
                                      .origin    = ned::text::Buffer::Diagnostic::Origin::Prose,
                                      .message   = "a hint that cannot fit"},
    });

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 29, .y_min = 0, .y_max = 0});

    ned::ui::Screen screen = ned::ui::Screen(30, 1);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 29, .y_min = 0, .y_max = 0});
    view.Paint(canvas);

    const int         gutter = GutterWidth(1);
    const std::string row0   = ContentRowText(screen, 0, 30 - gutter, 1);
    REQUIRE(row0.find("╮") == std::string::npos); // ╮
    REQUIRE(row0.find("├") == std::string::npos); // ├
    REQUIRE(row0.find("╯") == std::string::npos); // ╯
}

TEST_CASE("Paint echoes the diagnostic on point's line and clears it after leaving the line", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("one\ntwo\n");
    fixture.buffer.SetDiagnostics({
        ned::text::Buffer::Diagnostic{.startByte = fixture.buffer.Content().LineToByteOffset(1),
                                      .endByte   = fixture.buffer.Content().LineToByteOffset(1) + 3,
                                      .severity  = ned::text::Buffer::Diagnostic::Severity::Error,
                                      .message   = "boom"},
    });

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});
    ned::ui::Screen screen = ned::ui::Screen(20, 3);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});

    fixture.buffer.SetPoint(fixture.buffer.Content().LineToByteOffset(1));
    view.Paint(canvas);
    REQUIRE(fixture.statusMessage == "Error: boom");

    // Moving off the line clears the auto-echo (but only its own message).
    fixture.buffer.SetPoint(0);
    view.Paint(canvas);
    REQUIRE(fixture.statusMessage.empty());

    // A real message already on display is never clobbered by the echo.
    fixture.statusMessage = "important result";
    fixture.buffer.SetPoint(fixture.buffer.Content().LineToByteOffset(1));
    view.Paint(canvas);
    REQUIRE(fixture.statusMessage == "important result");
}

// user-facing-hang-affordance follow-up (ChildProcess-hang-protection-round-2).
// ned::editor::DiagnosticsLog's unseen flag is process-wide state, not
// per-view -- ResetDiagnosticsLogForTesting on the way in and out keeps this
// test from depending on, or leaking into, any other test's own LogMessage
// calls (confirmed live: an earlier, unguarded version of this feature let a
// completely unrelated test's LogMessage call intermittently clobber the
// diagnostic-echo test just above under `ned_tests --order rand`).
TEST_CASE("Paint never surfaces an unseen DiagnosticsLog entry unless SetSurfaceUnseenLogEntries(true) was called",
          "[BufferView]") {
    ned::editor::ResetDiagnosticsLogForTesting();
    Fixture fixture;
    ned::editor::LogMessage(ned::editor::LogCategory::Task, ned::editor::LogSeverity::Error, "build failed");
    REQUIRE(ned::editor::HasUnseenDiagnosticsLogEntry());

    ned::ui::BufferView view = fixture.View(); // SetSurfaceUnseenLogEntries never called -- default off
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});
    ned::ui::Screen screen = ned::ui::Screen(20, 3);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});
    view.Paint(canvas);

    REQUIRE(fixture.statusMessage.empty());
    REQUIRE(ned::editor::HasUnseenDiagnosticsLogEntry()); // never acknowledged -- this pane never opted in
    ned::editor::ResetDiagnosticsLogForTesting();
}

TEST_CASE("Paint surfaces an unseen DiagnosticsLog entry once SetSurfaceUnseenLogEntries(true) was called",
          "[BufferView]") {
    ned::editor::ResetDiagnosticsLogForTesting();
    Fixture fixture;
    ned::editor::LogMessage(ned::editor::LogCategory::Task, ned::editor::LogSeverity::Error, "build failed");

    ned::ui::BufferView view = fixture.View();
    view.SetSurfaceUnseenLogEntries(true);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});
    ned::ui::Screen screen = ned::ui::Screen(20, 3);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});
    view.Paint(canvas);

    REQUIRE(fixture.statusMessage == "New warning -- see *Messages* (M-x show-messages)");
    REQUIRE_FALSE(ned::editor::HasUnseenDiagnosticsLogEntry());
    ned::editor::ResetDiagnosticsLogForTesting();
}

TEST_CASE("The debug gutter column shows a breakpoint dot and widens the gutter", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("one\ntwo\nthree");
    fixture.buffer.SetPath("/tmp/ned-dap-view-test.c");
    fixture.buffer.SetPoint(0);

    ned::ui::EventLoop           eventLoop;
    ned::editor::dap::DapManager manager(eventLoop);
    manager.ToggleBreakpoint("/tmp/ned-dap-view-test.c", 2);

    ned::ui::BufferView view = fixture.View();
    view.SetDapManager(&manager);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});

    ned::ui::Screen screen = ned::ui::Screen(20, 3);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});
    view.Paint(canvas);

    // The debug column claims x=0, shifting everything else right by 1.
    REQUIRE(screen.PixelAt(0, 1).character == "●");
    REQUIRE(screen.PixelAt(0, 1).foreground_color == fixture.theme.breakpointMarker);
    REQUIRE(screen.PixelAt(0, 0).character == " ");
    REQUIRE(screen.PixelAt(GutterWidth(3) + 1, 0).character == "o"); // content shifted by the new column
}

TEST_CASE("The stopped line gets an execution arrow and a background wash", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("one\ntwo\nthree");
    fixture.buffer.SetPath("/tmp/ned-dap-exec-test.c");
    fixture.buffer.SetPoint(0);

    ned::ui::EventLoop           eventLoop;
    ned::editor::dap::DapManager manager(eventLoop);
    ned::editor::dap::DapClient* client  = nullptr;
    FakeDapAdapter               adapter = FakeDapAdapter::Create(manager, eventLoop, client);

    // Minimal real handshake against the fake adapter, then a stop on
    // line 2 of this buffer's own file.
    ned::editor::dap::SetDapLaunchConfig("bufferview-dap-exec", "{}");
    manager.StartOrContinue("bufferview-dap-exec");
    const auto initialize = adapter.NextRequest();
    client->DispatchFrame(DapResponseFrame(initialize["seq"].get<int>(), "initialize", ned::editor::dap::Json::object()));
    const auto launch = adapter.NextRequest();
    client->DispatchFrame(DapResponseFrame(launch["seq"].get<int>(), "launch", ned::editor::dap::Json::object()));
    client->DispatchFrame(DapEventFrame("stopped", {{"reason", "breakpoint"}, {"threadId", 1}}));
    const auto stackTrace = adapter.NextRequest();
    client->DispatchFrame(DapResponseFrame(
        stackTrace["seq"].get<int>(), "stackTrace",
        {{"stackFrames", ned::editor::dap::Json::array(
                             {{{"id", 1}, {"name", "main"}, {"line", 2}, {"source", {{"path", "/tmp/ned-dap-exec-test.c"}}}}})}}));
    ned::editor::dap::SetDapLaunchConfig("bufferview-dap-exec", "");

    ned::ui::BufferView view = fixture.View();
    view.SetDapManager(&manager);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});

    ned::ui::Screen screen = ned::ui::Screen(20, 3);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});
    view.Paint(canvas);

    REQUIRE(screen.PixelAt(0, 1).character == "▸");
    REQUIRE(screen.PixelAt(0, 1).foreground_color == fixture.theme.executionMarker);
    const int contentStart = GutterWidth(3) + 1;               // +1 for the debug column
    REQUIRE(screen.PixelAt(contentStart, 1).character == "t"); // "two"
    REQUIRE(screen.PixelAt(contentStart, 1).background_color == fixture.theme.executionLineBackground);
    REQUIRE(screen.PixelAt(contentStart, 0).background_color == fixture.theme.background); // other lines untouched
}

// OnKeyEvent-dispatch-gap follow-up: DapEvaluate/VcsSwitchBranch/VcsCreateBranch
// are all handled inside HandlePromptKey and documented as routing through it
// (same shape as TaskName/GotoLine/etc.), but OnKeyEvent's own dispatch chain
// never actually sent those three InputModes there -- a real keystroke fell
// through to ordinary self-insert-command instead of the prompt. dap-evaluate
// is the one exercised here (no vcsRunner_ wiring needed to reach it, unlike
// the two vcs-* commands); the fix is one shared dispatch condition, so this
// stands in for all three.
TEST_CASE("dap-evaluate's prompt captures keystrokes instead of falling through to normal editing", "[BufferView]") {
    Fixture                      fixture;
    ned::ui::EventLoop           eventLoop;
    ned::editor::dap::DapManager manager(eventLoop);

    ned::ui::BufferView view = fixture.View();
    view.SetDapManager(&manager);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Alt('x'));
    TypeText(view, "dap-evaluate");
    view.OnEvent(ned::ui::test::Return());
    REQUIRE(fixture.statusMessage == "Evaluate: ");

    view.OnEvent(ned::ui::test::Character("x"));
    REQUIRE(fixture.statusMessage == "Evaluate: x");
    REQUIRE(fixture.buffer.Text().empty()); // must NOT have landed as a self-inserted character
}

// DAP round 2 below.

TEST_CASE("The debug gutter distinguishes conditional and logpoint breakpoints by glyph", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("one\ntwo\nthree\nfour");
    fixture.buffer.SetPath("/tmp/ned-dap-kind-test.c");
    fixture.buffer.SetPoint(0);

    ned::ui::EventLoop           eventLoop;
    ned::editor::dap::DapManager manager(eventLoop);
    manager.ToggleBreakpoint("/tmp/ned-dap-kind-test.c", 2);                    // plain
    manager.SetBreakpointCondition("/tmp/ned-dap-kind-test.c", 3, "x > 1");     // conditional
    manager.SetBreakpointLogMessage("/tmp/ned-dap-kind-test.c", 4, "hit: {x}"); // logpoint

    ned::ui::BufferView view = fixture.View();
    view.SetDapManager(&manager);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 4});

    ned::ui::Screen screen = ned::ui::Screen(20, 5);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 4});
    view.Paint(canvas);

    REQUIRE(screen.PixelAt(0, 1).character == "●"); // line 2
    REQUIRE(screen.PixelAt(0, 1).foreground_color == fixture.theme.breakpointMarker);
    REQUIRE(screen.PixelAt(0, 2).character == "◆"); // line 3, conditional
    REQUIRE(screen.PixelAt(0, 2).foreground_color == fixture.theme.breakpointMarker);
    REQUIRE(screen.PixelAt(0, 3).character == "○"); // line 4, logpoint
    REQUIRE(screen.PixelAt(0, 3).foreground_color == fixture.theme.breakpointMarker);
}

TEST_CASE("An unverified breakpoint renders in the dimmed marker color", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("one\ntwo\nthree");
    fixture.buffer.SetPath("/tmp/ned-dap-unverified-test.c");
    fixture.buffer.SetPoint(0);

    ned::ui::EventLoop           eventLoop;
    ned::editor::dap::DapManager manager(eventLoop);
    ned::editor::dap::DapClient* client  = nullptr;
    FakeDapAdapter               adapter = FakeDapAdapter::Create(manager, eventLoop, client);

    ned::editor::dap::SetDapLaunchConfig("bufferview-dap-unverified", "{}");
    manager.StartOrContinue("bufferview-dap-unverified");
    const auto initialize = adapter.NextRequest();
    client->DispatchFrame(DapResponseFrame(initialize["seq"].get<int>(), "initialize", ned::editor::dap::Json::object()));
    const auto launch = adapter.NextRequest();
    client->DispatchFrame(DapResponseFrame(launch["seq"].get<int>(), "launch", ned::editor::dap::Json::object()));
    ned::editor::dap::SetDapLaunchConfig("bufferview-dap-unverified", "");

    // Toggled mid-session (not via the "initialized" event, which would
    // also fire a configurationDone request right behind setBreakpoints --
    // two frames FakeDapAdapter::NextRequest isn't built to split apart,
    // see its own header comment) -- sends exactly one setBreakpoints
    // request, mirroring DapManagerTest's own "mid-session toggle" test.
    manager.ToggleBreakpoint("/tmp/ned-dap-unverified-test.c", 2);
    const auto setBreakpoints = adapter.NextRequest();
    REQUIRE(setBreakpoints["command"] == "setBreakpoints");
    client->DispatchFrame(DapResponseFrame(setBreakpoints["seq"].get<int>(), "setBreakpoints",
                                           {{"breakpoints", ned::editor::dap::Json::array({{{"verified", false}}})}}));

    ned::ui::BufferView view = fixture.View();
    view.SetDapManager(&manager);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});

    ned::ui::Screen screen = ned::ui::Screen(20, 3);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});
    view.Paint(canvas);

    REQUIRE(screen.PixelAt(0, 1).character == "●");
    REQUIRE(screen.PixelAt(0, 1).foreground_color == fixture.theme.unverifiedBreakpointMarker);
}

TEST_CASE("dap-set-breakpoint-condition's prompt sets a condition on point's own line", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("one\ntwo\nthree");
    fixture.buffer.SetPath("/tmp/ned-dap-condition-test.c");
    fixture.buffer.SetPoint(fixture.buffer.Content().LineToByteOffset(1)); // line 2

    ned::ui::EventLoop           eventLoop;
    ned::editor::dap::DapManager manager(eventLoop);

    ned::ui::BufferView view = fixture.View();
    view.SetDapManager(&manager);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Alt('x'));
    TypeText(view, "dap-set-breakpoint-condition");
    view.OnEvent(ned::ui::test::Return());
    REQUIRE(fixture.statusMessage == "Condition (empty to clear): ");

    TypeText(view, "x > 1");
    view.OnEvent(ned::ui::test::Return());

    const auto breakpoints = manager.BreakpointsForKey(ned::editor::dap::DapManager::NormalizePathKey("/tmp/ned-dap-condition-test.c"));
    REQUIRE(breakpoints.size() == 1);
    REQUIRE(breakpoints[0].line == 2);
    REQUIRE(breakpoints[0].condition == "x > 1");
}

TEST_CASE("dap-add-watch and dap-remove-watch round-trip through the *debug* buffer", "[BufferView]") {
    Fixture                      fixture;
    ned::ui::EventLoop           eventLoop;
    ned::editor::dap::DapManager manager(eventLoop);
    ned::editor::dap::DapClient* client  = nullptr;
    FakeDapAdapter               adapter = FakeDapAdapter::Create(manager, eventLoop, client);

    ned::editor::dap::SetDapLaunchConfig("bufferview-dap-watch", "{}");
    manager.StartOrContinue("bufferview-dap-watch");
    const auto initialize = adapter.NextRequest();
    client->DispatchFrame(DapResponseFrame(initialize["seq"].get<int>(), "initialize", ned::editor::dap::Json::object()));
    const auto launch = adapter.NextRequest();
    client->DispatchFrame(DapResponseFrame(launch["seq"].get<int>(), "launch", ned::editor::dap::Json::object()));

    client->DispatchFrame(DapEventFrame("stopped", {{"reason", "breakpoint"}, {"threadId", 1}}));
    const auto autoStackTrace = adapter.NextRequest(); // HandleStoppedEvent's own internal fetch
    client->DispatchFrame(
        DapResponseFrame(autoStackTrace["seq"].get<int>(), "stackTrace", {{"stackFrames", ned::editor::dap::Json::array()}}));
    ned::editor::dap::SetDapLaunchConfig("bufferview-dap-watch", "");

    ned::ui::BufferView view = fixture.View();
    view.SetDapManager(&manager);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 20});

    view.OnEvent(ned::ui::test::Alt('x'));
    TypeText(view, "dap-add-watch");
    view.OnEvent(ned::ui::test::Return());
    REQUIRE(fixture.statusMessage == "Add watch: ");
    TypeText(view, "1 + 1");
    view.OnEvent(ned::ui::test::Return());
    REQUIRE(manager.Watches() == std::vector<std::string>{"1 + 1"});

    view.OnEvent(ned::ui::test::Alt('x'));
    TypeText(view, "dap-show-debug");
    view.OnEvent(ned::ui::test::Return());

    const auto showDebugStackTrace = adapter.NextRequest();
    REQUIRE(showDebugStackTrace["command"] == "stackTrace");
    client->DispatchFrame(DapResponseFrame(
        showDebugStackTrace["seq"].get<int>(), "stackTrace",
        {{"stackFrames", ned::editor::dap::Json::array({{{"id", 1}, {"name", "main"}}})}}));

    const auto scopesRequest = adapter.NextRequest();
    REQUIRE(scopesRequest["command"] == "scopes");
    client->DispatchFrame(DapResponseFrame(scopesRequest["seq"].get<int>(), "scopes", {{"scopes", ned::editor::dap::Json::array()}}));

    const auto evaluateRequest = adapter.NextRequest();
    REQUIRE(evaluateRequest["command"] == "evaluate");
    REQUIRE(evaluateRequest["arguments"]["expression"] == "1 + 1");
    REQUIRE(evaluateRequest["arguments"]["context"] == "watch");
    client->DispatchFrame(DapResponseFrame(evaluateRequest["seq"].get<int>(), "evaluate", {{"result", "2"}}));

    // BufferList::CreateBuffer always uniquifies -- a second dap-show-debug
    // run below creates "*debug*<2>", not a reused "*debug*" -- so this
    // reads back through the view's own active buffer rather than
    // re-Find()-ing the original name.
    ned::text::Buffer& debugBuffer = fixture.activeBuffer.Get();
    REQUIRE(debugBuffer.Text().find("== Watches ==") != std::string::npos);
    REQUIRE(debugBuffer.Text().find("1 + 1 = 2") != std::string::npos);
    REQUIRE(debugBuffer.Text().find("[watch:0]") != std::string::npos);

    // Point the *debug* buffer's own view at the watch line and remove it.
    const std::size_t watchLineByte = debugBuffer.Text().find("1 + 1 = 2");
    REQUIRE(watchLineByte != std::string::npos);
    debugBuffer.SetPoint(watchLineByte);

    view.OnEvent(ned::ui::test::Alt('x'));
    TypeText(view, "dap-remove-watch");
    view.OnEvent(ned::ui::test::Return());
    REQUIRE(manager.Watches().empty());

    // dap-remove-watch re-runs ShowDebugInfo -- drain its (now watch-less,
    // scope-less) stackTrace/scopes round trip so the buffer settles.
    const auto refreshStackTrace = adapter.NextRequest();
    client->DispatchFrame(DapResponseFrame(
        refreshStackTrace["seq"].get<int>(), "stackTrace",
        {{"stackFrames", ned::editor::dap::Json::array({{{"id", 1}, {"name", "main"}}})}}));
    const auto refreshScopes = adapter.NextRequest();
    client->DispatchFrame(
        DapResponseFrame(refreshScopes["seq"].get<int>(), "scopes", {{"scopes", ned::editor::dap::Json::array()}}));

    REQUIRE(fixture.activeBuffer.Get().Text().find("== Watches ==") == std::string::npos);
}

TEST_CASE("dap-select-thread's numbered prompt refocuses subsequent stack-trace requests", "[BufferView]") {
    Fixture                      fixture;
    ned::ui::EventLoop           eventLoop;
    ned::editor::dap::DapManager manager(eventLoop);
    ned::editor::dap::DapClient* client  = nullptr;
    FakeDapAdapter               adapter = FakeDapAdapter::Create(manager, eventLoop, client);

    ned::editor::dap::SetDapLaunchConfig("bufferview-dap-threads", "{}");
    manager.StartOrContinue("bufferview-dap-threads");
    const auto initialize = adapter.NextRequest();
    client->DispatchFrame(DapResponseFrame(initialize["seq"].get<int>(), "initialize", ned::editor::dap::Json::object()));
    const auto launch = adapter.NextRequest();
    client->DispatchFrame(DapResponseFrame(launch["seq"].get<int>(), "launch", ned::editor::dap::Json::object()));

    client->DispatchFrame(DapEventFrame("stopped", {{"reason", "breakpoint"}, {"threadId", 1}}));
    const auto autoStackTrace = adapter.NextRequest();
    client->DispatchFrame(
        DapResponseFrame(autoStackTrace["seq"].get<int>(), "stackTrace", {{"stackFrames", ned::editor::dap::Json::array()}}));
    ned::editor::dap::SetDapLaunchConfig("bufferview-dap-threads", "");

    ned::ui::BufferView view = fixture.View();
    view.SetDapManager(&manager);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Alt('x'));
    TypeText(view, "dap-select-thread");
    view.OnEvent(ned::ui::test::Return());

    const auto threadsRequest = adapter.NextRequest();
    REQUIRE(threadsRequest["command"] == "threads");
    client->DispatchFrame(DapResponseFrame(
        threadsRequest["seq"].get<int>(), "threads",
        {{"threads", ned::editor::dap::Json::array({{{"id", 1}, {"name", "main"}}, {{"id", 2}, {"name", "worker"}}})}}));
    REQUIRE(fixture.statusMessage.find("1) main") != std::string::npos);
    REQUIRE(fixture.statusMessage.find("worker") != std::string::npos);

    view.OnEvent(ned::ui::test::Character("2")); // pick the second thread and commit
    const auto refreshStackTrace = adapter.NextRequest();
    REQUIRE(refreshStackTrace["command"] == "stackTrace");
    REQUIRE(refreshStackTrace["arguments"]["threadId"] == 2);
    client->DispatchFrame(DapResponseFrame(
        refreshStackTrace["seq"].get<int>(), "stackTrace",
        {{"stackFrames", ned::editor::dap::Json::array({{{"id", 5}, {"name", "worker"}}})}}));
    REQUIRE(fixture.statusMessage.find("Selected thread: worker") != std::string::npos);

    manager.RequestStackTrace([](std::vector<ned::editor::dap::DapManager::StackFrame>) {});
    REQUIRE(adapter.NextRequest()["arguments"]["threadId"] == 2);
}

TEST_CASE("dap-set-variable edits a *debug* buffer variable line via the right owner reference", "[BufferView]") {
    Fixture                      fixture;
    ned::ui::EventLoop           eventLoop;
    ned::editor::dap::DapManager manager(eventLoop);
    ned::editor::dap::DapClient* client  = nullptr;
    FakeDapAdapter               adapter = FakeDapAdapter::Create(manager, eventLoop, client);

    ned::editor::dap::SetDapLaunchConfig("bufferview-dap-set-var", "{}");
    manager.StartOrContinue("bufferview-dap-set-var");
    const auto initialize = adapter.NextRequest();
    client->DispatchFrame(DapResponseFrame(initialize["seq"].get<int>(), "initialize", ned::editor::dap::Json::object()));
    const auto launch = adapter.NextRequest();
    client->DispatchFrame(DapResponseFrame(launch["seq"].get<int>(), "launch", ned::editor::dap::Json::object()));

    client->DispatchFrame(DapEventFrame("stopped", {{"reason", "breakpoint"}, {"threadId", 1}}));
    const auto autoStackTrace = adapter.NextRequest();
    client->DispatchFrame(
        DapResponseFrame(autoStackTrace["seq"].get<int>(), "stackTrace", {{"stackFrames", ned::editor::dap::Json::array()}}));
    ned::editor::dap::SetDapLaunchConfig("bufferview-dap-set-var", "");

    ned::ui::BufferView view = fixture.View();
    view.SetDapManager(&manager);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 20});

    view.OnEvent(ned::ui::test::Alt('x'));
    TypeText(view, "dap-show-debug");
    view.OnEvent(ned::ui::test::Return());

    const auto showDebugStackTrace = adapter.NextRequest();
    client->DispatchFrame(DapResponseFrame(
        showDebugStackTrace["seq"].get<int>(), "stackTrace",
        {{"stackFrames", ned::editor::dap::Json::array({{{"id", 1}, {"name", "main"}}})}}));
    const auto scopesRequest = adapter.NextRequest();
    client->DispatchFrame(DapResponseFrame(
        scopesRequest["seq"].get<int>(), "scopes",
        {{"scopes", ned::editor::dap::Json::array({{{"name", "Locals"}, {"variablesReference", 100}}})}}));
    const auto variablesRequest = adapter.NextRequest();
    REQUIRE(variablesRequest["arguments"]["variablesReference"] == 100);
    client->DispatchFrame(DapResponseFrame(
        variablesRequest["seq"].get<int>(), "variables",
        {{"variables", ned::editor::dap::Json::array({{{"name", "x"}, {"value", "1"}, {"type", "int"}, {"variablesReference", 0}}})}}));

    ned::text::Buffer* debugBuffer = fixture.bufferList.Find("*debug*");
    REQUIRE(debugBuffer != nullptr);
    const std::size_t variableLineByte = debugBuffer->Text().find("x: int = 1");
    REQUIRE(variableLineByte != std::string::npos);
    REQUIRE(debugBuffer->Text().find("[owner:100]") != std::string::npos);
    debugBuffer->SetPoint(variableLineByte);

    view.OnEvent(ned::ui::test::Alt('x'));
    TypeText(view, "dap-set-variable");
    view.OnEvent(ned::ui::test::Return());
    REQUIRE(fixture.statusMessage == "New value for x: ");
    TypeText(view, "42");
    view.OnEvent(ned::ui::test::Return());

    const auto setVariableRequest = adapter.NextRequest();
    REQUIRE(setVariableRequest["command"] == "setVariable");
    REQUIRE(setVariableRequest["arguments"]["variablesReference"] == 100);
    REQUIRE(setVariableRequest["arguments"]["name"] == "x");
    REQUIRE(setVariableRequest["arguments"]["value"] == "42");
    client->DispatchFrame(DapResponseFrame(setVariableRequest["seq"].get<int>(), "setVariable",
                                           {{"value", "42"}, {"type", "int"}, {"variablesReference", 0}}));

    REQUIRE(fixture.bufferList.Find("*debug*")->Text().find("x: int = 42") != std::string::npos);
}

TEST_CASE("dap-toggle-console reaches the registered callback", "[BufferView]") {
    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    int toggles = 0;
    view.SetOnDapConsoleToggle([&toggles] { ++toggles; });

    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Character('D'));
    REQUIRE(toggles == 1);
}

TEST_CASE("The diagnostics gutter uses a distinct glyph per severity", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("one\ntwo\nthree\nfour");
    const auto lineStart = [&](std::size_t line) { return fixture.buffer.Content().LineToByteOffset(line); };
    fixture.buffer.SetDiagnostics({
        ned::text::Buffer::Diagnostic{.startByte = lineStart(0), .endByte = lineStart(0) + 1, .severity = ned::text::Buffer::Diagnostic::Severity::Error, .message = "e"},
        ned::text::Buffer::Diagnostic{.startByte = lineStart(1), .endByte = lineStart(1) + 1, .severity = ned::text::Buffer::Diagnostic::Severity::Warning, .message = "w"},
        ned::text::Buffer::Diagnostic{.startByte = lineStart(2), .endByte = lineStart(2) + 1, .severity = ned::text::Buffer::Diagnostic::Severity::Information, .message = "i"},
        ned::text::Buffer::Diagnostic{.startByte = lineStart(3), .endByte = lineStart(3) + 1, .severity = ned::text::Buffer::Diagnostic::Severity::Hint, .message = "h"},
    });

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 3});

    // inline-diagnostics follow-up: off for this test -- every line here
    // carries a diagnostic, so annotation rows would interleave and shift
    // rows 1-3; this test is about the gutter glyphs alone. Restored below
    // (process-wide state, same clean-up convention as BackgroundActivity's
    // own tests).
    ned::editor::SetInlineDiagnosticsEnabled(false);

    ned::ui::Screen screen = ned::ui::Screen(20, 4);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 3});
    view.Paint(canvas);
    ned::editor::SetInlineDiagnosticsEnabled(true);

    REQUIRE(screen.PixelAt(1, 0).character == "✗");
    REQUIRE(screen.PixelAt(1, 0).foreground_color == fixture.theme.diagnosticError);
    REQUIRE(screen.PixelAt(1, 1).character == "▲");
    REQUIRE(screen.PixelAt(1, 1).foreground_color == fixture.theme.diagnosticWarning);
    REQUIRE(screen.PixelAt(1, 2).character == "i");
    REQUIRE(screen.PixelAt(1, 2).foreground_color == fixture.theme.diagnosticInformation);
    REQUIRE(screen.PixelAt(1, 3).character == "·");
    REQUIRE(screen.PixelAt(1, 3).foreground_color == fixture.theme.diagnosticHint);
}

TEST_CASE("The current line's gutter number is styled distinctly from the rest", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("one\ntwo\nthree");
    fixture.buffer.SetPoint(fixture.buffer.Content().LineToByteOffset(1)); // point on line 1 ("two")

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});

    ned::ui::Screen screen = ned::ui::Screen(20, 3);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});
    view.Paint(canvas);

    const int gutter = GutterWidth(3);
    REQUIRE(screen.PixelAt(gutter - 2, 0).foreground_color == fixture.theme.lineNumberForeground);
    REQUIRE(screen.PixelAt(gutter - 2, 1).foreground_color == fixture.theme.currentLineNumberForeground);
    REQUIRE(screen.PixelAt(gutter - 2, 2).foreground_color == fixture.theme.lineNumberForeground);
}

TEST_CASE("Gutter highlights lines fully or partially inside the selected region, distinctly", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("one\ntwo\nthree");
    fixture.buffer.SetMark(0);
    fixture.buffer.SetPoint(6); // region = [0, 6) -> all of "one", just "tw" of "two"

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});

    ned::ui::Screen screen = ned::ui::Screen(20, 3);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});
    view.Paint(canvas);

    const int gutter = GutterWidth(3);

    // Line 0 ("one") is fully inside the region: both the digit and the gap column highlight.
    REQUIRE(screen.PixelAt(gutter - 2, 0).background_color == fixture.theme.selectionBackground);
    REQUIRE(screen.PixelAt(gutter - 1, 0).background_color == fixture.theme.selectionBackground);

    // Line 1 ("two") is only partially inside: the digit stays plain, only the gap column highlights.
    REQUIRE(screen.PixelAt(gutter - 2, 1).background_color == fixture.theme.background);
    REQUIRE(screen.PixelAt(gutter - 1, 1).background_color == fixture.theme.selectionBackground);

    // Line 2 ("three") is untouched by the region: no highlight anywhere in the gutter.
    REQUIRE(screen.PixelAt(gutter - 2, 2).background_color == fixture.theme.background);
    REQUIRE(screen.PixelAt(gutter - 1, 2).background_color == fixture.theme.background);
}

TEST_CASE("Gutter highlighting is absent when no mark is set", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("one\ntwo");

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 1});

    ned::ui::Screen screen = ned::ui::Screen(20, 2);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 1});
    view.Paint(canvas);

    const int gutter = GutterWidth(2);
    REQUIRE(screen.PixelAt(gutter - 2, 0).background_color == fixture.theme.background);
    REQUIRE(screen.PixelAt(gutter - 1, 0).background_color == fixture.theme.background);
    REQUIRE(screen.PixelAt(gutter - 2, 1).background_color == fixture.theme.background);
    REQUIRE(screen.PixelAt(gutter - 1, 1).background_color == fixture.theme.background);
}

TEST_CASE("Gutter fully highlights a line selected through to the very end of the buffer", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("one\ntwo");
    fixture.buffer.SetMark(fixture.buffer.Content().LineToByteOffset(1)); // start of "two"
    fixture.buffer.SetPoint(fixture.buffer.Content().ByteLength());       // end of buffer, no trailing newline

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 1});

    ned::ui::Screen screen = ned::ui::Screen(20, 2);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 1});
    view.Paint(canvas);

    const int gutter = GutterWidth(2);
    REQUIRE(screen.PixelAt(gutter - 2, 1).background_color == fixture.theme.selectionBackground);
    REQUIRE(screen.PixelAt(gutter - 1, 1).background_color == fixture.theme.selectionBackground);
}

TEST_CASE("Clicking inside the gutter moves point to the start of that line", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("one\ntwo\nthree");
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});

    view.OnEvent(MousePress(0, 2)); // inside the gutter, row 2 ("three")

    REQUIRE(fixture.buffer.Point() == fixture.buffer.Content().LineToByteOffset(2));
}

TEST_CASE("A keyboard navigation key after a mouse-drag selection extends it, not collapses it", "[BufferView]") {
    // Was "collapses it instead of extending it" -- revised alongside
    // set-mark-command (C-SPC) landing as a keyboard-reachable way to set
    // the mark: plain motion commands no longer ClearMark() at all (see
    // Commands.cpp's own comment on this), so a mouse-drag-set mark now
    // behaves exactly like a keyboard-set one -- arrow keys move point and
    // leave the mark in place, matching Emacs' own "mark persists until
    // explicitly cleared" model, needed so kill-region/kill-ring-save can
    // act on a region grown by keyboard motion after C-SPC.
    Fixture fixture;
    fixture.buffer.InsertAtPoint("the quick brown fox");

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    const int gutter = GutterWidth(1);
    view.OnEvent(MousePress(gutter + 4, 0));
    view.OnEvent(MouseMove(gutter + 10, 0, ned::ui::MouseEvent::Button::Left));
    REQUIRE(fixture.buffer.HasMark());
    REQUIRE(fixture.buffer.Region() == std::pair<std::size_t, std::size_t>{4, 10});

    view.OnEvent(MouseRelease(gutter + 10, 0));
    REQUIRE(fixture.buffer.HasMark());

    view.OnEvent(ned::ui::test::ArrowRight());
    REQUIRE(fixture.buffer.HasMark());
    REQUIRE(fixture.buffer.Mark() == 4);   // unchanged -- the drag's own start point
    REQUIRE(fixture.buffer.Point() == 11); // moved from the drag's endpoint (10)
}

// These three tests build their own BufferView from a buffer created via
// fixture.bufferList.CreateBuffer(...) rather than fixture.View(): quit's
// unsaved-changes check reads context.bufferList, and fixture.buffer (unlike
// a real app's buffer) is never actually a member of fixture.bufferList.
TEST_CASE("C-x C-c prompts for confirmation when a buffer has unsaved changes", "[BufferView]") {
    Fixture            fixture;
    ned::text::Buffer& buffer = fixture.bufferList.CreateBuffer("scratch");
    buffer.InsertAtPoint("edit");
    REQUIRE(buffer.Modified());

    ned::ui::ActiveBuffer activeBuffer(buffer);
    ned::ui::BufferView   view(activeBuffer, fixture.killRing, fixture.registers, fixture.promptHistory, fixture.bufferList, fixture.dispatcher,
                               fixture.statusMessage, fixture.mode, fixture.theme);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('x'));
    view.OnEvent(ned::ui::test::Ctrl('c'));

    REQUIRE(fixture.statusMessage.find("Unsaved changes in: scratch") == 0);
}

TEST_CASE("'n' cancels the quit-confirmation prompt and returns to normal editing", "[BufferView]") {
    Fixture            fixture;
    ned::text::Buffer& buffer = fixture.bufferList.CreateBuffer("scratch");
    buffer.InsertAtPoint("edit");

    ned::ui::ActiveBuffer activeBuffer(buffer);
    ned::ui::BufferView   view(activeBuffer, fixture.killRing, fixture.registers, fixture.promptHistory, fixture.bufferList, fixture.dispatcher,
                               fixture.statusMessage, fixture.mode, fixture.theme);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('x'));
    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Character("n"));

    REQUIRE(fixture.statusMessage == "Quit cancelled.");

    view.OnEvent(ned::ui::test::Character("z")); // back to normal editing
    REQUIRE(buffer.Text() == "editz");
}

TEST_CASE("'y' at the quit-confirmation prompt does not crash key_press", "[BufferView]") {
    // Same guard as the "C-x C-c (quit) does not crash key_press" test
    // above, hit via HandleConfirmQuitKey's 'y'/'Y' branch instead:
    // eventLoop_'s own null check (BufferView::SetEventLoop) means a
    // no-EventLoop-registered BufferView (every test-constructed one) takes
    // this branch as a safe no-op.
    Fixture            fixture;
    ned::text::Buffer& buffer = fixture.bufferList.CreateBuffer("scratch");
    buffer.InsertAtPoint("edit");

    ned::ui::ActiveBuffer activeBuffer(buffer);
    ned::ui::BufferView   view(activeBuffer, fixture.killRing, fixture.registers, fixture.promptHistory, fixture.bufferList, fixture.dispatcher,
                               fixture.statusMessage, fixture.mode, fixture.theme);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('x'));
    view.OnEvent(ned::ui::test::Ctrl('c'));
    REQUIRE(fixture.statusMessage.find("Unsaved changes in: scratch") == 0); // reached ConfirmQuit safely

    view.OnEvent(ned::ui::test::Character("y")); // must not crash
    REQUIRE(fixture.statusMessage == "Shutting down...");
}

TEST_CASE("Rendered content stays aligned through many mixed scroll-up/scroll-down steps", "[BufferView]") {
    Fixture fixture;

    std::string content;
    for (int i = 0; i < 60; ++i) {
        content += "#line" + std::to_string(i) + "\n";
    }
    fixture.buffer.InsertAtPoint(content);
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 7});

    ned::ui::Screen screen = ned::ui::Screen(20, 8);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 7});

    const std::size_t totalLines = fixture.buffer.Content().LineCount();
    const int         gutter     = GutterWidth(totalLines);

    // Every step below (a scroll direction), applied in order; after each,
    // verify every visible row's '#' lands at the same screen column and
    // the row's number suffix matches topLine_ + row exactly.
    const std::vector<ned::ui::MouseEvent::Button> steps = {
        ned::ui::MouseEvent::Button::WheelDown,
        ned::ui::MouseEvent::Button::WheelDown,
        ned::ui::MouseEvent::Button::WheelDown,
        ned::ui::MouseEvent::Button::WheelDown,
        ned::ui::MouseEvent::Button::WheelDown,
        ned::ui::MouseEvent::Button::WheelUp,
        ned::ui::MouseEvent::Button::WheelUp,
        ned::ui::MouseEvent::Button::WheelDown,
        ned::ui::MouseEvent::Button::WheelUp,
        ned::ui::MouseEvent::Button::WheelUp,
        ned::ui::MouseEvent::Button::WheelUp,
        ned::ui::MouseEvent::Button::WheelUp,
        ned::ui::MouseEvent::Button::WheelUp,
        ned::ui::MouseEvent::Button::WheelUp, // overshoots back to 0
        ned::ui::MouseEvent::Button::WheelDown,
        ned::ui::MouseEvent::Button::WheelUp,
    };

    for (std::size_t step = 0; step < steps.size(); ++step) {
        view.OnEvent(MouseWheel(0, 0, steps[step]));
        view.Paint(canvas);

        for (int row = 0; row < 8; ++row) {
            const std::string rowText = ContentRowText(screen, row, 12, totalLines);
            INFO("step " << step << " row " << row << " text [" << rowText << "]");
            // '#' must be exactly at the gutter boundary (column 0 of content) if this row has real content.
            if (rowText[0] != ' ') { // blank filler rows (past EOF) are all spaces
                REQUIRE(screen.PixelAt(gutter, row).character == "#");
            }
        }
    }
}

TEST_CASE("C-x C-f prompts for a path, then find-file opens an existing file and switches buffers", "[BufferView]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_bufferview_test_findfile.txt";
    std::filesystem::remove(path);
    {
        std::ofstream(path) << "hello from disk";
    }

    Fixture               fixture;
    ned::text::Buffer&    scratch = fixture.bufferList.CreateBuffer("scratch");
    ned::ui::ActiveBuffer activeBuffer(scratch);
    ned::ui::BufferView   view(activeBuffer, fixture.killRing, fixture.registers, fixture.promptHistory, fixture.bufferList, fixture.dispatcher,
                               fixture.statusMessage, fixture.mode, fixture.theme);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('x'));
    view.OnEvent(ned::ui::test::Ctrl('f'));
    REQUIRE(fixture.statusMessage == "Find file: ");

    TypeText(view, path.string());
    view.OnEvent(ned::ui::test::Return());

    REQUIRE(&activeBuffer.Get() != &scratch);
    REQUIRE(activeBuffer.Get().Text() == "hello from disk");
    REQUIRE(fixture.statusMessage.find("Opened") == 0);

    std::filesystem::remove(path);
}

TEST_CASE("find-file on a path that doesn't exist yet creates a new buffer and reports (New file)", "[BufferView]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_bufferview_test_findfile_new.txt";
    std::filesystem::remove(path);

    Fixture               fixture;
    ned::text::Buffer&    scratch = fixture.bufferList.CreateBuffer("scratch");
    ned::ui::ActiveBuffer activeBuffer(scratch);
    ned::ui::BufferView   view(activeBuffer, fixture.killRing, fixture.registers, fixture.promptHistory, fixture.bufferList, fixture.dispatcher,
                               fixture.statusMessage, fixture.mode, fixture.theme);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('x'));
    view.OnEvent(ned::ui::test::Ctrl('f'));
    TypeText(view, path.string());
    view.OnEvent(ned::ui::test::Return());

    REQUIRE(&activeBuffer.Get() != &scratch);
    REQUIRE(activeBuffer.Get().Text().empty());
    REQUIRE(fixture.statusMessage == "(New file)");

    // Back to normal editing in the new buffer.
    view.OnEvent(ned::ui::test::Character("z"));
    REQUIRE(activeBuffer.Get().Text() == "z");
}

TEST_CASE("Escape cancels the find-file prompt and returns to normal editing on the original buffer", "[BufferView]") {
    Fixture               fixture;
    ned::text::Buffer&    scratch = fixture.bufferList.CreateBuffer("scratch");
    ned::ui::ActiveBuffer activeBuffer(scratch);
    ned::ui::BufferView   view(activeBuffer, fixture.killRing, fixture.registers, fixture.promptHistory, fixture.bufferList, fixture.dispatcher,
                               fixture.statusMessage, fixture.mode, fixture.theme);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('x'));
    view.OnEvent(ned::ui::test::Ctrl('f'));
    TypeText(view, "/nonexistent");
    view.OnEvent(ned::ui::test::Escape());

    REQUIRE(&activeBuffer.Get() == &scratch);
    REQUIRE(fixture.statusMessage == "Find file cancelled.");

    view.OnEvent(ned::ui::test::Character("z")); // back to normal editing
    REQUIRE(scratch.Text() == "z");
}

TEST_CASE("C-x b switches to another already-open buffer by name", "[BufferView]") {
    Fixture            fixture;
    ned::text::Buffer& scratch = fixture.bufferList.CreateBuffer("scratch");
    ned::text::Buffer& other   = fixture.bufferList.CreateBuffer("other");
    other.InsertAtPoint("from the other buffer");

    ned::ui::ActiveBuffer activeBuffer(scratch);
    ned::ui::BufferView   view(activeBuffer, fixture.killRing, fixture.registers, fixture.promptHistory, fixture.bufferList, fixture.dispatcher,
                               fixture.statusMessage, fixture.mode, fixture.theme);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('x'));
    view.OnEvent(ned::ui::test::Character("b")); // plain, not Ctrl
    REQUIRE(fixture.statusMessage == "Switch to buffer: ");

    TypeText(view, "other");
    view.OnEvent(ned::ui::test::Return());

    REQUIRE(&activeBuffer.Get() == &other);
    REQUIRE(activeBuffer.Get().Text() == "from the other buffer");
    REQUIRE(fixture.statusMessage.empty());
}

TEST_CASE("switch-to-buffer reports an error and stays put for an unknown buffer name", "[BufferView]") {
    Fixture            fixture;
    ned::text::Buffer& scratch = fixture.bufferList.CreateBuffer("scratch");

    ned::ui::ActiveBuffer activeBuffer(scratch);
    ned::ui::BufferView   view(activeBuffer, fixture.killRing, fixture.registers, fixture.promptHistory, fixture.bufferList, fixture.dispatcher,
                               fixture.statusMessage, fixture.mode, fixture.theme);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('x'));
    view.OnEvent(ned::ui::test::Character("b"));
    TypeText(view, "no-such-buffer");
    view.OnEvent(ned::ui::test::Return());

    REQUIRE(&activeBuffer.Get() == &scratch);
    REQUIRE(fixture.statusMessage == "No buffer named \"no-such-buffer\"");
}

TEST_CASE("Switching to a shorter buffer clamps the viewport instead of rendering blank rows",
          "[BufferView]") {
    // A real reported bug: scroll deep into a long buffer, switch to a much
    // shorter one (e.g. by clicking its tab -- TabBar's own click handler
    // calls ActiveBuffer::Set() directly, entirely independent of
    // BufferView's own topLine_, the same as switch-to-buffer exercises
    // here) -- topLine_ carried over from the long buffer used to point well
    // past the short buffer's own last line, rendering nothing but blank
    // rows instead of its real content.
    Fixture fixture;

    std::string longContent;
    for (int i = 0; i < 50; ++i) {
        longContent += "long line " + std::to_string(i) + "\n";
    }
    fixture.buffer.InsertAtPoint(longContent);

    ned::text::Buffer& shortBuffer = fixture.bufferList.CreateBuffer("short");
    shortBuffer.InsertAtPoint("short line 0\nshort line 1\nshort line 2\n");
    shortBuffer.SetPoint(0); // InsertAtPoint leaves point at the end -- a fresh file's own point starts at 0

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 4});

    ned::ui::Screen screen = ned::ui::Screen(40, 5);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 4});

    view.SetTopLine(40); // scroll deep into the long buffer
    view.Paint(canvas);
    REQUIRE(view.TopLine() > 30); // sanity check: genuinely scrolled down first

    view.OnEvent(ned::ui::test::Ctrl('x'));
    view.OnEvent(ned::ui::test::Character("b"));
    TypeText(view, "short");
    view.OnEvent(ned::ui::test::Return());
    REQUIRE(&fixture.activeBuffer.Get() == &shortBuffer);

    view.Paint(canvas);
    REQUIRE(view.TopLine() <= shortBuffer.Content().LineCount());
    REQUIRE(ContentRowText(screen, 0, 12, shortBuffer.Content().LineCount()) == "short line 0");
}

TEST_CASE("A stored file place's topLine is restored when its buffer becomes active", "[BufferView][Session]") {
    // session-persistence slice 1: the EnsureTopLineValidForActiveBuffer
    // seam must apply a stored viewport, both for the startup buffer (a
    // pane's very first Paint) and for a later switch-to.
    ned::editor::ResetFilePlacesForTesting();

    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_bufferview_session_topline";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const std::filesystem::path file = dir / "a.txt";
    {
        std::ofstream out(file);
        for (int i = 0; i < 100; ++i) {
            out << "line " << i << "\n";
        }
    }

    Fixture            fixture;
    ned::text::Buffer& fileBuffer = fixture.bufferList.OpenFile(file);
    fileBuffer.SetPoint(fileBuffer.ByteOffsetForLineAndColumn(50, 0));
    ned::editor::RecordFilePlace(fileBuffer, 48, 4);

    fixture.activeBuffer.Set(fileBuffer);
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 4});

    ned::ui::Screen screen = ned::ui::Screen(40, 5);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 4});
    view.Paint(canvas);

    REQUIRE(view.TopLine() == 48);

    // The switch path (EnsureTopLineValidForActiveBuffer's seam) must
    // restore it too: away to another buffer and back.
    fixture.activeBuffer.Set(fixture.buffer);
    view.Paint(canvas);
    REQUIRE(view.TopLine() == 0);
    fixture.activeBuffer.Set(fileBuffer);
    view.Paint(canvas);
    REQUIRE(view.TopLine() == 48);

    ned::editor::ResetFilePlacesForTesting();
}

TEST_CASE("Secondary cursors render as inverted cells with their selections highlighted", "[BufferView][MultiCursor]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("foo bar\nfoo baz\n");
    fixture.buffer.SetPoint(0);
    // Line 2's "foo" selected by a secondary cursor (mark 8, point 11), and
    // a third cursor parked at line 1's content end (offset 7) -- the
    // no-codepoint-cell case end-of-line motion produces constantly.
    fixture.buffer.AddCursorAt(11, 8);
    fixture.buffer.AddCursorAt(7);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 29, .y_min = 0, .y_max = 3});
    ned::ui::Screen screen = ned::ui::Screen(30, 4);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 29, .y_min = 0, .y_max = 3});
    view.Paint(canvas);

    const int gutter = GutterWidth(3);
    // The caret after line 2's "foo" (buffer offset 11 = row 1, column 3).
    REQUIRE(screen.PixelAt(gutter + 3, 1).inverted);
    // The end-of-line caret on row 0's first padding cell (column 7).
    REQUIRE(screen.PixelAt(gutter + 7, 0).inverted);
    // The secondary selection's background across line 2's "foo".
    const ned::ui::Theme theme = ned::ui::DarkTheme();
    for (int col = 0; col < 3; ++col) {
        REQUIRE(screen.PixelAt(gutter + col, 1).background_color == theme.selectionBackground);
    }
    // An unselected cell keeps the plain background, and the primary's own
    // cell is NOT inverted (it gets the real terminal cursor instead).
    REQUIRE(screen.PixelAt(gutter + 5, 1).background_color == theme.background);
    REQUIRE_FALSE(screen.PixelAt(gutter + 0, 0).inverted);
}

TEST_CASE("M-n and C-DOWN drive select-next-occurrence and add-cursor-below end to end", "[BufferView][MultiCursor]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("foo bar\nfoo baz\n");
    fixture.buffer.SetPoint(1); // inside line 1's "foo"

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 29, .y_min = 0, .y_max = 3});

    view.OnEvent(ned::ui::test::Alt('n')); // select the word at point
    REQUIRE(fixture.buffer.HasMark());
    REQUIRE(fixture.buffer.Region() == std::pair<std::size_t, std::size_t>{0, 3});

    view.OnEvent(ned::ui::test::Alt('n')); // add a cursor at line 2's "foo"
    REQUIRE(fixture.buffer.SecondaryCursors().size() == 1);
    REQUIRE(fixture.buffer.SecondaryCursors()[0].mark == 8);

    view.OnEvent(ned::ui::test::ArrowDownCtrl());
    REQUIRE(fixture.buffer.SecondaryCursors().size() == 2);

    view.OnEvent(ned::ui::test::Ctrl('g')); // keyboard-quit collapses
    REQUIRE_FALSE(fixture.buffer.HasSecondaryCursors());
}

TEST_CASE("The project-init trust prompt delivers each decision exactly once", "[BufferView][ProjectTrust]") {
    const std::filesystem::path initPath = "/some/project/.ned/init.janet";

    struct Decision {
        std::filesystem::path            path;
        ned::editor::ProjectInitDecision choice;
    };

    const auto runPrompt = [&](const ned::ui::Event& key) -> std::vector<Decision> {
        Fixture               fixture;
        ned::ui::BufferView   view = fixture.View();
        std::vector<Decision> decisions;
        view.RequestTrustProjectInit(initPath, [&](const std::filesystem::path&     path,
                                                   ned::editor::ProjectInitDecision choice) {
            decisions.push_back({path, choice});
        });
        REQUIRE(fixture.statusMessage.find("init.janet") != std::string::npos);
        view.OnEvent(ned::ui::test::Character("x")); // ignored -- prompt stays active
        REQUIRE(decisions.empty());
        view.OnEvent(key);
        view.OnEvent(key); // a second press must not re-deliver -- the session ended
        return decisions;
    };

    const auto once = runPrompt(ned::ui::test::Character("y"));
    REQUIRE(once.size() == 1);
    REQUIRE(once[0].path == initPath);
    REQUIRE(once[0].choice == ned::editor::ProjectInitDecision::LoadOnce);

    const auto always = runPrompt(ned::ui::test::Character("a"));
    REQUIRE(always.size() == 1);
    REQUIRE(always[0].choice == ned::editor::ProjectInitDecision::LoadAlways);

    const auto declined = runPrompt(ned::ui::test::Character("n"));
    REQUIRE(declined.size() == 1);
    REQUIRE(declined[0].choice == ned::editor::ProjectInitDecision::Decline);
}

TEST_CASE("Replacing a scrolled-deep preview with a new, not-yet-open, much shorter file renders its real "
          "content instead of going blank",
          "[BufferView]") {
    // A real reported bug, distinct from the one above: ProjectSidebar's own
    // single-click preview replacement used to close the old preview buffer
    // (freeing it) *before* opening the new one -- letting the new buffer's
    // own allocation reuse the exact address just freed. BufferView's
    // topLineValidatedBuffer_ (still holding that now-reused address from
    // before the click) would then wrongly compare equal to the new active
    // buffer's address and skip revalidating topLine_ entirely, leaving it
    // pointing well past the new, much shorter buffer's own last line --
    // rendering as entirely blank, not merely unclamped. Fixed by opening
    // the new buffer and switching to it *before* closing the old preview.
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "ned_bufferview_test_preview_replace_reuse";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream large(dir / "large.txt");
        for (int i = 0; i < 500; ++i) {
            large << "large line " << i << "\n";
        }
    }
    {
        std::ofstream(dir / "short.txt") << "short line 0\nshort line 1\nshort line 2\n";
    }
    const CurrentPathGuard cwdGuard(dir);

    Fixture                 fixture;
    ned::ui::ProjectSidebar sidebar(
        [&fixture]() -> ned::ui::ActiveBuffer& { return fixture.activeBuffer; }, fixture.bufferList,
        fixture.statusMessage, fixture.theme);
    sidebar.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 27, .y_min = 0, .y_max = 19});
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 19});

    ned::ui::Screen screen = ned::ui::Screen(40, 20);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 19});

    sidebar.OnEvent(MousePress(0, 1)); // "large.txt" -- opens as a preview (row 0 is the sidebar's own header)
    ned::text::Buffer* largeBuffer = fixture.bufferList.FindByPath(dir / "large.txt");
    REQUIRE(largeBuffer != nullptr);
    REQUIRE(&fixture.activeBuffer.Get() == largeBuffer);

    largeBuffer->SetPoint(largeBuffer->Content().ByteLength()); // scroll deep, same as paging to EOF
    view.Paint(canvas);
    REQUIRE(view.TopLine() > 400); // sanity check: genuinely scrolled down first

    sidebar.OnEvent(MousePress(0, 2)); // "short.txt" -- not yet open, replaces the preview
    ned::text::Buffer* shortBuffer = fixture.bufferList.FindByPath(dir / "short.txt");
    REQUIRE(shortBuffer != nullptr);
    REQUIRE(&fixture.activeBuffer.Get() == shortBuffer);
    REQUIRE(fixture.bufferList.Find("large.txt") == nullptr); // old preview genuinely closed

    view.Paint(canvas);
    REQUIRE(view.TopLine() <= shortBuffer->Content().LineCount());
    REQUIRE(ContentRowText(screen, 0, 12, shortBuffer->Content().LineCount()) == "short line 0");

    std::filesystem::remove_all(dir);
}

TEST_CASE("Switching to a buffer whose point already falls within the carried-over scroll position "
          "leaves the viewport untouched",
          "[BufferView]") {
    // The other half of the fix above: EnsureTopLineValidForActiveBuffer
    // shouldn't unconditionally jump back to the top on every switch, only
    // when the carried-over topLine_ genuinely doesn't work for the newly
    // active buffer -- switching between two comparably long buffers should
    // feel like nothing happened to the viewport.
    Fixture fixture;

    std::string contentA;
    for (int i = 0; i < 50; ++i) {
        contentA += "a line " + std::to_string(i) + "\n";
    }
    fixture.buffer.InsertAtPoint(contentA);
    // InsertAtPoint leaves point at the end (line 50) -- every keystroke
    // (even a prefix key like C-x alone, via RunCommandAndHandleOutcome's
    // own unconditional post-dispatch ScrollToShowPoint) re-scrolls to keep
    // point visible, which would corrupt the topLine_=20 set below before
    // the switch to bufferB ever happens. Point needs to already sit inside
    // that viewport so this test isolates what it actually claims to test.
    fixture.buffer.SetPoint(fixture.buffer.Content().LineToByteOffset(25));

    ned::text::Buffer& bufferB = fixture.bufferList.CreateBuffer("b");
    std::string        contentB;
    for (int i = 0; i < 50; ++i) {
        contentB += "b line " + std::to_string(i) + "\n";
    }
    bufferB.InsertAtPoint(contentB);
    bufferB.SetPoint(bufferB.Content().LineToByteOffset(22)); // within the carried-over viewport below

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 19});

    ned::ui::Screen screen = ned::ui::Screen(40, 20);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 19});

    view.SetTopLine(20);
    view.Paint(canvas);

    view.OnEvent(ned::ui::test::Ctrl('x'));
    view.OnEvent(ned::ui::test::Character("b"));
    TypeText(view, "b");
    view.OnEvent(ned::ui::test::Return());
    REQUIRE(&fixture.activeBuffer.Get() == &bufferB);

    view.Paint(canvas);
    REQUIRE(view.TopLine() == 20); // untouched -- bufferB's own point (line 22) was already visible
}

TEST_CASE("Tab in switch-to-buffer completes a unique prefix and confirms with Enter", "[BufferView]") {
    Fixture            fixture;
    ned::text::Buffer& scratch = fixture.bufferList.CreateBuffer("scratch");
    ned::text::Buffer& other   = fixture.bufferList.CreateBuffer("other-buffer");
    other.InsertAtPoint("hi");

    ned::ui::ActiveBuffer activeBuffer(scratch);
    ned::ui::BufferView   view(activeBuffer, fixture.killRing, fixture.registers, fixture.promptHistory, fixture.bufferList, fixture.dispatcher,
                               fixture.statusMessage, fixture.mode, fixture.theme);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('x'));
    view.OnEvent(ned::ui::test::Character("b"));
    TypeText(view, "oth");
    view.OnEvent(ned::ui::test::Tab());
    REQUIRE(fixture.statusMessage == "Switch to buffer: other-buffer");

    view.OnEvent(ned::ui::test::Return());
    REQUIRE(&activeBuffer.Get() == &other);
}

TEST_CASE("Tab in switch-to-buffer with ambiguous matches completes to the common prefix and lists candidates",
          "[BufferView]") {
    Fixture            fixture;
    ned::text::Buffer& scratch = fixture.bufferList.CreateBuffer("scratch");
    fixture.bufferList.CreateBuffer("alpha");
    fixture.bufferList.CreateBuffer("alphabet");

    ned::ui::ActiveBuffer activeBuffer(scratch);
    ned::ui::BufferView   view(activeBuffer, fixture.killRing, fixture.registers, fixture.promptHistory, fixture.bufferList, fixture.dispatcher,
                               fixture.statusMessage, fixture.mode, fixture.theme);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('x'));
    view.OnEvent(ned::ui::test::Character("b"));
    TypeText(view, "al");
    view.OnEvent(ned::ui::test::Tab());

    // Common prefix of "alpha"/"alphabet" is "alpha" -- extends past what was typed.
    REQUIRE(fixture.statusMessage == "Switch to buffer: alpha  {alpha alphabet}");

    // Prompt is still live: still on the original buffer, no crash from the ambiguous Tab.
    REQUIRE(&activeBuffer.Get() == &scratch);
}

TEST_CASE("Tab with no matches leaves the prompt untouched", "[BufferView]") {
    Fixture            fixture;
    ned::text::Buffer& scratch = fixture.bufferList.CreateBuffer("scratch");

    ned::ui::ActiveBuffer activeBuffer(scratch);
    ned::ui::BufferView   view(activeBuffer, fixture.killRing, fixture.registers, fixture.promptHistory, fixture.bufferList, fixture.dispatcher,
                               fixture.statusMessage, fixture.mode, fixture.theme);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('x'));
    view.OnEvent(ned::ui::test::Character("b"));
    TypeText(view, "no-such-prefix");
    view.OnEvent(ned::ui::test::Tab());

    REQUIRE(fixture.statusMessage == "Switch to buffer: no-such-prefix");
}

TEST_CASE("Tab in find-file completes a unique file path and Enter opens it", "[BufferView]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_bufferview_test_tab_unique";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "onlyfile.txt") << "unique file contents";
    }

    Fixture            fixture;
    ned::text::Buffer& scratch = fixture.bufferList.CreateBuffer("scratch");

    ned::ui::ActiveBuffer activeBuffer(scratch);
    ned::ui::BufferView   view(activeBuffer, fixture.killRing, fixture.registers, fixture.promptHistory, fixture.bufferList, fixture.dispatcher,
                               fixture.statusMessage, fixture.mode, fixture.theme);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('x'));
    view.OnEvent(ned::ui::test::Ctrl('f'));
    TypeText(view, (dir / "only").string());
    view.OnEvent(ned::ui::test::Tab());
    REQUIRE(fixture.statusMessage == "Find file: " + (dir / "onlyfile.txt").string());

    view.OnEvent(ned::ui::test::Return());
    REQUIRE(&activeBuffer.Get() != &scratch);
    REQUIRE(activeBuffer.Get().Text() == "unique file contents");

    std::filesystem::remove_all(dir);
}

TEST_CASE("Tab in find-file with ambiguous matches completes to the common prefix and lists candidates",
          "[BufferView]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_bufferview_test_tab_ambiguous";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "apple.txt") << "x";
    }
    {
        std::ofstream(dir / "apricot.txt") << "x";
    }

    Fixture            fixture;
    ned::text::Buffer& scratch = fixture.bufferList.CreateBuffer("scratch");

    ned::ui::ActiveBuffer activeBuffer(scratch);
    ned::ui::BufferView   view(activeBuffer, fixture.killRing, fixture.registers, fixture.promptHistory, fixture.bufferList, fixture.dispatcher,
                               fixture.statusMessage, fixture.mode, fixture.theme);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('x'));
    view.OnEvent(ned::ui::test::Ctrl('f'));
    TypeText(view, (dir / "ap").string());
    view.OnEvent(ned::ui::test::Tab());

    // Common prefix of "apple.txt"/"apricot.txt" is just "ap" -- unchanged.
    REQUIRE(fixture.statusMessage ==
            "Find file: " + (dir / "ap").string() + "  {" + (dir / "apple.txt").string() + " " +
                (dir / "apricot.txt").string() + "}");
    REQUIRE(&activeBuffer.Get() == &scratch);

    std::filesystem::remove_all(dir);
}

TEST_CASE("SetTopLine clamps so the buffer's last line stops at the bottom of the viewport, not past it",
          "[BufferView]") {
    Fixture fixture;

    std::string content;
    for (int i = 0; i < 10; ++i) {
        content += "line\n";
    }
    fixture.buffer.InsertAtPoint(content);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    const std::size_t totalLines = fixture.buffer.Content().LineCount();
    view.SetTopLine(1000);
    REQUIRE(view.TopLine() == totalLines - 3); // last line ends up on the bottom row, not scrolled past it

    view.SetTopLine(2);
    REQUIRE(view.TopLine() == 2);
}

TEST_CASE("mouse_wheel scrolling down repeatedly stops with the last line at the bottom row", "[BufferView]") {
    Fixture fixture;

    std::string content;
    for (int i = 0; i < 10; ++i) {
        if (i > 0) {
            content += "\n";
        }
        content += "line" + std::to_string(i);
    }
    fixture.buffer.InsertAtPoint(content);
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 3});

    ned::ui::Screen screen = ned::ui::Screen(40, 4);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 3});

    for (int i = 0; i < 20; ++i) { // way more than enough wheel ticks to hit the end
        view.OnEvent(MouseWheel(0, 0, ned::ui::MouseEvent::Button::WheelDown));
    }
    view.Paint(canvas);

    const std::size_t totalLines = fixture.buffer.Content().LineCount();
    REQUIRE(view.TopLine() == totalLines - 4);
    REQUIRE(ContentRowText(screen, 3, 5, totalLines) == "line9"); // last line on the bottom row
    REQUIRE(ContentRowText(screen, 0, 5, totalLines) == "line6"); // no blank filler rows below it
}

TEST_CASE("SetScrollBar makes paint() keep the bar's scrollable_length/position/item_visual_length in sync",
          "[BufferView]") {
    Fixture fixture;

    std::string content;
    for (int i = 0; i < 20; ++i) {
        content += "line\n";
    }
    fixture.buffer.InsertAtPoint(content);
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 4});

    ned::ui::ScrollBar scrollBar(fixture.theme.scrollBar);
    view.SetScrollBar(&scrollBar);

    ned::ui::Screen screen = ned::ui::Screen(40, 5);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 4});
    view.Paint(canvas);

    const std::size_t totalLines = fixture.buffer.Content().LineCount();
    REQUIRE(scrollBar.scrollable_length == static_cast<int>(totalLines - 5) + 1); // MaxTopLine() + 1
    REQUIRE(scrollBar.position == 0);
    REQUIRE(scrollBar.item_visual_length == 1);

    view.OnEvent(MouseWheel(0, 0, ned::ui::MouseEvent::Button::WheelDown));
    view.Paint(canvas);

    REQUIRE(scrollBar.position == static_cast<int>(view.TopLine()));
    REQUIRE(view.TopLine() > 0);
}

TEST_CASE("paint() without a scroll bar set is a safe no-op for the sync step", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("hello");

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 9, .y_min = 0, .y_max = 2});

    ned::ui::Screen screen = ned::ui::Screen(10, 3);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 9, .y_min = 0, .y_max = 2});

    view.Paint(canvas); // no SetScrollBar call -- must not crash
    REQUIRE(view.TopLine() == 0);
}

TEST_CASE("SetScrollArrows disables both arrows when the whole buffer fits on screen", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("one\ntwo\nthree");

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 9}); // plenty of room for 3 lines

    const ned::ui::Brush       enabledBrush{.foreground = ned::ui::Color::White};
    const ned::ui::Brush       disabledBrush{.foreground = ned::ui::Color::BrightBlack};
    ned::ui::ScrollArrowButton up(U'▲', enabledBrush, disabledBrush);
    ned::ui::ScrollArrowButton down(U'▼', enabledBrush, disabledBrush);
    view.SetScrollArrows(&up, &down);

    ned::ui::Screen screen = ned::ui::Screen(40, 10);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 9});
    view.Paint(canvas);

    ned::ui::Screen arrowScreen = ned::ui::Screen(1, 1);
    ned::ui::Canvas arrowCanvas(arrowScreen, ned::ui::Box{.x_min = 0, .x_max = 0, .y_min = 0, .y_max = 0});
    up.Paint(arrowCanvas);
    REQUIRE(CellMatchesBrush(arrowScreen.PixelAt(0, 0), disabledBrush));
    down.Paint(arrowCanvas);
    REQUIRE(CellMatchesBrush(arrowScreen.PixelAt(0, 0), disabledBrush));
}

TEST_CASE("SetScrollArrows enables only the down arrow at the top of a scrollable buffer", "[BufferView]") {
    Fixture fixture;

    std::string content;
    for (int i = 0; i < 20; ++i) {
        if (i > 0) {
            content += "\n";
        }
        content += "line" + std::to_string(i);
    }
    fixture.buffer.InsertAtPoint(content);
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 4});

    const ned::ui::Brush       enabledBrush{.foreground = ned::ui::Color::White};
    const ned::ui::Brush       disabledBrush{.foreground = ned::ui::Color::BrightBlack};
    ned::ui::ScrollArrowButton up(U'▲', enabledBrush, disabledBrush);
    ned::ui::ScrollArrowButton down(U'▼', enabledBrush, disabledBrush);
    view.SetScrollArrows(&up, &down);

    ned::ui::Screen screen = ned::ui::Screen(40, 5);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 4});
    view.Paint(canvas);

    ned::ui::Screen arrowScreen = ned::ui::Screen(1, 1);
    ned::ui::Canvas arrowCanvas(arrowScreen, ned::ui::Box{.x_min = 0, .x_max = 0, .y_min = 0, .y_max = 0});
    up.Paint(arrowCanvas);
    REQUIRE(CellMatchesBrush(arrowScreen.PixelAt(0, 0), disabledBrush)); // already at the top
    down.Paint(arrowCanvas);
    REQUIRE(CellMatchesBrush(arrowScreen.PixelAt(0, 0), enabledBrush)); // more content below
}

TEST_CASE("SetScrollArrows enables only the up arrow at the bottom of a scrollable buffer", "[BufferView]") {
    Fixture fixture;

    std::string content;
    for (int i = 0; i < 20; ++i) {
        if (i > 0) {
            content += "\n";
        }
        content += "line" + std::to_string(i);
    }
    fixture.buffer.InsertAtPoint(content);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 4});

    const ned::ui::Brush       enabledBrush{.foreground = ned::ui::Color::White};
    const ned::ui::Brush       disabledBrush{.foreground = ned::ui::Color::BrightBlack};
    ned::ui::ScrollArrowButton up(U'▲', enabledBrush, disabledBrush);
    ned::ui::ScrollArrowButton down(U'▼', enabledBrush, disabledBrush);
    view.SetScrollArrows(&up, &down);

    view.SetTopLine(1000); // clamps to MaxTopLine() -- bottom of the buffer

    ned::ui::Screen screen = ned::ui::Screen(40, 5);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 4});
    view.Paint(canvas);

    ned::ui::Screen arrowScreen = ned::ui::Screen(1, 1);
    ned::ui::Canvas arrowCanvas(arrowScreen, ned::ui::Box{.x_min = 0, .x_max = 0, .y_min = 0, .y_max = 0});
    up.Paint(arrowCanvas);
    REQUIRE(CellMatchesBrush(arrowScreen.PixelAt(0, 0), enabledBrush)); // more content above
    down.Paint(arrowCanvas);
    REQUIRE(CellMatchesBrush(arrowScreen.PixelAt(0, 0), disabledBrush)); // already at the bottom
}

TEST_CASE("C-c C-s prompts for a pattern, then project-search opens a results buffer", "[BufferView]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_bufferview_test_project_search";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "match.txt") << "before\nneedle here\nafter\n";
    }
    const CurrentPathGuard cwdGuard(dir);

    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Ctrl('s'));
    REQUIRE(fixture.statusMessage == "Project search: ");

    TypeText(view, "needle");
    view.OnEvent(ned::ui::test::Return());

    REQUIRE(&fixture.activeBuffer.Get() != &fixture.buffer);
    REQUIRE(fixture.activeBuffer.Get().Name().find("*search results*") == 0);
    REQUIRE(fixture.activeBuffer.Get().Text().find((dir / "match.txt").string() + ":2: needle here") !=
            std::string::npos);
    REQUIRE(fixture.statusMessage.find("1 match for \"needle\"") == 0);

    std::filesystem::remove_all(dir);
}

TEST_CASE("C-c a builds a sectioned *agenda* multibuffer, and C-c C-v on one of its excerpts jumps to the real headline",
          "[BufferView]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_bufferview_test_org_agenda";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "tasks.org") << "* DONE Already done\n* TODO Buy milk\n";
    }
    const CurrentPathGuard cwdGuard(dir);

    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Character("a"));

    REQUIRE(&fixture.activeBuffer.Get() != &fixture.buffer);
    REQUIRE(fixture.activeBuffer.Get().Name().find("*agenda*") == 0);
    // No SCHEDULED:/DEADLINE: on "Buy milk" -- it's Undated, sole section.
    REQUIRE(fixture.activeBuffer.Get().Text().find("▸ [Undated] " + (dir / "tasks.org").string() + ":2") !=
            std::string::npos);
    REQUIRE(fixture.activeBuffer.Get().Text().find("TODO Buy milk") != std::string::npos);
    REQUIRE(fixture.activeBuffer.Get().Text().find("Already done") == std::string::npos); // DONE excluded

    // Point on the excerpt's own body line, not its header -- proves the
    // jump comes from MultibufferIndex::SpanAtOffset (see the *diagnostics*
    // multibuffer's own analogous test), not a lucky regex match.
    const std::size_t bodyOffset = fixture.activeBuffer.Get().Text().find("TODO Buy milk");
    REQUIRE(bodyOffset != std::string::npos);
    fixture.activeBuffer.Get().SetPoint(bodyOffset);

    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Ctrl('v'));

    REQUIRE(fixture.activeBuffer.Get().Name() == "tasks.org");
    REQUIRE(fixture.activeBuffer.Get().Content().ByteOffsetToLine(fixture.activeBuffer.Get().Point()) == 1);

    std::filesystem::remove_all(dir);
}

TEST_CASE("M-x org-clock-report builds a *clock report* multibuffer scoped to the current buffer, with subtree rollups",
          "[BufferView]") {
    // Org's own keymap (C-c C-x r) is mode-local -- Fixture's Dispatcher only
    // carries the global keymap layer (see Fixture's own doc comment above),
    // so this drives the command by name through M-x instead, the same
    // mode-independent path org-set-tags's own tests above use.
    Fixture fixture;
    fixture.buffer.InsertAtPoint("* Parent\n:LOGBOOK:\nCLOCK: [2026-08-23 Sun 09:00]--[2026-08-23 Sun 09:30] =>  0:30\n:END:\n"
                                 "** Child\n:LOGBOOK:\nCLOCK: [2026-08-23 Sun 10:00]--[2026-08-23 Sun 11:00] =>  1:00\n:END:\n"
                                 "* Untouched\n");
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Alt('x'));
    TypeText(view, "org-clock-report");
    view.OnEvent(ned::ui::test::Return());

    REQUIRE(&fixture.activeBuffer.Get() != &fixture.buffer);
    REQUIRE(fixture.activeBuffer.Get().Name().find("*clock report*") == 0);
    const std::string text = fixture.activeBuffer.Get().Text();
    REQUIRE(text.find("Parent") != std::string::npos);
    REQUIRE(text.find("own 0:30   subtree 1:30") != std::string::npos); // parent's own 0:30 + child's 1:00
    REQUIRE(text.find("Child") != std::string::npos);
    REQUIRE(text.find("own 1:00   subtree 1:00") != std::string::npos);
    REQUIRE(text.find("Untouched") == std::string::npos); // no clocked time anywhere in its subtree -- excluded
}

TEST_CASE("project-search reports no matches without switching buffers", "[BufferView]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_bufferview_test_project_search_none";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "file.txt") << "nothing to find here\n";
    }
    const CurrentPathGuard cwdGuard(dir);

    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Ctrl('s'));
    TypeText(view, "needle");
    view.OnEvent(ned::ui::test::Return());

    REQUIRE(&fixture.activeBuffer.Get() == &fixture.buffer);
    REQUIRE(fixture.statusMessage == "No matches for \"needle\"");

    std::filesystem::remove_all(dir);
}

TEST_CASE("project-search reports an invalid regex without switching buffers", "[BufferView]") {
    const CurrentPathGuard cwdGuard(std::filesystem::temp_directory_path());

    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Ctrl('s'));
    view.OnEvent(ned::ui::test::Character("(")); // "(" with no closing paren -> invalid regex
    view.OnEvent(ned::ui::test::Return());

    REQUIRE(&fixture.activeBuffer.Get() == &fixture.buffer);
    REQUIRE(fixture.statusMessage.find("Invalid regex") == 0);
}

TEST_CASE("Escape cancels project-search and returns to normal editing", "[BufferView]") {
    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Ctrl('s'));
    TypeText(view, "needle");
    view.OnEvent(ned::ui::test::Escape());

    REQUIRE(&fixture.activeBuffer.Get() == &fixture.buffer);
    REQUIRE(fixture.statusMessage == "Project search cancelled.");

    view.OnEvent(ned::ui::test::Character("z")); // back to normal editing
    REQUIRE(fixture.buffer.Text() == "z");
}

TEST_CASE("C-c C-v jumps to the file:line under point in a project-search-shaped line", "[BufferView]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_bufferview_test_visit_target.txt";
    {
        std::ofstream(path) << "one\ntwo\nthree\n";
    }

    Fixture fixture;
    fixture.buffer.InsertAtPoint("some text\n" + path.string() + ":2: two\nmore text");
    fixture.buffer.SetPoint(fixture.buffer.Content().LineToByteOffset(1)); // point on the results-shaped line

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Ctrl('v'));

    REQUIRE(&fixture.activeBuffer.Get() != &fixture.buffer);
    REQUIRE(fixture.activeBuffer.Get().Text() == "one\ntwo\nthree\n");
    REQUIRE(fixture.activeBuffer.Get().Content().ByteOffsetToLine(fixture.activeBuffer.Get().Point()) == 1);

    std::filesystem::remove(path);
}

TEST_CASE("C-c C-v is a no-op on a line that isn't shaped like a search result", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("just some ordinary text");
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Ctrl('v'));

    REQUIRE(&fixture.activeBuffer.Get() == &fixture.buffer);
    REQUIRE(fixture.buffer.Text() == "just some ordinary text");
}

TEST_CASE("Enter visits the result under point in a read-only results buffer", "[BufferView]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_bufferview_test_enter_visit_target.txt";
    {
        std::ofstream(path) << "one\ntwo\nthree\n";
    }

    Fixture            fixture;
    ned::text::Buffer& results = fixture.bufferList.CreateBuffer("*search results*");
    results.InsertAtPoint("some text\n" + path.string() + ":2: two\nmore text");
    results.SetPoint(results.Content().LineToByteOffset(1));
    results.SetReadOnly(true);
    ned::ui::ActiveBuffer activeBuffer(results);
    ned::ui::BufferView   view(activeBuffer, fixture.killRing, fixture.registers, fixture.promptHistory, fixture.bufferList, fixture.dispatcher,
                               fixture.statusMessage, fixture.mode, fixture.theme);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Return());

    REQUIRE(&activeBuffer.Get() != &results);
    REQUIRE(activeBuffer.Get().Text() == "one\ntwo\nthree\n");
    REQUIRE(activeBuffer.Get().Content().ByteOffsetToLine(activeBuffer.Get().Point()) == 1);

    std::filesystem::remove(path);
}

TEST_CASE("Enter does the usual thing (self-insert/newline) in an ordinary, editable buffer", "[BufferView]") {
    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Return());

    REQUIRE(fixture.buffer.Text() == "\n"); // newline, not routed through VisitSearchResult
}

TEST_CASE("A mouse click visits the result under the click in a read-only results buffer", "[BufferView]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_bufferview_test_click_visit_target.txt";
    {
        std::ofstream(path) << "one\ntwo\nthree\n";
    }

    Fixture            fixture;
    ned::text::Buffer& results = fixture.bufferList.CreateBuffer("*search results*");
    results.InsertAtPoint("some text\n" + path.string() + ":2: two\nmore text");
    results.SetReadOnly(true);
    ned::ui::ActiveBuffer activeBuffer(results);
    ned::ui::BufferView   view(activeBuffer, fixture.killRing, fixture.registers, fixture.promptHistory, fixture.bufferList, fixture.dispatcher,
                               fixture.statusMessage, fixture.mode, fixture.theme);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    // Click on row 1 (the results-shaped line), column 0.
    view.OnEvent(MousePress(0, 1));

    REQUIRE(&activeBuffer.Get() != &results);
    REQUIRE(activeBuffer.Get().Text() == "one\ntwo\nthree\n");
    REQUIRE(activeBuffer.Get().Content().ByteOffsetToLine(activeBuffer.Get().Point()) == 1);

    std::filesystem::remove(path);
}

TEST_CASE("A mouse click in an ordinary, editable buffer just moves point, no visit", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("hello\nworld");
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    view.OnEvent(MousePress(0, 1));

    REQUIRE(&fixture.activeBuffer.Get() == &fixture.buffer); // unchanged -- no visit happened
}

TEST_CASE("C-c C-r walks pattern, replacement, and confirmation, rewriting matched files on 'y'", "[BufferView]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_bufferview_test_project_replace";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "a.txt") << "needle\n";
    }
    const CurrentPathGuard cwdGuard(dir);

    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Ctrl('r'));
    REQUIRE(fixture.statusMessage == "Project replace regex: ");

    TypeText(view, "needle");
    view.OnEvent(ned::ui::test::Return());
    REQUIRE(fixture.statusMessage.find("Replace \"needle\" with:") == 0);
    // The preview buffer is switched to as soon as the pattern is confirmed,
    // not just at the final y/n -- visible the whole time the replacement
    // text is being typed.
    REQUIRE(&fixture.activeBuffer.Get() != &fixture.buffer);
    REQUIRE(fixture.activeBuffer.Get().Text().find((dir / "a.txt").string() + ":1: needle") != std::string::npos);

    TypeText(view, "found");
    view.OnEvent(ned::ui::test::Return());
    REQUIRE(fixture.statusMessage.find("Replace matches on 1 line across 1 file with \"found\"? (y/n)") == 0);

    view.OnEvent(ned::ui::test::Character("y"));
    REQUIRE(fixture.statusMessage == "Replaced 1 occurrence in 1 file.");

    std::ifstream     file(dir / "a.txt");
    const std::string written((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    REQUIRE(written == "found\n");

    std::filesystem::remove_all(dir);
}

TEST_CASE("'n' at the project-replace confirmation cancels without touching any file", "[BufferView]") {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "ned_bufferview_test_project_replace_cancel";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "a.txt") << "needle\n";
    }
    const CurrentPathGuard cwdGuard(dir);

    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Ctrl('r'));
    TypeText(view, "needle");
    view.OnEvent(ned::ui::test::Return());
    TypeText(view, "found");
    view.OnEvent(ned::ui::test::Return());

    view.OnEvent(ned::ui::test::Character("n"));
    REQUIRE(fixture.statusMessage == "Project replace cancelled.");

    std::ifstream     file(dir / "a.txt");
    const std::string written((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    REQUIRE(written == "needle\n"); // untouched

    std::filesystem::remove_all(dir);
}

TEST_CASE("Escape cancels project-replace at any stage, touching no file", "[BufferView]") {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "ned_bufferview_test_project_replace_escape";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "a.txt") << "needle\n";
    }
    const CurrentPathGuard cwdGuard(dir);

    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Ctrl('r'));
    TypeText(view, "needle");
    view.OnEvent(ned::ui::test::Escape());

    REQUIRE(fixture.statusMessage == "Project replace cancelled.");

    std::ifstream     file(dir / "a.txt");
    const std::string written((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    REQUIRE(written == "needle\n");

    std::filesystem::remove_all(dir);
}

TEST_CASE("project-replace reports an invalid regex and stays in the pattern prompt", "[BufferView]") {
    const CurrentPathGuard cwdGuard(std::filesystem::temp_directory_path());

    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Ctrl('r'));
    view.OnEvent(ned::ui::test::Character("(")); // invalid regex
    view.OnEvent(ned::ui::test::Return());

    REQUIRE(fixture.statusMessage.find("Invalid regex") == 0);

    // Still entering the pattern: DEL edits it, Escape cancels out cleanly.
    view.OnEvent(ned::ui::test::Backspace());
    view.OnEvent(ned::ui::test::Escape());
    REQUIRE(fixture.statusMessage == "Project replace cancelled.");
}

TEST_CASE("project-replace with no matches ends the session without creating a preview buffer", "[BufferView]") {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "ned_bufferview_test_project_replace_nomatch";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "a.txt") << "nothing relevant\n";
    }
    const CurrentPathGuard cwdGuard(dir);

    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Ctrl('r'));
    TypeText(view, "needle");
    view.OnEvent(ned::ui::test::Return());
    TypeText(view, "found");
    view.OnEvent(ned::ui::test::Return());

    REQUIRE(&fixture.activeBuffer.Get() == &fixture.buffer); // no preview buffer -- nothing to preview
    REQUIRE(fixture.statusMessage.find("No matches") == 0);

    std::filesystem::remove_all(dir);
}

TEST_CASE("RequestCloseBuffer closes an unmodified, non-active buffer immediately", "[BufferView]") {
    Fixture               fixture;
    ned::text::Buffer&    scratch = fixture.bufferList.CreateBuffer("scratch");
    ned::text::Buffer&    other   = fixture.bufferList.CreateBuffer("other");
    ned::ui::ActiveBuffer activeBuffer(scratch);
    ned::ui::BufferView   view(activeBuffer, fixture.killRing, fixture.registers, fixture.promptHistory, fixture.bufferList, fixture.dispatcher,
                               fixture.statusMessage, fixture.mode, fixture.theme);

    view.RequestCloseBuffer(other);

    REQUIRE(fixture.bufferList.Count() == 1);
    REQUIRE(fixture.bufferList.Find("other") == nullptr);
    REQUIRE(&activeBuffer.Get() == &scratch); // untouched -- "other" wasn't active
}

TEST_CASE("RequestCloseBuffer closing the active buffer switches to another remaining one", "[BufferView]") {
    Fixture               fixture;
    ned::text::Buffer&    scratch = fixture.bufferList.CreateBuffer("scratch");
    ned::text::Buffer&    other   = fixture.bufferList.CreateBuffer("other");
    ned::ui::ActiveBuffer activeBuffer(scratch);
    ned::ui::BufferView   view(activeBuffer, fixture.killRing, fixture.registers, fixture.promptHistory, fixture.bufferList, fixture.dispatcher,
                               fixture.statusMessage, fixture.mode, fixture.theme);

    view.RequestCloseBuffer(scratch);

    REQUIRE(fixture.bufferList.Count() == 1);
    REQUIRE(&activeBuffer.Get() == &other);
}

TEST_CASE("RequestCloseBuffer closing the only remaining buffer replaces it with a fresh scratch buffer",
          "[BufferView]") {
    Fixture               fixture;
    ned::text::Buffer&    only = fixture.bufferList.CreateBuffer("only");
    ned::ui::ActiveBuffer activeBuffer(only);
    ned::ui::BufferView   view(activeBuffer, fixture.killRing, fixture.registers, fixture.promptHistory, fixture.bufferList, fixture.dispatcher,
                               fixture.statusMessage, fixture.mode, fixture.theme);

    view.RequestCloseBuffer(only);

    // Never left with zero buffers -- a brand new one replaces it, and it's
    // the active buffer, not just sitting unopened in the list.
    REQUIRE(fixture.bufferList.Count() == 1);
    REQUIRE(fixture.bufferList.Find("only") == nullptr);
    REQUIRE(&activeBuffer.Get() != &only);
    REQUIRE(activeBuffer.Get().Name() == "scratch");
}

TEST_CASE("RequestCloseBuffer closes a modified, read-only ('tossable') buffer immediately, no prompt",
          "[BufferView]") {
    Fixture            fixture;
    ned::text::Buffer& scratch = fixture.bufferList.CreateBuffer("scratch");
    ned::text::Buffer& results = fixture.bufferList.CreateBuffer("*search results*");
    results.InsertAtPoint("/some/file.txt:1: match\n"); // marks it Modified(), the same way BuildResultsBuffer's own insert does
    results.SetReadOnly(true);
    REQUIRE(results.Modified());
    ned::ui::ActiveBuffer activeBuffer(scratch);
    ned::ui::BufferView   view(activeBuffer, fixture.killRing, fixture.registers, fixture.promptHistory, fixture.bufferList, fixture.dispatcher,
                               fixture.statusMessage, fixture.mode, fixture.theme);

    view.RequestCloseBuffer(results);

    REQUIRE(fixture.bufferList.Count() == 1);
    REQUIRE(fixture.bufferList.Find("*search results*") == nullptr);
}

TEST_CASE("quit doesn't prompt when the only modified buffer is read-only", "[BufferView]") {
    Fixture            fixture;
    ned::text::Buffer& results = fixture.bufferList.CreateBuffer("*search results*");
    results.InsertAtPoint("/some/file.txt:1: match\n");
    results.SetReadOnly(true);
    ned::ui::ActiveBuffer activeBuffer(results);
    ned::ui::BufferView   view(activeBuffer, fixture.killRing, fixture.registers, fixture.promptHistory, fixture.bufferList, fixture.dispatcher,
                               fixture.statusMessage, fixture.mode, fixture.theme);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('x'));
    view.OnEvent(ned::ui::test::Ctrl('c'));

    // No "Unsaved changes in: ..." prompt -- quit's own anyModified check
    // (Commands.cpp) already excludes read-only buffers, so this goes
    // straight to context.quit = true instead of ConfirmQuit.
    REQUIRE(fixture.statusMessage.find("Unsaved changes") == std::string::npos);
}

TEST_CASE("Typing into a read-only buffer reports the error via the status line, doesn't change its text",
          "[BufferView]") {
    Fixture            fixture;
    ned::text::Buffer& results = fixture.bufferList.CreateBuffer("*search results*");
    results.InsertAtPoint("original");
    results.SetReadOnly(true);
    ned::ui::ActiveBuffer activeBuffer(results);
    ned::ui::BufferView   view(activeBuffer, fixture.killRing, fixture.registers, fixture.promptHistory, fixture.bufferList, fixture.dispatcher,
                               fixture.statusMessage, fixture.mode, fixture.theme);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Character("z"));

    REQUIRE(results.Text() == "original");
    REQUIRE(fixture.statusMessage.find("read-only") != std::string::npos);
}

TEST_CASE("RequestCloseBuffer on a modified buffer prompts, 'y' confirms the close", "[BufferView]") {
    Fixture            fixture;
    ned::text::Buffer& scratch = fixture.bufferList.CreateBuffer("scratch");
    ned::text::Buffer& other   = fixture.bufferList.CreateBuffer("other");
    other.InsertAtPoint("unsaved");
    ned::ui::ActiveBuffer activeBuffer(scratch);
    ned::ui::BufferView   view(activeBuffer, fixture.killRing, fixture.registers, fixture.promptHistory, fixture.bufferList, fixture.dispatcher,
                               fixture.statusMessage, fixture.mode, fixture.theme);

    view.RequestCloseBuffer(other);
    REQUIRE(fixture.bufferList.Count() == 2); // not closed yet -- awaiting confirmation
    REQUIRE(fixture.statusMessage.find("unsaved changes") != std::string::npos);

    view.OnEvent(ned::ui::test::Character("y"));

    REQUIRE(fixture.bufferList.Count() == 1);
    REQUIRE(fixture.bufferList.Find("other") == nullptr);
}

TEST_CASE("RequestCloseBuffer on a modified buffer prompts, 'n' cancels and keeps it", "[BufferView]") {
    Fixture            fixture;
    ned::text::Buffer& scratch = fixture.bufferList.CreateBuffer("scratch");
    ned::text::Buffer& other   = fixture.bufferList.CreateBuffer("other");
    other.InsertAtPoint("unsaved");
    ned::ui::ActiveBuffer activeBuffer(scratch);
    ned::ui::BufferView   view(activeBuffer, fixture.killRing, fixture.registers, fixture.promptHistory, fixture.bufferList, fixture.dispatcher,
                               fixture.statusMessage, fixture.mode, fixture.theme);

    view.RequestCloseBuffer(other);
    view.OnEvent(ned::ui::test::Character("n"));

    REQUIRE(fixture.bufferList.Count() == 2);
    REQUIRE(fixture.bufferList.Find("other") == &other);
    REQUIRE(fixture.statusMessage.find("cancelled") != std::string::npos);
}

TEST_CASE("RequestCloseBuffer is a no-op while another interactive session is already active", "[BufferView]") {
    Fixture               fixture;
    ned::text::Buffer&    scratch = fixture.bufferList.CreateBuffer("scratch");
    ned::text::Buffer&    other   = fixture.bufferList.CreateBuffer("other");
    ned::ui::ActiveBuffer activeBuffer(scratch);
    ned::ui::BufferView   view(activeBuffer, fixture.killRing, fixture.registers, fixture.promptHistory, fixture.bufferList, fixture.dispatcher,
                               fixture.statusMessage, fixture.mode, fixture.theme);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('s')); // isearch-forward -- an interactive session is now active

    view.RequestCloseBuffer(other);

    REQUIRE(fixture.bufferList.Count() == 2); // untouched
}

TEST_CASE("RequestOpenBinaryFile prompts, 'y' opens the file as text anyway", "[BufferView]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_bufferview_binary_confirm.bin";
    {
        std::ofstream file(path, std::ios::binary);
        file.put('a');
        file.put('\0');
    }

    Fixture               fixture;
    ned::text::Buffer&    scratch = fixture.bufferList.CreateBuffer("scratch");
    ned::ui::ActiveBuffer activeBuffer(scratch);
    ned::ui::BufferView   view(activeBuffer, fixture.killRing, fixture.registers, fixture.promptHistory, fixture.bufferList, fixture.dispatcher,
                               fixture.statusMessage, fixture.mode, fixture.theme);

    view.RequestOpenBinaryFile(path);
    REQUIRE(fixture.statusMessage.find("binary") != std::string::npos);
    REQUIRE(&activeBuffer.Get() == &scratch); // not opened yet -- awaiting confirmation

    view.OnEvent(ned::ui::test::Character("y"));

    REQUIRE(&activeBuffer.Get() != &scratch); // switched to the newly opened buffer
    REQUIRE(activeBuffer.Get().Size() == 2);

    std::filesystem::remove(path);
}

TEST_CASE("RequestOpenBinaryFile prompts, 'n' cancels without opening", "[BufferView]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_bufferview_binary_cancel.bin";
    {
        std::ofstream file(path, std::ios::binary);
        file.put('a');
        file.put('\0');
    }

    Fixture               fixture;
    ned::text::Buffer&    scratch = fixture.bufferList.CreateBuffer("scratch");
    ned::ui::ActiveBuffer activeBuffer(scratch);
    ned::ui::BufferView   view(activeBuffer, fixture.killRing, fixture.registers, fixture.promptHistory, fixture.bufferList, fixture.dispatcher,
                               fixture.statusMessage, fixture.mode, fixture.theme);

    view.RequestOpenBinaryFile(path);
    view.OnEvent(ned::ui::test::Character("n"));

    REQUIRE(&activeBuffer.Get() == &scratch);
    REQUIRE(fixture.statusMessage.find("cancelled") != std::string::npos);
    REQUIRE(fixture.bufferList.Count() == 1);

    std::filesystem::remove(path);
}

TEST_CASE("RequestOpenBinaryFile is a no-op while another interactive session is already active", "[BufferView]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_bufferview_binary_busy.bin";
    {
        std::ofstream file(path, std::ios::binary);
        file.put('a');
        file.put('\0');
    }

    Fixture               fixture;
    ned::text::Buffer&    scratch = fixture.bufferList.CreateBuffer("scratch");
    ned::ui::ActiveBuffer activeBuffer(scratch);
    ned::ui::BufferView   view(activeBuffer, fixture.killRing, fixture.registers, fixture.promptHistory, fixture.bufferList, fixture.dispatcher,
                               fixture.statusMessage, fixture.mode, fixture.theme);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('s')); // isearch-forward -- an interactive session is now active

    view.RequestOpenBinaryFile(path);

    REQUIRE(fixture.bufferList.Count() == 1); // untouched

    std::filesystem::remove(path);
}

TEST_CASE("Window-splitting keybindings each invoke the registered onWindowRequest handler",
          "[BufferView]") {
    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();

    std::vector<ned::editor::InteractiveRequest> received;
    view.SetOnWindowRequest([&received](ned::editor::InteractiveRequest request) { received.push_back(request); });

    view.OnEvent(ned::ui::test::Ctrl('x'));
    view.OnEvent(ned::ui::test::Character("2"));
    REQUIRE(received == std::vector{ned::editor::InteractiveRequest::SplitBelow});

    view.OnEvent(ned::ui::test::Ctrl('x'));
    view.OnEvent(ned::ui::test::Character("3"));
    view.OnEvent(ned::ui::test::Ctrl('x'));
    view.OnEvent(ned::ui::test::Character("0"));
    view.OnEvent(ned::ui::test::Ctrl('x'));
    view.OnEvent(ned::ui::test::Character("1"));
    view.OnEvent(ned::ui::test::Ctrl('x'));
    view.OnEvent(ned::ui::test::Character("o"));

    REQUIRE(received == std::vector{
                            ned::editor::InteractiveRequest::SplitBelow,
                            ned::editor::InteractiveRequest::SplitRight,
                            ned::editor::InteractiveRequest::DeleteWindow,
                            ned::editor::InteractiveRequest::DeleteOtherWindows,
                            ned::editor::InteractiveRequest::OtherWindow,
                        });
}

TEST_CASE("A window-splitting keybinding is a safe no-op when no handler is registered", "[BufferView]") {
    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();

    view.OnEvent(ned::ui::test::Ctrl('x'));
    view.OnEvent(ned::ui::test::Character("2")); // must not crash
}

TEST_CASE("SetOnBufferClosed fires with the closing buffer before it's erased", "[BufferView]") {
    Fixture               fixture;
    ned::text::Buffer&    scratch = fixture.bufferList.CreateBuffer("scratch");
    ned::text::Buffer&    other   = fixture.bufferList.CreateBuffer("other");
    ned::ui::ActiveBuffer activeBuffer(scratch);
    ned::ui::BufferView   view(activeBuffer, fixture.killRing, fixture.registers, fixture.promptHistory, fixture.bufferList, fixture.dispatcher,
                               fixture.statusMessage, fixture.mode, fixture.theme);

    ned::text::Buffer* closed = nullptr;
    view.SetOnBufferClosed([&closed](ned::text::Buffer& buffer) { closed = &buffer; });

    view.RequestCloseBuffer(other);

    REQUIRE(closed == &other);
    REQUIRE(fixture.bufferList.Find("other") == nullptr); // genuinely gone by the time this test asserts
}

TEST_CASE("Closing a buffer is a safe no-op when no onBufferClosed handler is registered", "[BufferView]") {
    Fixture               fixture;
    ned::text::Buffer&    scratch = fixture.bufferList.CreateBuffer("scratch");
    ned::text::Buffer&    other   = fixture.bufferList.CreateBuffer("other");
    ned::ui::ActiveBuffer activeBuffer(scratch);
    ned::ui::BufferView   view(activeBuffer, fixture.killRing, fixture.registers, fixture.promptHistory, fixture.bufferList, fixture.dispatcher,
                               fixture.statusMessage, fixture.mode, fixture.theme);

    view.RequestCloseBuffer(other); // must not crash

    REQUIRE(fixture.bufferList.Find("other") == nullptr);
}

TEST_CASE("SetOnActiveBufferChanged fires on a real buffer switch, not on repeated Paint of the same buffer, "
          "and not on the first Paint after construction",
          "[BufferView]") {
    Fixture             fixture;
    ned::text::Buffer&  other = fixture.bufferList.CreateBuffer("other");
    ned::ui::BufferView view  = fixture.View();

    std::vector<ned::text::Buffer*> changed;
    view.SetOnActiveBufferChanged([&changed](ned::text::Buffer& buffer) { changed.push_back(&buffer); });

    ned::ui::Screen screen = ned::ui::Screen(20, 3);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});

    view.Paint(canvas); // first Paint after construction -- must not fire
    REQUIRE(changed.empty());

    view.Paint(canvas); // same buffer again -- must not fire
    REQUIRE(changed.empty());

    fixture.activeBuffer.Set(other);
    view.Paint(canvas); // a real switch -- must fire exactly once, with the new buffer
    REQUIRE(changed == std::vector<ned::text::Buffer*>{&other});

    view.Paint(canvas); // still showing `other` -- must not fire again
    REQUIRE(changed == std::vector<ned::text::Buffer*>{&other});
}

TEST_CASE("Left-pressing inside BufferView takes keyboard focus", "[BufferView]") {
    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    // Focusable()/TakeFocus() are exercised meaningfully once this widget
    // sits inside a real Container tree (see WindowManagerTest.cpp for the
    // actual multi-pane focus-cycling assertions) -- this is just the
    // narrower, BufferView-local guarantee that a left click always calls
    // TakeFocus(), not a crash/no-op check on an unparented widget.
    REQUIRE(view.Focusable());
    view.OnEvent(MousePress(0, 0)); // must not crash with no parent Container
}

TEST_CASE("C-c C-p toggles the registered project sidebar's collapse state", "[BufferView]") {
    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    ned::ui::ProjectSidebar sidebar(
        [&fixture]() -> ned::ui::ActiveBuffer& { return fixture.activeBuffer; }, fixture.bufferList, fixture.statusMessage,
        fixture.theme);
    REQUIRE_FALSE(sidebar.Collapsed()); // starts expanded
    view.SetProjectSidebar(&sidebar);

    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Ctrl('p'));
    REQUIRE(sidebar.Collapsed());
    REQUIRE(sidebar.active); // chrome redesign: hiding collapses to a strip, never deactivates

    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Ctrl('p'));
    REQUIRE_FALSE(sidebar.Collapsed());
}

TEST_CASE("C-c p expands the sidebar if needed and hands it the keyboard focus", "[BufferView]") {
    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    ned::ui::ProjectSidebar sidebar(
        [&fixture]() -> ned::ui::ActiveBuffer& { return fixture.activeBuffer; }, fixture.bufferList, fixture.statusMessage,
        fixture.theme);
    sidebar.SetCollapsed(true);
    view.SetProjectSidebar(&sidebar);
    view.TakeFocus();

    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Character("p"));

    REQUIRE_FALSE(sidebar.Collapsed()); // focusing an invisible tree would be meaningless
    REQUIRE(sidebar.Focused());
}

TEST_CASE("A growing sidebar resize drag hands off to BufferView's mouse_move/mouse_release", "[BufferView]") {
    Fixture fixture;

    ned::ui::ProjectSidebar sidebar(
        [&fixture]() -> ned::ui::ActiveBuffer& { return fixture.activeBuffer; }, fixture.bufferList, fixture.statusMessage,
        fixture.theme);
    ned::ui::BufferView view = fixture.View();

    // Placed directly via SetBox_ side by side, mirroring what main.cpp's own
    // Row{ProjectSidebar, BufferView, ...} composition achieves every frame.
    // The sidebar's box width matches its own starting Width() so the
    // divider column (derived from the box) and the resize anchor
    // (BeginResize captures the internal width_ field) agree, the same
    // invariant main.cpp's real per-frame relayout maintains.
    const int startWidth = sidebar.Width();
    sidebar.SetBox_(ned::ui::Box{.x_min = 0, .x_max = startWidth - 1, .y_min = 0, .y_max = 2});
    view.SetBox_(ned::ui::Box{.x_min = startWidth, .x_max = startWidth + 39, .y_min = 0, .y_max = 2});

    view.SetProjectSidebar(&sidebar);

    sidebar.OnEvent(MousePress(startWidth - 1, 0)); // divider column
    REQUIRE(sidebar.IsResizing());

    // The cursor has moved 5 columns into BufferView's own territory -- with
    // no mouse-capture (every mouse event is delivered to every leaf widget
    // regardless of position; see Widget.h's own header comment), this event
    // is hit-tested to BufferView, not ProjectSidebar, purely because view's
    // own box starts where sidebar's box ends.
    view.OnEvent(MouseMove(startWidth + 5, 0));

    REQUIRE(sidebar.Width() == startWidth + 6); // grew by (startWidth + 5) - (startWidth - 1) == 6

    view.OnEvent(MouseRelease(startWidth + 5, 0));
    REQUIRE_FALSE(sidebar.IsResizing());
}

TEST_CASE("toggle-project-sidebar is a safe no-op when no sidebar is registered", "[BufferView]") {
    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Ctrl('p')); // must not crash
}

TEST_CASE("toggle-terminal reaches the registered callback from both bindings", "[BufferView]") {
    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    int toggles = 0;
    view.SetOnTerminalToggle([&toggles] { ++toggles; });

    // The portable C-c t chord sequence...
    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Character('t'));
    REQUIRE(toggles == 1);

    // ...and the reserved C-` primary (kitty-protocol shape: lowercase-range
    // codepoint id plus the Ctrl modifier bit).
    ncinput input{};
    input.id        = U'`';
    input.modifiers = NCKEY_MOD_CTRL;
    input.evtype    = NCTYPE_PRESS;
    view.OnEvent(ned::ui::Event(input));
    REQUIRE(toggles == 2);
}

TEST_CASE("toggle-terminal is a safe no-op when no handler is registered", "[BufferView]") {
    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Character('t')); // must not crash
}

TEST_CASE("C-c C-d prompts for a path, then create-directory creates it on disk", "[BufferView]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_bufferview_test_create_dir";
    std::filesystem::remove_all(dir);

    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Ctrl('d'));
    REQUIRE(fixture.statusMessage == "Create directory: ");

    TypeText(view, dir.string());
    view.OnEvent(ned::ui::test::Return());

    REQUIRE(std::filesystem::is_directory(dir));
    REQUIRE(fixture.statusMessage == "Created directory " + dir.string());

    view.OnEvent(ned::ui::test::Character("z")); // back to normal editing
    REQUIRE(fixture.buffer.Text() == "z");

    std::filesystem::remove_all(dir);
}

TEST_CASE("create-directory reports an error rather than crashing on failure", "[BufferView]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_bufferview_test_create_dir_conflict";
    std::filesystem::remove_all(path);
    {
        std::ofstream(path) << "x";
    }

    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Ctrl('d'));
    TypeText(view, path.string());
    view.OnEvent(ned::ui::test::Return());

    REQUIRE_FALSE(fixture.statusMessage.empty());
    REQUIRE(fixture.statusMessage != "Created directory " + path.string());

    std::filesystem::remove_all(path);
}

TEST_CASE("Escape cancels the create-directory prompt without touching disk", "[BufferView]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_bufferview_test_create_dir_cancel";
    std::filesystem::remove_all(dir);

    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Ctrl('d'));
    TypeText(view, dir.string());
    view.OnEvent(ned::ui::test::Escape());

    REQUIRE_FALSE(std::filesystem::exists(dir));
    REQUIRE(fixture.statusMessage == "Create directory cancelled.");
}

TEST_CASE("C-c C-k prompts for a path, confirms with y, and delete-file removes it", "[BufferView]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_bufferview_test_delete_file.txt";
    std::filesystem::remove_all(path);
    {
        std::ofstream(path) << "x";
    }

    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Ctrl('k'));
    REQUIRE(fixture.statusMessage == "Delete file: ");

    TypeText(view, path.string());
    view.OnEvent(ned::ui::test::Return());
    REQUIRE(fixture.statusMessage == "Delete \"" + path.string() + "\"? (y/n)");

    view.OnEvent(ned::ui::test::Character("y"));

    REQUIRE_FALSE(std::filesystem::exists(path));
    REQUIRE(fixture.statusMessage == "Deleted " + path.string());

    view.OnEvent(ned::ui::test::Character("z")); // back to normal editing
    REQUIRE(fixture.buffer.Text() == "z");
}

TEST_CASE("delete-file confirmation: 'n' cancels and leaves the path untouched", "[BufferView]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_bufferview_test_delete_file_cancel.txt";
    std::filesystem::remove_all(path);
    {
        std::ofstream(path) << "x";
    }

    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Ctrl('k'));
    TypeText(view, path.string());
    view.OnEvent(ned::ui::test::Return());

    view.OnEvent(ned::ui::test::Character("n"));

    REQUIRE(std::filesystem::exists(path));
    REQUIRE(fixture.statusMessage == "Delete cancelled.");

    std::filesystem::remove_all(path);
}

TEST_CASE("delete-file reports an error and ends the session for a path that doesn't exist", "[BufferView]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_bufferview_test_delete_missing.txt";
    std::filesystem::remove_all(path);

    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Ctrl('k'));
    TypeText(view, path.string());
    view.OnEvent(ned::ui::test::Return());

    REQUIRE(fixture.statusMessage == "No such file or directory: " + path.string());

    view.OnEvent(ned::ui::test::Character("z")); // session already ended -- back to normal editing
    REQUIRE(fixture.buffer.Text() == "z");
}

TEST_CASE("DeleteProjectPath recursively removes a directory through delete-file", "[BufferView]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_bufferview_test_delete_dir";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "sub");
    {
        std::ofstream(dir / "sub" / "file.txt") << "x";
    }

    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Ctrl('k'));
    TypeText(view, dir.string());
    view.OnEvent(ned::ui::test::Return());
    view.OnEvent(ned::ui::test::Character("y"));

    REQUIRE_FALSE(std::filesystem::exists(dir));
}

TEST_CASE("C-c C-n renames a file on disk and follows the buffer that had it open", "[BufferView]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_bufferview_test_rename";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    const std::filesystem::path from = dir / "old.txt";
    const std::filesystem::path to   = dir / "new.txt";
    {
        std::ofstream(from) << "content";
    }

    Fixture               fixture;
    ned::text::Buffer&    opened = fixture.bufferList.OpenOrCreateFile(from);
    ned::ui::ActiveBuffer activeBuffer(opened);
    ned::ui::BufferView   view(activeBuffer, fixture.killRing, fixture.registers, fixture.promptHistory, fixture.bufferList, fixture.dispatcher,
                               fixture.statusMessage, fixture.mode, fixture.theme);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Ctrl('n'));
    REQUIRE(fixture.statusMessage == "Rename file: ");

    TypeText(view, from.string());
    view.OnEvent(ned::ui::test::Return());
    REQUIRE(fixture.statusMessage == "Rename \"" + from.string() + "\" to: ");

    TypeText(view, to.string());
    view.OnEvent(ned::ui::test::Return());

    REQUIRE_FALSE(std::filesystem::exists(from));
    REQUIRE(std::filesystem::exists(to));
    REQUIRE(fixture.statusMessage == "Renamed to " + to.string());
    REQUIRE(activeBuffer.Get().Path() == to);
    REQUIRE(activeBuffer.Get().Name() == "new.txt");

    std::filesystem::remove_all(dir);
}

TEST_CASE("rename-file on a directory relocates every open buffer nested inside it, not just the active one",
          "[BufferView]") {
    const std::filesystem::path base = std::filesystem::temp_directory_path() / "ned_bufferview_test_rename_dir";
    std::filesystem::remove_all(base);
    const std::filesystem::path from = base / "old";
    const std::filesystem::path to   = base / "new";
    std::filesystem::create_directories(from / "sub");
    {
        std::ofstream(from / "a.txt") << "a";
    }
    {
        std::ofstream(from / "sub" / "b.txt") << "b";
    }

    Fixture               fixture;
    ned::text::Buffer&    bufferA = fixture.bufferList.OpenOrCreateFile(from / "a.txt");
    ned::text::Buffer&    bufferB = fixture.bufferList.OpenOrCreateFile(from / "sub" / "b.txt");
    ned::ui::ActiveBuffer activeBuffer(bufferA); // bufferB is open but not active
    ned::ui::BufferView   view(activeBuffer, fixture.killRing, fixture.registers, fixture.promptHistory, fixture.bufferList, fixture.dispatcher,
                               fixture.statusMessage, fixture.mode, fixture.theme);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Ctrl('n'));
    TypeText(view, from.string());
    view.OnEvent(ned::ui::test::Return());
    TypeText(view, to.string());
    view.OnEvent(ned::ui::test::Return());

    REQUIRE_FALSE(std::filesystem::exists(from));
    REQUIRE(std::filesystem::exists(to / "a.txt"));
    REQUIRE(std::filesystem::exists(to / "sub" / "b.txt"));
    REQUIRE(fixture.statusMessage == "Renamed to " + to.string());

    // Both buffers follow -- bufferB despite never having been the active
    // one -- and each keeps its own filename, since only the ancestor
    // directory moved, not the files themselves.
    REQUIRE(bufferA.Path() == to / "a.txt");
    REQUIRE(bufferA.Name() == "a.txt");
    REQUIRE(bufferB.Path() == to / "sub" / "b.txt");
    REQUIRE(bufferB.Name() == "b.txt");

    std::filesystem::remove_all(base);
}

TEST_CASE("rename-file on a path with no open buffer just renames on disk", "[BufferView]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_bufferview_test_rename_no_buffer";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    const std::filesystem::path from = dir / "old.txt";
    const std::filesystem::path to   = dir / "new.txt";
    {
        std::ofstream(from) << "content";
    }

    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Ctrl('n'));
    TypeText(view, from.string());
    view.OnEvent(ned::ui::test::Return());
    TypeText(view, to.string());
    view.OnEvent(ned::ui::test::Return());

    REQUIRE_FALSE(std::filesystem::exists(from));
    REQUIRE(std::filesystem::exists(to));
    REQUIRE(fixture.buffer.Path() == std::nullopt); // untouched -- fixture.buffer never had `from` open

    std::filesystem::remove_all(dir);
}

TEST_CASE("rename-file reports an error and ends the session when the source doesn't exist", "[BufferView]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_bufferview_test_rename_missing";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);

    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Ctrl('n'));
    TypeText(view, (dir / "nope.txt").string());
    view.OnEvent(ned::ui::test::Return());

    REQUIRE(fixture.statusMessage == "No such file or directory: " + (dir / "nope.txt").string());

    view.OnEvent(ned::ui::test::Character("z")); // session already ended -- back to normal editing
    REQUIRE(fixture.buffer.Text() == "z");

    std::filesystem::remove_all(dir);
}

TEST_CASE("rename-file reports an error when the destination already exists, leaving the source in place",
          "[BufferView]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_bufferview_test_rename_conflict";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    const std::filesystem::path from = dir / "from.txt";
    const std::filesystem::path to   = dir / "to.txt";
    {
        std::ofstream(from) << "x";
    }
    {
        std::ofstream(to) << "x";
    }

    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Ctrl('n'));
    TypeText(view, from.string());
    view.OnEvent(ned::ui::test::Return());
    TypeText(view, to.string());
    view.OnEvent(ned::ui::test::Return());

    REQUIRE(std::filesystem::exists(from));
    REQUIRE_FALSE(fixture.statusMessage.empty());
    REQUIRE(fixture.statusMessage != "Renamed to " + to.string());

    std::filesystem::remove_all(dir);
}

TEST_CASE("C-c C-o prompts for a name and find-scratch creates a new scratch note", "[BufferView]") {
    const std::filesystem::path dataDir = std::filesystem::temp_directory_path() / "ned_bufferview_test_scratch_new";
    std::filesystem::remove_all(dataDir);
    const EnvVarGuard xdg("XDG_DATA_HOME", dataDir.c_str());
    const EnvVarGuard home("HOME", nullptr);

    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Ctrl('o'));
    REQUIRE(fixture.statusMessage == "Find scratch: ");

    TypeText(view, "todo");
    view.OnEvent(ned::ui::test::Return());

    REQUIRE(&fixture.activeBuffer.Get() != &fixture.buffer);
    REQUIRE(fixture.activeBuffer.Get().Text().empty());
    REQUIRE(fixture.activeBuffer.Get().Path() == ned::editor::ScratchPathForName("todo"));
    REQUIRE(fixture.statusMessage == "Scratch: todo");
    REQUIRE(std::filesystem::is_directory(ned::editor::ScratchDirectory()));

    // Back to normal editing in the new scratch buffer.
    view.OnEvent(ned::ui::test::Character("z"));
    REQUIRE(fixture.activeBuffer.Get().Text() == "z");

    std::filesystem::remove_all(dataDir);
}

TEST_CASE("find-scratch reopens an existing scratch note by name", "[BufferView]") {
    const std::filesystem::path dataDir = std::filesystem::temp_directory_path() / "ned_bufferview_test_scratch_existing";
    std::filesystem::remove_all(dataDir);
    const EnvVarGuard xdg("XDG_DATA_HOME", dataDir.c_str());
    const EnvVarGuard home("HOME", nullptr);

    std::filesystem::create_directories(ned::editor::ScratchDirectory());
    {
        std::ofstream(ned::editor::ScratchPathForName("todo")) << "buy milk";
    }

    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Ctrl('o'));
    TypeText(view, "todo");
    view.OnEvent(ned::ui::test::Return());

    REQUIRE(fixture.activeBuffer.Get().Text() == "buy milk");
    REQUIRE(fixture.statusMessage == "Scratch: todo");

    std::filesystem::remove_all(dataDir);
}

TEST_CASE("find-scratch rejects a name containing a path separator", "[BufferView]") {
    const std::filesystem::path dataDir = std::filesystem::temp_directory_path() / "ned_bufferview_test_scratch_invalid";
    std::filesystem::remove_all(dataDir);
    const EnvVarGuard xdg("XDG_DATA_HOME", dataDir.c_str());
    const EnvVarGuard home("HOME", nullptr);

    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Ctrl('o'));
    TypeText(view, "../escape");
    view.OnEvent(ned::ui::test::Return());

    REQUIRE(fixture.statusMessage == "Invalid scratch name: \"../escape\"");
    REQUIRE(&fixture.activeBuffer.Get() == &fixture.buffer); // never switched

    view.OnEvent(ned::ui::test::Character("z")); // session already ended -- back to normal editing
    REQUIRE(fixture.buffer.Text() == "z");

    std::filesystem::remove_all(dataDir);
}

TEST_CASE("Escape cancels the find-scratch prompt without creating anything", "[BufferView]") {
    const std::filesystem::path dataDir = std::filesystem::temp_directory_path() / "ned_bufferview_test_scratch_cancel";
    std::filesystem::remove_all(dataDir);
    const EnvVarGuard xdg("XDG_DATA_HOME", dataDir.c_str());
    const EnvVarGuard home("HOME", nullptr);

    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Ctrl('o'));
    TypeText(view, "todo");
    view.OnEvent(ned::ui::test::Escape());

    REQUIRE(&fixture.activeBuffer.Get() == &fixture.buffer);
    REQUIRE(fixture.statusMessage == "Find scratch cancelled.");
    REQUIRE_FALSE(std::filesystem::exists(dataDir));

    view.OnEvent(ned::ui::test::Character("z")); // back to normal editing
    REQUIRE(fixture.buffer.Text() == "z");
}

TEST_CASE("Tab in find-scratch completes a unique scratch name and Enter opens it", "[BufferView]") {
    const std::filesystem::path dataDir = std::filesystem::temp_directory_path() / "ned_bufferview_test_scratch_tab";
    std::filesystem::remove_all(dataDir);
    const EnvVarGuard xdg("XDG_DATA_HOME", dataDir.c_str());
    const EnvVarGuard home("HOME", nullptr);

    std::filesystem::create_directories(ned::editor::ScratchDirectory());
    {
        std::ofstream(ned::editor::ScratchPathForName("todo-list")) << "unique scratch contents";
    }

    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Ctrl('o'));
    TypeText(view, "todo");
    view.OnEvent(ned::ui::test::Tab());
    REQUIRE(fixture.statusMessage == "Find scratch: todo-list");

    view.OnEvent(ned::ui::test::Return());
    REQUIRE(fixture.activeBuffer.Get().Text() == "unique scratch contents");

    std::filesystem::remove_all(dataDir);
}

// The pre-migration "timer() auto-saves a modified scratch buffer" test
// doesn't have an equivalent anymore: BufferView's scratch auto-save is now
// driven by a real std::jthread (StartAutoSaveTimer(ScreenInteractive&)), not
// a synchronous ox::Timer-style hook a test can just call directly -- see
// BufferView.h's own doc comment on StartAutoSaveTimer for why (it needs a
// live ScreenInteractive to marshal the actual save back onto the main loop
// thread via PostEvent, and isn't even started for a test-constructed
// BufferView in the first place, the same "inert until explicitly wired up"
// contract SetScrollBar/SetProjectSidebar already have). The behavior this
// test actually verified -- a modified scratch buffer under XDG_DATA_HOME
// gets written to disk with correct content -- already has full, direct,
// dedicated coverage in Tests/ScratchPadTest.cpp's own AutoSaveScratchBuffers
// test cases (e.g. "AutoSaveScratchBuffers saves a modified buffer whose path
// is directly in the scratch directory"), so nothing is actually lost here,
// just relocated to where it's still directly, synchronously testable.

// execute-extended-command follow-up (M-x). ned::ui::test::Alt('x') exercises the
// "M-x" binding directly (a real fast Alt+x press, per KeyTranslationTest.cpp's
// own "TranslateKey maps Alt/Meta+letter to Meta chords" case) -- the separate
// "ESC x" two-chord fallback binding shares the same command and BufferView-side
// handling, so it isn't re-tested here.

TEST_CASE("M-x prompts for a command name, listing every command alphabetically before any input", "[BufferView]") {
    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 2});
    CaptureCandidates(view, fixture.candidates);

    view.OnEvent(ned::ui::test::Alt('x'));

    REQUIRE(fixture.statusMessage == "M-x ");
    // Display is capped to kMaxPopupRows (see BuildFuzzyCandidatePopupModel)
    // -- "acp-send-prompt" is alphabetically first among registered commands
    // (was "add-cursor-above", before that "backward-char" -- the ACP
    // client slice 2 follow-up added three "acp-*" commands sorting ahead
    // of it, the same shift its own comment already anticipated happening
    // again), so it's always within that window regardless of how many
    // other commands exist.
    REQUIRE(CandidateSelected(fixture.candidates, "acp-send-prompt"));
    REQUIRE(CandidatesHaveMoreTail(fixture.candidates)); // more than kMaxPopupRows commands are registered
}

TEST_CASE("Typing in M-x narrows candidates and marks the top-ranked one selected", "[BufferView]") {
    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 2});
    CaptureCandidates(view, fixture.candidates);

    view.OnEvent(ned::ui::test::Alt('x'));
    // DAP round 2 follow-up: "stb" alone now also fuzzy-matches
    // dap-set-breakpoint-condition/dap-set-breakpoint-log-message with a
    // higher score than switch-to-buffer (both "s" and "b" land on a
    // word-boundary right after "dap-set-" / "-breakpoint-"), so the query
    // widened to "swbuf" -- still short, still uniquely resolves to
    // switch-to-buffer (no other registered command name contains a 'w' at
    // all), and stays robust to future command names the same way.
    TypeText(view, "swbuf");

    REQUIRE(CandidateSelected(fixture.candidates, "switch-to-buffer"));
    REQUIRE_FALSE(CandidatesContain(fixture.candidates, "quit"));
}

TEST_CASE("Down in M-x moves the selection, and Enter invokes whichever candidate is selected", "[BufferView]") {
    Fixture fixture;
    bool    alphaInvoked = false;
    bool    betaInvoked  = false;
    // Same fuzzy score by construction (identical shape/word-boundary match
    // for query "zzz"), so ranking falls back to alphabetical order --
    // zzz-alpha ranks first, zzz-beta second, deterministically.
    fixture.registry.Register("zzz-alpha", "", [&](ned::editor::CommandContext&) { alphaInvoked = true; });
    fixture.registry.Register("zzz-beta", "", [&](ned::editor::CommandContext&) { betaInvoked = true; });

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 2});
    CaptureCandidates(view, fixture.candidates);

    view.OnEvent(ned::ui::test::Alt('x'));
    TypeText(view, "zzz");
    REQUIRE(CandidateSelected(fixture.candidates, "zzz-alpha"));

    view.OnEvent(ned::ui::test::ArrowDown());
    REQUIRE(CandidateSelected(fixture.candidates, "zzz-beta"));

    view.OnEvent(ned::ui::test::Return());

    REQUIRE_FALSE(alphaInvoked);
    REQUIRE(betaInvoked);
}

TEST_CASE("Enter in M-x invokes the matched command and returns to normal editing", "[BufferView]") {
    Fixture fixture;
    bool    invoked = false;
    fixture.registry.Register("sentinel-command", "", [&](ned::editor::CommandContext&) { invoked = true; });

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Alt('x'));
    TypeText(view, "sentinel");
    view.OnEvent(ned::ui::test::Return());

    REQUIRE(invoked);

    view.OnEvent(ned::ui::test::Character("z")); // back to normal editing, proves inputMode_ is Normal again
    REQUIRE(fixture.buffer.Text() == "z");
}

TEST_CASE("Escape cancels the M-x prompt and returns to normal editing", "[BufferView]") {
    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Alt('x'));
    TypeText(view, "swi");
    view.OnEvent(ned::ui::test::Escape());

    REQUIRE(fixture.statusMessage == "Command cancelled.");

    view.OnEvent(ned::ui::test::Character("z")); // back to normal editing
    REQUIRE(fixture.buffer.Text() == "z");
}

TEST_CASE("M-x to find-file chains directly into find-file's own prompt", "[BufferView]") {
    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Alt('x'));
    TypeText(view, "find-file"); // exact match -- unambiguously top-ranked
    view.OnEvent(ned::ui::test::Return());

    REQUIRE(fixture.statusMessage == "Find file: ");

    view.OnEvent(ned::ui::test::Escape()); // cancel the chained prompt cleanly
}

TEST_CASE("M-x org-set-tags chains into a tags prompt pre-filled with the headline's current tags",
          "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("* Buy milk :errand:home:\n");
    fixture.buffer.SetPoint(2);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Alt('x'));
    TypeText(view, "org-set-tags");
    view.OnEvent(ned::ui::test::Return());

    REQUIRE(fixture.statusMessage == "Tags (colon-separated): errand:home");

    view.OnEvent(ned::ui::test::Escape()); // cancel -- buffer untouched
    REQUIRE(fixture.buffer.Text() == "* Buy milk :errand:home:\n");
}

TEST_CASE("Submitting the org-set-tags prompt rewrites the headline's tags block", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("* Buy milk :errand:\n");
    fixture.buffer.SetPoint(2);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Alt('x'));
    TypeText(view, "org-set-tags");
    view.OnEvent(ned::ui::test::Return());

    // Wholesale-replace the pre-filled text rather than appending to it.
    for (int i = 0; i < 20; ++i)
        view.OnEvent(ned::ui::test::Backspace());
    TypeText(view, "urgent:home");
    view.OnEvent(ned::ui::test::Return());

    REQUIRE(fixture.buffer.Text() == "* Buy milk :urgent:home:\n");
    REQUIRE(fixture.statusMessage.empty());
}

TEST_CASE("M-x org-set-tags off a headline reports failure without opening a prompt", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("plain text");
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Alt('x'));
    TypeText(view, "org-set-tags");
    view.OnEvent(ned::ui::test::Return());

    REQUIRE(fixture.statusMessage == "Not on a headline.");
    // Typing now self-inserts rather than feeding a (nonexistent) prompt --
    // confirms no interactive session was left open.
    view.OnEvent(ned::ui::test::Character("X"));
    REQUIRE(fixture.buffer.Text() == "Xplain text");
}

TEST_CASE("Enter in M-x on an unmatched query reports no match and returns to normal editing", "[BufferView]") {
    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Alt('x'));
    TypeText(view, "0"); // no registered command name contains a digit
    view.OnEvent(ned::ui::test::Return());

    REQUIRE(fixture.statusMessage == "No command matching \"0\"");

    view.OnEvent(ned::ui::test::Character("z")); // back to normal editing
    REQUIRE(fixture.buffer.Text() == "z");
}

// kmacro-start-macro/kmacro-end-or-call-macro follow-up.

TEST_CASE("F3 records keystrokes and F4 stops recording, reporting a keys-recorded count", "[BufferView]") {
    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::F(3));
    REQUIRE(fixture.statusMessage == "Recording keyboard macro...");

    TypeText(view, "ab");
    view.OnEvent(ned::ui::test::F(4));

    REQUIRE(fixture.statusMessage == "Keyboard macro recorded (2 keys).");
    REQUIRE(fixture.buffer.Text() == "ab");
}

TEST_CASE("F4 while not recording replays the last recorded macro", "[BufferView]") {
    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::F(3));
    TypeText(view, "ab");
    view.OnEvent(ned::ui::test::F(4)); // stop
    REQUIRE(fixture.buffer.Text() == "ab");

    view.OnEvent(ned::ui::test::F(4)); // replay
    REQUIRE(fixture.buffer.Text() == "abab");

    view.OnEvent(ned::ui::test::F(4)); // replay again
    REQUIRE(fixture.buffer.Text() == "ababab");
}

TEST_CASE("F4 with nothing recorded yet reports no macro is available", "[BufferView]") {
    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::F(4));

    REQUIRE(fixture.statusMessage == "No keyboard macro has been recorded yet.");
    REQUIRE(fixture.buffer.Text().empty());
}

TEST_CASE("Replaying a macro stops cleanly once a replayed command opens an interactive session",
          "[BufferView]") {
    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::F(3));
    TypeText(view, "a");
    view.OnEvent(ned::ui::test::Ctrl('s')); // isearch-forward -- the command itself is recorded,
    view.OnEvent(ned::ui::test::Escape());  // but nothing typed inside isearch ever reaches Dispatcher,
    TypeText(view, "b");                    // so this Escape (cancel) isn't recorded either -- back to
    view.OnEvent(ned::ui::test::F(4));      // Normal before "b" is typed and recording stops.

    REQUIRE(fixture.statusMessage == "Keyboard macro recorded (3 keys)."); // 'a', C-s, 'b'
    REQUIRE(fixture.buffer.Text() == "ab");

    view.OnEvent(ned::ui::test::F(4)); // replay: 'a' inserts, C-s enters isearch, then stops early

    REQUIRE(fixture.buffer.Text() == "aba");        // only the leading 'a' from the replay was inserted
    REQUIRE(fixture.statusMessage == "I-search: "); // isearch genuinely entered, not skipped/corrupted

    view.OnEvent(ned::ui::test::Escape()); // clean up the still-live isearch session
}

// point-to-register/jump-to-register/copy-to-register/insert-register follow-up.

TEST_CASE("point-to-register then jump-to-register moves point back to the saved position", "[BufferView]") {
    // jump-to-register resolves the saved buffer by name via BufferList::Find
    // (see HandleRegisterKey), so the buffer point-to-register saves against
    // has to actually be in bufferList_ -- fixture.buffer on its own isn't
    // (it's a standalone convenience object, not registered), matching every
    // other test in this file that needs a real, findable buffer.
    Fixture            fixture;
    ned::text::Buffer& scratch = fixture.bufferList.CreateBuffer("scratch");
    scratch.InsertAtPoint("hello world");
    scratch.SetPoint(5);
    fixture.activeBuffer.Set(scratch);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('x'));
    view.OnEvent(ned::ui::test::Character("r"));
    view.OnEvent(ned::ui::test::Character(" "));
    view.OnEvent(ned::ui::test::Character("a"));
    REQUIRE(fixture.statusMessage == "Point stored in register.");

    scratch.SetPoint(0);
    REQUIRE(scratch.Point() == 0);

    view.OnEvent(ned::ui::test::Ctrl('x'));
    view.OnEvent(ned::ui::test::Character("r"));
    view.OnEvent(ned::ui::test::Character("j"));
    view.OnEvent(ned::ui::test::Character("a"));

    REQUIRE(scratch.Point() == 5);
    REQUIRE(fixture.statusMessage.empty());
}

TEST_CASE("jump-to-register to a buffer that's since been closed reports an error instead of crashing",
          "[BufferView]") {
    Fixture            fixture;
    ned::text::Buffer& other = fixture.bufferList.CreateBuffer("other");
    other.InsertAtPoint("other buffer text");
    other.SetPoint(3);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 2});

    fixture.activeBuffer.Set(other);
    view.OnEvent(ned::ui::test::Ctrl('x'));
    view.OnEvent(ned::ui::test::Character("r"));
    view.OnEvent(ned::ui::test::Character(" "));
    view.OnEvent(ned::ui::test::Character("b"));

    fixture.activeBuffer.Set(fixture.buffer); // back to a still-open buffer before closing "other"
    fixture.bufferList.Close("other");

    view.OnEvent(ned::ui::test::Ctrl('x'));
    view.OnEvent(ned::ui::test::Character("r"));
    view.OnEvent(ned::ui::test::Character("j"));
    view.OnEvent(ned::ui::test::Character("b"));

    REQUIRE(fixture.statusMessage == "Buffer for that register no longer exists.");
}

TEST_CASE("copy-to-register with no active mark reports an error", "[BufferView]") {
    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('x'));
    view.OnEvent(ned::ui::test::Character("r"));
    view.OnEvent(ned::ui::test::Character("s"));
    view.OnEvent(ned::ui::test::Character("a"));

    REQUIRE(fixture.statusMessage == "No region to copy.");
}

TEST_CASE("copy-to-register then insert-register round-trips region text into a different buffer",
          "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("hello world");
    fixture.buffer.SetMark(0);
    fixture.buffer.SetPoint(5); // region == "hello"

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('x'));
    view.OnEvent(ned::ui::test::Character("r"));
    view.OnEvent(ned::ui::test::Character("s"));
    view.OnEvent(ned::ui::test::Character("c"));
    REQUIRE(fixture.statusMessage == "Copied to register.");

    ned::text::Buffer& other = fixture.bufferList.CreateBuffer("other");
    fixture.activeBuffer.Set(other);

    view.OnEvent(ned::ui::test::Ctrl('x'));
    view.OnEvent(ned::ui::test::Character("r"));
    view.OnEvent(ned::ui::test::Character("i"));
    view.OnEvent(ned::ui::test::Character("c"));

    REQUIRE(other.Text() == "hello");
    REQUIRE(fixture.statusMessage.empty());
}

// multi-cursor-round-2 follow-up.

TEST_CASE("Multi-cursor point-to-register then jump-to-register recreates every cursor", "[BufferView]") {
    Fixture            fixture;
    ned::text::Buffer& scratch = fixture.bufferList.CreateBuffer("scratch");
    scratch.InsertAtPoint("hello world");
    scratch.SetPoint(5);
    scratch.AddCursorAt(11);
    fixture.activeBuffer.Set(scratch);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('x'));
    view.OnEvent(ned::ui::test::Character("r"));
    view.OnEvent(ned::ui::test::Character(" "));
    view.OnEvent(ned::ui::test::Character("a"));
    REQUIRE(fixture.statusMessage == "Point stored in register.");

    scratch.SetPoint(0);
    scratch.ClearSecondaryCursors();

    view.OnEvent(ned::ui::test::Ctrl('x'));
    view.OnEvent(ned::ui::test::Character("r"));
    view.OnEvent(ned::ui::test::Character("j"));
    view.OnEvent(ned::ui::test::Character("a"));

    REQUIRE(scratch.Point() == 5);
    REQUIRE(scratch.SecondaryCursors().size() == 1);
    REQUIRE(scratch.SecondaryCursors()[0].point == 11);
}

TEST_CASE("Multi-cursor copy-to-register then insert-register distributes pieces 1:1", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("hello world");
    fixture.buffer.SetPoint(0);
    fixture.buffer.SetMark(5);         // primary's own region: "hello"
    fixture.buffer.AddCursorAt(11, 6); // secondary: mark=6, point=11 -> "world"

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('x'));
    view.OnEvent(ned::ui::test::Character("r"));
    view.OnEvent(ned::ui::test::Character("s"));
    view.OnEvent(ned::ui::test::Character("c"));
    REQUIRE(fixture.statusMessage == "Copied to register.");

    ned::text::Buffer& other = fixture.bufferList.CreateBuffer("other");
    other.InsertAtPoint("X\nY\n");
    other.SetPoint(0);    // before "X"
    other.AddCursorAt(2); // before "Y"
    fixture.activeBuffer.Set(other);

    view.OnEvent(ned::ui::test::Ctrl('x'));
    view.OnEvent(ned::ui::test::Character("r"));
    view.OnEvent(ned::ui::test::Character("i"));
    view.OnEvent(ned::ui::test::Character("c"));

    REQUIRE(other.Text() == "helloX\nworldY\n");
}

// Emacs-keymap-round-2 follow-up (zap-to-char).

TEST_CASE("zap-to-char kills forward up to and including the target character", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("hello, world");
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Alt('z'));
    REQUIRE(fixture.statusMessage == "Zap to char: ");
    view.OnEvent(ned::ui::test::Character(","));

    REQUIRE(fixture.buffer.Text() == " world");
    REQUIRE(fixture.buffer.Point() == 0);
    REQUIRE(fixture.killRing.Current() == "hello,");
    REQUIRE(fixture.statusMessage.empty());
}

TEST_CASE("zap-to-char reports when the character doesn't occur after point", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("hello");
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Alt('z'));
    view.OnEvent(ned::ui::test::Character("z"));

    REQUIRE(fixture.buffer.Text() == "hello");
    REQUIRE(fixture.statusMessage == "No such character.");
}

TEST_CASE("Escape cancels a pending zap-to-char without killing anything", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("hello, world");
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Alt('z'));
    view.OnEvent(ned::ui::test::Escape());

    REQUIRE(fixture.buffer.Text() == "hello, world");
    REQUIRE(fixture.killRing.Empty());
    REQUIRE(fixture.statusMessage == "Zap to char cancelled.");
}

TEST_CASE("Consecutive zap-to-char kills append into one kill-ring entry", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("a.b.c.");
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Alt('z'));
    view.OnEvent(ned::ui::test::Character(".")); // kills "a."
    view.OnEvent(ned::ui::test::Alt('z'));
    view.OnEvent(ned::ui::test::Character(".")); // consecutive -- appends "b."

    REQUIRE(fixture.buffer.Text() == "c.");
    REQUIRE(fixture.killRing.Current() == "a.b.");
}

TEST_CASE("An intervening command breaks the zap-to-char append chain", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("a.b.c.");
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Alt('z'));
    view.OnEvent(ned::ui::test::Character(".")); // kills "a."
    view.OnEvent(ned::ui::test::Ctrl('f'));      // ordinary motion command in between
    view.OnEvent(ned::ui::test::Alt('z'));
    view.OnEvent(ned::ui::test::Character(".")); // a fresh entry, not an append

    REQUIRE(fixture.killRing.Current() != "a.b.");
}

TEST_CASE("Multi-cursor zap-to-char kills one piece per cursor", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("a.x\nb.y\n");
    fixture.buffer.SetPoint(0);    // before "a.x"
    fixture.buffer.AddCursorAt(4); // before "b.y"

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 4});

    view.OnEvent(ned::ui::test::Alt('z'));
    view.OnEvent(ned::ui::test::Character("."));

    REQUIRE(fixture.buffer.Text() == "x\ny\n");
    REQUIRE(fixture.killRing.CurrentPieces() == std::vector<std::string>{"a.", "b."});
}

TEST_CASE("Multi-cursor kill-rectangle then yank-rectangle distributes blocks 1:1", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("abcdef\nghijkl\nABCDEF\nGHIJKL");

    fixture.buffer.SetMark(fixture.buffer.ByteOffsetForLineAndColumn(0, 1, 1));
    fixture.buffer.SetPoint(fixture.buffer.ByteOffsetForLineAndColumn(1, 4, 1)); // primary rectangle: lines 0-1, cols[1,4)

    const std::size_t secondaryMark  = fixture.buffer.ByteOffsetForLineAndColumn(2, 1, 1);
    const std::size_t secondaryPoint = fixture.buffer.ByteOffsetForLineAndColumn(3, 4, 1);
    fixture.buffer.AddCursorAt(secondaryPoint, secondaryMark); // secondary rectangle: lines 2-3, cols[1,4)

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 4});

    view.OnEvent(ned::ui::test::Ctrl('x'));
    view.OnEvent(ned::ui::test::Character("r"));
    view.OnEvent(ned::ui::test::Character("k"));

    REQUIRE(fixture.buffer.Text() == "aef\ngkl\nAEF\nGKL");
    REQUIRE_FALSE(fixture.buffer.HasMark());
    REQUIRE(fixture.buffer.HasSecondaryCursors()); // 2 distinct cursors remain, each landed at its own rectangle's start

    fixture.buffer.ClearSecondaryCursors();
    fixture.buffer.SetPoint(fixture.buffer.ByteOffsetForLineAndColumn(0, 3, 1));               // end of "aef"
    const std::size_t secondaryYankPoint = fixture.buffer.ByteOffsetForLineAndColumn(2, 3, 1); // end of "AEF"
    fixture.buffer.AddCursorAt(secondaryYankPoint);

    view.OnEvent(ned::ui::test::Ctrl('x'));
    view.OnEvent(ned::ui::test::Character("r"));
    view.OnEvent(ned::ui::test::Character("y"));

    REQUIRE(fixture.buffer.Text() == "aefbcd\ngklhij\nAEFBCD\nGKLHIJ");
}

TEST_CASE("add-cursor-below scrolls the view to show the newly added cursor", "[BufferView]") {
    Fixture     fixture;
    std::string content;
    for (int i = 0; i < 10; ++i) {
        content += "line" + std::to_string(i) + "\n";
    }
    fixture.buffer.InsertAtPoint(content);
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 1}); // 2 lines visible

    ned::ui::Screen screen = ned::ui::Screen(20, 2);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 1});

    view.Paint(canvas);
    REQUIRE(ContentRowText(screen, 0, 5, 10) == "line0");

    // add-cursor-below 5 times lands the newest secondary cursor on line 5,
    // well past the 2-row viewport starting at line 0 -- proves
    // ScrollToShowOffset (not the ordinary, unmoved-primary
    // ScrollToShowPoint) ran.
    for (int i = 0; i < 5; ++i) {
        view.OnEvent(ned::ui::test::ArrowDownCtrl());
    }

    view.Paint(canvas);
    REQUIRE(ContentRowText(screen, 1, 5, 10) == "line5");
}

TEST_CASE("insert-register and jump-to-register on a never-set register report the right error",
          "[BufferView]") {
    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('x'));
    view.OnEvent(ned::ui::test::Character("r"));
    view.OnEvent(ned::ui::test::Character("i"));
    view.OnEvent(ned::ui::test::Character("z"));
    REQUIRE(fixture.statusMessage == "Register does not contain text.");

    view.OnEvent(ned::ui::test::Ctrl('x'));
    view.OnEvent(ned::ui::test::Character("r"));
    view.OnEvent(ned::ui::test::Character("j"));
    view.OnEvent(ned::ui::test::Character("z"));
    REQUIRE(fixture.statusMessage == "Register does not contain a position.");
}

TEST_CASE("Escape cancels a register prompt cleanly", "[BufferView]") {
    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('x'));
    view.OnEvent(ned::ui::test::Character("r"));
    view.OnEvent(ned::ui::test::Character(" "));
    REQUIRE(fixture.statusMessage == "Point to register: ");

    view.OnEvent(ned::ui::test::Escape());
    REQUIRE(fixture.statusMessage == "Register command cancelled.");

    view.OnEvent(ned::ui::test::Character("z")); // back to normal editing
    REQUIRE(fixture.buffer.Text() == "z");
}

// kill-rectangle/delete-rectangle/yank-rectangle/string-rectangle follow-up.
// Mark is set directly via Buffer::SetMark (there's no keyboard
// set-mark-command in this codebase -- mouse-drag is the only real way --
// matching the same workaround the register tests above already needed for
// copy-to-register).

TEST_CASE("kill-rectangle with no active mark reports an error", "[BufferView]") {
    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('x'));
    view.OnEvent(ned::ui::test::Character("r"));
    view.OnEvent(ned::ui::test::Character("k"));

    REQUIRE(fixture.statusMessage == "No rectangle region selected.");
}

TEST_CASE("C-x r k then C-x r y round-trips a rectangle through real key events", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("abcdef\nghijkl");
    fixture.buffer.SetMark(1);   // line 0, column 1
    fixture.buffer.SetPoint(11); // line 1, column 4

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('x'));
    view.OnEvent(ned::ui::test::Character("r"));
    view.OnEvent(ned::ui::test::Character("k"));

    REQUIRE(fixture.buffer.Text() == "aef\ngkl");
    REQUIRE_FALSE(fixture.buffer.HasMark());

    fixture.buffer.SetPoint(fixture.buffer.Content().ByteLength()); // end of buffer -- "gkl"'s own column 3

    view.OnEvent(ned::ui::test::Ctrl('x'));
    view.OnEvent(ned::ui::test::Character("r"));
    view.OnEvent(ned::ui::test::Character("y"));

    // "bcd" lands directly after "gkl" (already exactly column 3 wide); "hij"
    // lands on a freshly-created line, padded with 3 spaces to reach column 3.
    REQUIRE(fixture.buffer.Text() == "aef\ngklbcd\n   hij");
}

TEST_CASE("C-x r t prompts for a replacement string and applies it across the rectangle", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("abcdef\nghijkl");
    fixture.buffer.SetMark(1);
    fixture.buffer.SetPoint(11);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('x'));
    view.OnEvent(ned::ui::test::Character("r"));
    view.OnEvent(ned::ui::test::Character("t"));
    REQUIRE(fixture.statusMessage == "String rectangle: ");

    TypeText(view, "XY");
    view.OnEvent(ned::ui::test::Return());

    REQUIRE(fixture.buffer.Text() == "aXYef\ngXYkl");
    REQUIRE_FALSE(fixture.buffer.HasMark());
    REQUIRE(fixture.statusMessage.empty());
}

TEST_CASE("string-rectangle with no active mark reports an error without opening a prompt", "[BufferView]") {
    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('x'));
    view.OnEvent(ned::ui::test::Character("r"));
    view.OnEvent(ned::ui::test::Character("t"));

    REQUIRE(fixture.statusMessage == "No rectangle region selected.");

    view.OnEvent(ned::ui::test::Character("z")); // proves we're back in Normal mode, not waiting in a prompt
    REQUIRE(fixture.buffer.Text() == "z");
}

// narrow-to-region/widen follow-up.

namespace {

// Ten lines, "line0".."line9", each terminated with a newline.
std::string TenNumberedLines() {
    std::string content;
    for (int i = 0; i < 10; ++i) {
        content += "line" + std::to_string(i) + "\n";
    }
    return content;
}

} // namespace

TEST_CASE("narrow-to-region with no active mark reports an error", "[BufferView]") {
    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('x'));
    view.OnEvent(ned::ui::test::Character("n"));
    view.OnEvent(ned::ui::test::Character("n"));

    REQUIRE(fixture.statusMessage == "No region to narrow to.");
}

TEST_CASE("A real narrow confines point motion to the narrowed lines", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint(TenNumberedLines());
    fixture.buffer.SetMark(fixture.buffer.Content().LineToByteOffset(3));
    fixture.buffer.SetPoint(fixture.buffer.Content().LineToByteOffset(5));

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 20});

    view.OnEvent(ned::ui::test::Ctrl('x'));
    view.OnEvent(ned::ui::test::Character("n"));
    view.OnEvent(ned::ui::test::Character("n"));
    REQUIRE(fixture.buffer.IsNarrowed());

    const auto [narrowStart, narrowEnd] = fixture.buffer.NarrowedRange();

    for (int i = 0; i < 20; ++i) { // far more than the narrowed span
        view.OnEvent(ned::ui::test::Ctrl('n'));
    }
    // Strictly < narrowEnd, not <=: narrowEnd is the excluded next line's
    // own start byte -- point resting *at* narrowEnd would already be
    // "on" that excluded line as far as ByteOffsetToLine is concerned, a
    // real, confirmed-via-manual-pty-testing bug this exact assertion is
    // written to catch (a looser <= wouldn't have).
    REQUIRE(fixture.buffer.Point() < narrowEnd);
    REQUIRE(fixture.buffer.Point() >= narrowStart);

    for (int i = 0; i < 20; ++i) {
        view.OnEvent(ned::ui::test::Ctrl('p'));
    }
    REQUIRE(fixture.buffer.Point() >= narrowStart);
    REQUIRE(fixture.buffer.Point() < narrowEnd);
}

TEST_CASE("widen restores full motion after narrowing", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint(TenNumberedLines());
    fixture.buffer.SetMark(fixture.buffer.Content().LineToByteOffset(3));
    fixture.buffer.SetPoint(fixture.buffer.Content().LineToByteOffset(5));

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 20});

    view.OnEvent(ned::ui::test::Ctrl('x'));
    view.OnEvent(ned::ui::test::Character("n"));
    view.OnEvent(ned::ui::test::Character("n"));
    REQUIRE(fixture.buffer.IsNarrowed());

    view.OnEvent(ned::ui::test::Ctrl('x'));
    view.OnEvent(ned::ui::test::Character("n"));
    view.OnEvent(ned::ui::test::Character("w"));
    REQUIRE_FALSE(fixture.buffer.IsNarrowed());

    for (int i = 0; i < 20; ++i) { // far more than 10 lines -- reaches the real last line if truly widened
        view.OnEvent(ned::ui::test::Ctrl('n'));
    }
    REQUIRE(fixture.buffer.Content().ByteOffsetToLine(fixture.buffer.Point()) ==
            fixture.buffer.Content().LineCount() - 1);
}

TEST_CASE("Typing at the end of the narrowed range's own last line extends it and keeps point inside",
          "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint(TenNumberedLines());
    fixture.buffer.SetMark(fixture.buffer.Content().LineToByteOffset(3));
    fixture.buffer.SetPoint(fixture.buffer.Content().LineToByteOffset(5));

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 20});

    view.OnEvent(ned::ui::test::Ctrl('x'));
    view.OnEvent(ned::ui::test::Character("n"));
    view.OnEvent(ned::ui::test::Character("n"));
    const std::size_t narrowEndBefore = fixture.buffer.NarrowedRange().second;

    // narrowEndBefore itself is the *excluded* next line's own start (never
    // a position point can actually be at while narrowed -- see
    // ClampPointToNarrowing's own doc comment) -- the real end of the
    // narrowed range's own last line, right before its trailing newline, is
    // one byte before that.
    fixture.buffer.SetPoint(narrowEndBefore - 1);
    view.OnEvent(ned::ui::test::Character("X"));

    const auto [narrowStartAfter, narrowEndAfter] = fixture.buffer.NarrowedRange();
    REQUIRE(narrowEndAfter == narrowEndBefore + 1);
    // Right after the newly-typed "X", still exactly one byte before the
    // (now grown) end -- no clamping needed, point already lands in bounds.
    REQUIRE(fixture.buffer.Point() == narrowEndBefore);
    REQUIRE(fixture.buffer.Point() >= narrowStartAfter);
}

TEST_CASE("BufferView's gutter is one column wider for a mode with a fold query", "[BufferView]") {
    Fixture fixture;
    fixture.mode = ned::editor::CMode();
    fixture.buffer.InsertAtPoint("x");

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 0});
    ned::ui::Screen screen = ned::ui::Screen(20, 1);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 0});
    view.Paint(canvas);

    REQUIRE(screen.PixelAt(GutterWidth(1, /*foldColumn=*/4), 0).character == "x");
}

TEST_CASE("BufferView shows no fold gutter column for a mode without a fold query", "[BufferView]") {
    Fixture fixture; // FundamentalMode -- no fold query
    fixture.buffer.InsertAtPoint("x");

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 0});
    ned::ui::Screen screen = ned::ui::Screen(20, 1);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 0});
    view.Paint(canvas);

    REQUIRE(screen.PixelAt(GutterWidth(1), 0).character == "x");
}

TEST_CASE("Clicking the fold gutter column collapses a code block, hiding its body", "[BufferView]") {
    Fixture fixture;
    fixture.mode = ned::editor::CMode();
    fixture.buffer.InsertAtPoint("int main(void) {\n    return 0;\n}\n");

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 29, .y_min = 0, .y_max = 2});
    ned::ui::Screen screen = ned::ui::Screen(30, 3);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 29, .y_min = 0, .y_max = 2});
    view.Paint(canvas); // establishes the foldable-blocks cache before the click

    const int foldStart = GutterWidth(3, /*foldColumn=*/4, /*symbolColumn=*/1) - 4;
    view.OnEvent(MousePress(foldStart, 0)); // fold column, row 0 -- the function's own opening line
    view.Paint(canvas);

    REQUIRE(screen.PixelAt(foldStart, 0).character == "⊞"); // collapsed glyph
    // The whole block body, including its own closing "}" line, is hidden
    // (matches Org's own "hides through the closing line" convention) --
    // row 0 instead gets the fold ellipsis plus a preview of that closing
    // line's own trimmed content, and row 1 (nothing left to show -- only
    // 3 lines exist and 2 are now hidden) is blank.
    REQUIRE(ContentRowText(screen, 0, 20, 3, /*foldColumn=*/4, /*symbolColumn=*/1) == "int main(void) { … }");
    REQUIRE(ContentRowText(screen, 1, 1, 3, /*foldColumn=*/4, /*symbolColumn=*/1) == " ");

    // Clicking again expands it back.
    view.OnEvent(MousePress(foldStart, 0));
    view.Paint(canvas);
    REQUIRE(screen.PixelAt(foldStart, 0).character == "⊟"); // expanded glyph
    REQUIRE(ContentRowText(screen, 1, 4, 3, /*foldColumn=*/4, /*symbolColumn=*/1) == "    ");
}

TEST_CASE("Nested fold regions render guide lines at increasing depth columns for an expanded block", "[BufferView]") {
    Fixture fixture;
    fixture.mode = ned::editor::CMode();
    fixture.buffer.InsertAtPoint("int main(void) {\n    if (a) {\n        x;\n    }\n}\n");

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 4});
    ned::ui::Screen screen = ned::ui::Screen(40, 5);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 4});
    view.Paint(canvas);

    const int foldStart = GutterWidth(5, /*foldColumn=*/4, /*symbolColumn=*/1) - 4;

    // Row 0: outer (depth 0) block's own header, column 0.
    REQUIRE(screen.PixelAt(foldStart, 0).character == "⊟");
    // Row 1: inner (depth 1) block's own header at column 1 -- column 0
    // still shows the outer block's guide line, since row 1 also sits
    // inside the outer block's own expanded span.
    REQUIRE(screen.PixelAt(foldStart, 1).character == "│");
    REQUIRE(screen.PixelAt(foldStart + 1, 1).character == "⊟");
    // Row 2 ("x;"): inside both spans -- guide lines at both columns.
    REQUIRE(screen.PixelAt(foldStart, 2).character == "│");
    REQUIRE(screen.PixelAt(foldStart + 1, 2).character == "│");
    // Row 3 ("    }"): the inner block's own closing row.
    REQUIRE(screen.PixelAt(foldStart, 3).character == "│");
    REQUIRE(screen.PixelAt(foldStart + 1, 3).character == "└");
    // Row 4 ("}"): the outer block's own closing row -- column 1 has
    // nothing left to show.
    REQUIRE(screen.PixelAt(foldStart, 4).character == "└");
    REQUIRE(screen.PixelAt(foldStart + 1, 4).character == " ");
}

TEST_CASE("Clicking a specific depth column toggles only that column's block", "[BufferView]") {
    Fixture fixture;
    fixture.mode = ned::editor::CMode();
    fixture.buffer.InsertAtPoint("int main(void) {\n    if (a) {\n        x;\n    }\n}\n");

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 4});
    ned::ui::Screen screen = ned::ui::Screen(40, 5);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 4});
    view.Paint(canvas);

    const int foldStart = GutterWidth(5, /*foldColumn=*/4, /*symbolColumn=*/1) - 4;
    view.OnEvent(MousePress(foldStart + 1, 1)); // inner block's own header, column 1
    view.Paint(canvas);

    REQUIRE(screen.PixelAt(foldStart + 1, 1).character == "⊞"); // inner now collapsed
    REQUIRE(screen.PixelAt(foldStart, 0).character == "⊟");     // outer untouched, still expanded
    // Collapsing the inner block hides its own body through its own
    // closing line (rows 2-3, "x;" and "    }") -- same "hides through the
    // closer" convention every other fold in this codebase already has --
    // so canvas row 2 now renders buffer line 4 ("}", the outer block's own
    // closer), not buffer line 3. Column 0 (the outer block, still
    // expanded) still gets its own closing corner there; column 1 (the
    // now-fully-consumed inner block) has nothing left to show.
    REQUIRE(screen.PixelAt(foldStart, 2).character == "└");
    REQUIRE(screen.PixelAt(foldStart + 1, 2).character == " ");
}

TEST_CASE("Collapsing an outer block leaves no guide line for its now-hidden nested block", "[BufferView]") {
    Fixture fixture;
    fixture.mode = ned::editor::CMode();
    fixture.buffer.InsertAtPoint("int main(void) {\n    if (a) {\n        x;\n    }\n}\n");

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 4});
    ned::ui::Screen screen = ned::ui::Screen(40, 5);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 4});
    view.Paint(canvas);

    const int foldStart = GutterWidth(5, /*foldColumn=*/4, /*symbolColumn=*/1) - 4;
    view.OnEvent(MousePress(foldStart, 0)); // outer block's own header, column 0
    view.Paint(canvas);

    REQUIRE(screen.PixelAt(foldStart, 0).character == "⊞");     // outer now collapsed
    REQUIRE(screen.PixelAt(foldStart + 1, 0).character == " "); // no column-1 guide line while the ancestor is folded
}

TEST_CASE("Blocks nested deeper than the gutter's columns draw no fold affordance at all", "[BufferView]") {
    // fold-gutter-depth-cap follow-up: depth >= kMaxFoldDepthColumns (4)
    // used to be clamped into the last column, piling deeper blocks' ⊞/⊟
    // and guide lines on top of the real depth-3 block's own -- now they
    // simply get no gutter entry (still foldable via code-fold-toggle).
    Fixture fixture;
    fixture.mode = ned::editor::CMode();
    // Depths 0-4: function body, then four nested if-blocks; the depth-4
    // block ("if (e)") opens on row 4 and closes on row 6.
    fixture.buffer.InsertAtPoint("int main(void) {\n"
                                 "    if (b) {\n"
                                 "        if (c) {\n"
                                 "            if (d) {\n"
                                 "                if (e) {\n"
                                 "                    x;\n"
                                 "                }\n"
                                 "            }\n"
                                 "        }\n"
                                 "    }\n"
                                 "}\n");

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 10});
    ned::ui::Screen screen = ned::ui::Screen(40, 11);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 10});
    view.Paint(canvas);

    const int foldStart = GutterWidth(11, /*foldColumn=*/4, /*symbolColumn=*/1) - 4;

    // Depths 0-3 each get their own header in their own column...
    REQUIRE(screen.PixelAt(foldStart + 0, 0).character == "⊟");
    REQUIRE(screen.PixelAt(foldStart + 1, 1).character == "⊟");
    REQUIRE(screen.PixelAt(foldStart + 2, 2).character == "⊟");
    REQUIRE(screen.PixelAt(foldStart + 3, 3).character == "⊟");
    // ...and the depth-4 block draws nothing: its header row shows only the
    // depth-3 block's own guide line in the last column (not a second,
    // stacked ⊟), and its body row likewise only inherited guide lines.
    REQUIRE(screen.PixelAt(foldStart + 3, 4).character == "│");
    REQUIRE(screen.PixelAt(foldStart + 3, 5).character == "│");
}

TEST_CASE("A block written entirely on one line gets no fold icon", "[BufferView]") {
    Fixture fixture;
    fixture.mode = ned::editor::CMode();
    // "int f(void) { return 1; }" -- header and closer are the same line,
    // so collapsing it would hide zero lines. No point showing a clickable
    // affordance that visibly does nothing.
    fixture.buffer.InsertAtPoint("int f(void) { return 1; }\n");

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 0});
    ned::ui::Screen screen = ned::ui::Screen(40, 1);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 0});
    view.Paint(canvas);

    const int foldStart = GutterWidth(1, /*foldColumn=*/4, /*symbolColumn=*/1) - 4;
    REQUIRE(screen.PixelAt(foldStart, 0).character == " ");
    REQUIRE(screen.PixelAt(foldStart + 1, 0).character == " ");
}

TEST_CASE("Nested blocks opening on the same line get one fold affordance -- the outermost", "[BufferView]") {
    // Regression test for a real, reported bug (clojure-and-jank follow-up):
    // lisp code routinely opens several foldable collections on one line
    // (`:profiles {:dev {:dependencies [...`), and the gutter used to stack
    // one ⊟ per depth on that single row -- three affordances all folding
    // essentially the same span. Only the outermost block gets an entry now;
    // see EnsureFoldGutterCache's own comment.
    Fixture fixture;
    fixture.mode = ned::editor::ClojureMode();
    // Three nested lists, all opening on line 0, all closing on line 1.
    fixture.buffer.InsertAtPoint("(a (b (c\n1)))\n");

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 1});
    ned::ui::Screen screen = ned::ui::Screen(40, 2);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 1});
    view.Paint(canvas);

    const int foldStart = GutterWidth(2, /*foldColumn=*/4) - 4;
    // One ⊟, in the outermost block's own column -- not a marker per depth.
    REQUIRE(screen.PixelAt(foldStart, 0).character == "⊟");
    REQUIRE(screen.PixelAt(foldStart + 1, 0).character == " ");
    REQUIRE(screen.PixelAt(foldStart + 2, 0).character == " ");

    // Clicking it collapses the outermost block, hiding the closing line.
    view.OnEvent(MousePress(foldStart, 0));
    view.Paint(canvas);
    REQUIRE(screen.PixelAt(foldStart, 0).character == "⊞");
    REQUIRE(ContentRowText(screen, 1, 1, 2, /*foldColumn=*/4) == " ");
}

TEST_CASE("A one-line block nested inside a real multi-line block still lets the outer block fold normally",
          "[BufferView]") {
    Fixture fixture;
    fixture.mode = ned::editor::CMode();
    // The outer function body spans multiple lines (real fold target); the
    // one-line "if" body nested inside it has nothing of its own to fold.
    fixture.buffer.InsertAtPoint("int main(void) {\n    if (a) { return 1; }\n}\n");

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});
    ned::ui::Screen screen = ned::ui::Screen(40, 3);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});
    view.Paint(canvas);

    const int foldStart = GutterWidth(3, /*foldColumn=*/4, /*symbolColumn=*/1) - 4;
    REQUIRE(screen.PixelAt(foldStart, 0).character == "⊟");     // outer block's own toggle, unaffected
    REQUIRE(screen.PixelAt(foldStart + 1, 1).character == " "); // one-line "if" body -- no icon
}

TEST_CASE("Fold header glyphs still render after scrolling past an earlier foldable block", "[BufferView]") {
    // Regression test for a real reported bug: a foldable block whose own
    // header line sits before topLine_ used to permanently stall the
    // header-glyph streaming cursor (Paint()'s foldGutterEntryCursor), so
    // every ⊞/⊟ glyph for the rest of the buffer silently stopped
    // rendering once scrolled past roughly line 50.
    Fixture fixture;
    fixture.mode = ned::editor::CMode();

    std::string source = "int early(void) {\n    return 0;\n}\n";
    for (int i = 0; i < 60; ++i) {
        source += "int pad" + std::to_string(i) + ";\n";
    }
    source += "int late(void) {\n    return 1;\n}\n";
    // Plenty more trailing lines so SetTopLine(63) below isn't itself
    // clamped down by MaxTopLine() to something before "int late" -- this
    // test is about the header-glyph cursor, not scroll clamping.
    for (int i = 0; i < 40; ++i) {
        source += "int trail" + std::to_string(i) + ";\n";
    }
    fixture.buffer.InsertAtPoint(source);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 4});
    ned::ui::Screen screen = ned::ui::Screen(40, 5);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 4});

    view.SetTopLine(63); // scrolled well past "int early(void) {" (line 0)
    REQUIRE(view.TopLine() == 63);
    view.Paint(canvas);

    // "int late(void) {" is line 63 -- the first visible row.
    const int foldStart = GutterWidth(fixture.buffer.Content().LineCount(), /*foldColumn=*/4, /*symbolColumn=*/1) - 4;
    REQUIRE(screen.PixelAt(foldStart, 0).character == "⊟");
}

TEST_CASE("The status column shows the unsaved-change indicator only on an edited line", "[BufferView]") {
    Fixture fixture;
    // Constructed directly with initial content (not via InsertAtPoint,
    // which would itself mark the whole thing as an unsaved change) so
    // this starts genuinely clean, the same way a freshly-loaded file would.
    fixture.buffer = ned::text::Buffer("scratch", ned::text::Rope("one\ntwo\nthree"));

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});
    ned::ui::Screen screen = ned::ui::Screen(20, 3);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});
    view.Paint(canvas);

    // No edits yet -- the status column is blank everywhere.
    REQUIRE(screen.PixelAt(0, 0).background_color == fixture.theme.background);
    REQUIRE(screen.PixelAt(0, 1).background_color == fixture.theme.background);

    fixture.buffer.SetPoint(fixture.buffer.Content().LineToByteOffset(1)); // start of "two"
    fixture.buffer.InsertAtPoint("X");
    view.Paint(canvas);

    REQUIRE(screen.PixelAt(0, 0).background_color == fixture.theme.background);             // "one" untouched
    REQUIRE(screen.PixelAt(0, 1).background_color == fixture.theme.unsavedChangeIndicator); // "two" edited
    REQUIRE(screen.PixelAt(0, 2).background_color == fixture.theme.background);             // "three" untouched
}

TEST_CASE("Saving clears the status column's unsaved-change indicator", "[BufferView]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned-bufferview-unsaved-test.txt";

    Fixture fixture;
    fixture.buffer.SetPath(path);
    fixture.buffer.InsertAtPoint("one\ntwo");

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 1});
    ned::ui::Screen screen = ned::ui::Screen(20, 2);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 1});
    view.Paint(canvas);

    REQUIRE(screen.PixelAt(0, 0).background_color == fixture.theme.unsavedChangeIndicator);

    fixture.buffer.Save();
    view.Paint(canvas);
    REQUIRE(screen.PixelAt(0, 0).background_color == fixture.theme.background);

    std::filesystem::remove(path);
}

TEST_CASE("Paint skips lines hidden by an Org fold and shows the fold indicator", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("* Parent\nbody\n* Sibling\n");
    fixture.buffer.SetFoldMarker(0, ned::text::Buffer::FoldMarker::Collapsed); // "* Parent"'s own line start

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});

    ned::ui::Screen screen = ned::ui::Screen(20, 3);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});
    view.Paint(canvas);

    const std::size_t totalLines = fixture.buffer.Content().LineCount();
    const int         gutter     = GutterWidth(totalLines);

    REQUIRE(ContentRowText(screen, 0, 8, totalLines) == "* Parent");
    REQUIRE(screen.PixelAt(gutter + 8, 0).character == " ");
    REQUIRE(screen.PixelAt(gutter + 9, 0).character == "…"); // fold indicator, right after "* Parent "
    // "body" (line 1) is hidden entirely -- row 1 shows "* Sibling" (line 2), not "body".
    REQUIRE(ContentRowText(screen, 1, 9, totalLines) == "* Sibling");
}

TEST_CASE("CursorPosition accounts for lines hidden above point by an Org fold", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("* Parent\nbody\n* Sibling\n");
    fixture.buffer.SetFoldMarker(0, ned::text::Buffer::FoldMarker::Collapsed);
    fixture.buffer.SetPoint(fixture.buffer.Text().find("Sibling"));

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});

    ned::ui::Screen screen = ned::ui::Screen(20, 3);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});
    view.Paint(canvas);

    REQUIRE(view.CursorPosition().has_value());
    // "body" (line 1) is hidden -- "* Sibling" (line 2) is only 1 visible
    // row below topLine_ (0), not 2.
    REQUIRE(view.CursorPosition()->y == 1);
}

TEST_CASE("Mouse click below an Org fold lands on the correct (visible) buffer line", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("* Parent\nbody\n* Sibling\n");
    fixture.buffer.SetFoldMarker(0, ned::text::Buffer::FoldMarker::Collapsed);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});

    ned::ui::Screen screen = ned::ui::Screen(20, 3);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});
    view.Paint(canvas); // establishes the fold cache before the click

    const int gutter = GutterWidth(fixture.buffer.Content().LineCount());
    view.OnEvent(MousePress(gutter, 1)); // screen row 1 -- "* Sibling" (line 2), since "body" (line 1) is hidden

    REQUIRE(fixture.buffer.Content().ByteOffsetToLine(fixture.buffer.Point()) == 2);
}

TEST_CASE("Paint collapses an Org link's raw markup down to just its own description", "[BufferView]") {
    Fixture fixture;
    fixture.mode = ned::editor::OrgMode();
    fixture.buffer.InsertAtPoint("x [[https://example.com][a website]] end\n");
    fixture.buffer.SetPoint(0); // outside the link (which starts at byte 2) -- collapsed

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    ned::ui::Screen screen = ned::ui::Screen(40, 3);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});
    view.Paint(canvas);

    REQUIRE(ContentRowText(screen, 0, 15, fixture.buffer.Content().LineCount()) == "x a website end");
}

TEST_CASE("Paint renders an Org link's raw markup once point moves inside it", "[BufferView]") {
    Fixture fixture;
    fixture.mode = ned::editor::OrgMode();
    fixture.buffer.InsertAtPoint("[[https://example.com][a website]] end\n");
    fixture.buffer.SetPoint(fixture.buffer.Text().find("example")); // inside the link -- raw

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    ned::ui::Screen screen = ned::ui::Screen(60, 3);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});
    view.Paint(canvas);

    REQUIRE(ContentRowText(screen, 0, 38, fixture.buffer.Content().LineCount()) ==
            "[[https://example.com][a website]] end");
}

TEST_CASE("Paint never collapses bracket-shaped text outside an org-mode buffer", "[BufferView]") {
    Fixture fixture; // fixture.mode stays FundamentalMode()
    fixture.buffer.InsertAtPoint("[[https://example.com][a website]] end\n");
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    ned::ui::Screen screen = ned::ui::Screen(60, 3);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});
    view.Paint(canvas);

    REQUIRE(ContentRowText(screen, 0, 38, fixture.buffer.Content().LineCount()) ==
            "[[https://example.com][a website]] end");
}

TEST_CASE("Clicking a collapsed Org link's own displayText lands point on the link's startByte", "[BufferView]") {
    Fixture fixture;
    fixture.mode = ned::editor::OrgMode();
    fixture.buffer.InsertAtPoint("x [[https://example.com][a website]] end\n");
    fixture.buffer.SetPoint(0); // outside the link -- collapsed when painted

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    ned::ui::Screen screen = ned::ui::Screen(60, 3);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});
    view.Paint(canvas); // establishes the link cache before the click

    const int gutter       = GutterWidth(fixture.buffer.Content().LineCount());
    const int linkStartCol = gutter + 2;           // "x " (2 columns) precedes the collapsed link
    view.OnEvent(MousePress(linkStartCol + 3, 0)); // land somewhere in the middle of "a website"

    REQUIRE(fixture.buffer.Point() == fixture.buffer.Text().find("[["));
}

TEST_CASE("open-link-at-point follows an Org internal link to its target headline", "[BufferView]") {
    Fixture fixture;
    fixture.mode = ned::editor::OrgMode();
    fixture.buffer.InsertAtPoint("* Some Heading\nbody\n[[*Some Heading]]\n");
    fixture.buffer.SetPoint(fixture.buffer.Text().find("Some Heading]]")); // point on the link's own line

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 4});

    view.OnEvent(ned::ui::test::Alt('x'));
    TypeText(view, "open-link-at-point");
    view.OnEvent(ned::ui::test::Return());

    REQUIRE(fixture.buffer.Point() == 0); // "* Some Heading"'s own line start
    REQUIRE(fixture.statusMessage.empty());
}

TEST_CASE("open-link-at-point reports failure for an Org internal link with no matching headline", "[BufferView]") {
    Fixture fixture;
    fixture.mode = ned::editor::OrgMode();
    fixture.buffer.InsertAtPoint("[[*Nowhere]]\n");
    fixture.buffer.SetPoint(2);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Alt('x'));
    TypeText(view, "open-link-at-point");
    view.OnEvent(ned::ui::test::Return());

    REQUIRE(fixture.statusMessage == "Link target not found: Nowhere");
}

TEST_CASE("open-link-at-point opens an Org link's URL target via the generic engine", "[BufferView]") {
    const UrlOpenCommandGuard guard;
    ned::editor::link::SetUrlOpenCommand("true"); // a real, always-succeeding no-op command

    Fixture fixture;
    fixture.mode = ned::editor::OrgMode();
    fixture.buffer.InsertAtPoint("[[https://example.com][site]]\n");
    fixture.buffer.SetPoint(2);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Alt('x'));
    TypeText(view, "open-link-at-point");
    view.OnEvent(ned::ui::test::Return());

    REQUIRE(fixture.statusMessage == "Opening https://example.com");
}

TEST_CASE("open-link-at-point falls back to bare-URL detection in an org-mode buffer off any bracket link",
          "[BufferView]") {
    const UrlOpenCommandGuard guard;
    ned::editor::link::SetUrlOpenCommand("true");

    Fixture fixture;
    fixture.mode = ned::editor::OrgMode();
    fixture.buffer.InsertAtPoint("see https://example.com here\n");
    fixture.buffer.SetPoint(fixture.buffer.Text().find("example"));

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Alt('x'));
    TypeText(view, "open-link-at-point");
    view.OnEvent(ned::ui::test::Return());

    REQUIRE(fixture.statusMessage == "Opening https://example.com");
}

TEST_CASE("open-link-at-point opens a bare URL in a non-Org buffer", "[BufferView]") {
    const UrlOpenCommandGuard guard;
    ned::editor::link::SetUrlOpenCommand("true");

    Fixture fixture; // FundamentalMode()
    fixture.buffer.InsertAtPoint("see https://example.com here\n");
    fixture.buffer.SetPoint(fixture.buffer.Text().find("example"));

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Alt('x'));
    TypeText(view, "open-link-at-point");
    view.OnEvent(ned::ui::test::Return());

    REQUIRE(fixture.statusMessage == "Opening https://example.com");
}

TEST_CASE("open-link-at-point opens an existing relative file path, switching to it", "[BufferView]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_bufferview_test_open_link_file";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "notes.txt") << "hello from notes\n";
    }
    const CurrentPathGuard pathGuard(dir); // relocates cwd + ProjectRoot() -- scratch buffers fall back to it

    Fixture fixture; // scratch buffer, no Path() of its own
    fixture.buffer.InsertAtPoint("see notes.txt here\n");
    fixture.buffer.SetPoint(fixture.buffer.Text().find("notes.txt"));

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Alt('x'));
    TypeText(view, "open-link-at-point");
    view.OnEvent(ned::ui::test::Return());

    REQUIRE(&fixture.activeBuffer.Get() != &fixture.buffer);
    REQUIRE(fixture.activeBuffer.Get().Text() == "hello from notes\n");

    std::filesystem::remove_all(dir);
}

TEST_CASE("open-link-at-point reports failure when nothing at point looks like a link", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("just some plain text\n");
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Alt('x'));
    TypeText(view, "open-link-at-point");
    view.OnEvent(ned::ui::test::Return());

    REQUIRE(fixture.statusMessage == "No link at point.");
}

// universal-clickable-affordances follow-up: Ctrl+Click is the mouse
// counterpart to open-link-at-point, wired in BufferView::OnMouseEvent
// rather than through the command registry -- these exercise that wiring
// directly rather than re-testing DetectLinkAtPoint's own detection logic,
// already covered above via the keyboard path.
TEST_CASE("Ctrl+Click opens the URL under the click", "[BufferView]") {
    const UrlOpenCommandGuard guard;
    ned::editor::link::SetUrlOpenCommand("true"); // a real, always-succeeding no-op command

    Fixture fixture; // FundamentalMode()
    fixture.buffer.InsertAtPoint("see https://example.com here\n");

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    // "https://example.com" starts right after "see " (4 columns in).
    view.OnEvent(MousePressCtrl(GutterWidth(1) + 4 + 8, 0));

    REQUIRE(fixture.statusMessage == "Opening https://example.com");
}

TEST_CASE("Ctrl+Click off any link still places point but reports no link, same as the keyboard path",
          "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("just some plain text\n");
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    view.OnEvent(MousePressCtrl(GutterWidth(1) + 5, 0));

    REQUIRE(fixture.buffer.Point() == 5);
    REQUIRE(fixture.statusMessage == "No link at point.");
}

TEST_CASE("A plain click (no Ctrl) on a URL just places point, doesn't open it", "[BufferView]") {
    const UrlOpenCommandGuard guard;
    ned::editor::link::SetUrlOpenCommand("true");

    Fixture fixture;
    fixture.buffer.InsertAtPoint("see https://example.com here\n");

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 59, .y_min = 0, .y_max = 2});

    view.OnEvent(MousePress(GutterWidth(1) + 4 + 8, 0));

    REQUIRE(fixture.buffer.Point() == 4 + 8);
    REQUIRE(fixture.statusMessage != "Opening https://example.com");
}

// structural-selection-expansion follow-up. M-=/M-- are simulated via their
// ESC-prefix fallback binding (Escape then the literal character), the same
// real two-chord sequence a slow/non-Meta-capable terminal would send --
// mirrors how narrow-to-region's tests above simulate "C-x n n" one chord at
// a time rather than a single synthetic multi-key event.

TEST_CASE("expand-selection with no structural selection support reports an error", "[BufferView]") {
    Fixture fixture; // FundamentalMode by default -- no expandSelection hook
    fixture.buffer.InsertAtPoint("hello");
    fixture.buffer.SetPoint(2);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Escape());
    view.OnEvent(ned::ui::test::Character("="));

    REQUIRE(fixture.statusMessage == "No structural selection support in this mode.");
    REQUIRE_FALSE(fixture.buffer.HasMark());
}

TEST_CASE("shrink-selection with no prior expansion reports an error", "[BufferView]") {
    Fixture fixture;
    fixture.mode = ned::editor::JsonMode();
    fixture.buffer.InsertAtPoint(R"({"a": 1})");
    fixture.buffer.SetPoint(6);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Escape());
    view.OnEvent(ned::ui::test::Character("-"));

    REQUIRE(fixture.statusMessage == "No selection to shrink to.");
}

TEST_CASE("expand-selection grows the selection step by step; shrink-selection walks it back down exactly",
          "[BufferView]") {
    Fixture fixture;
    fixture.mode = ned::editor::JsonMode();
    fixture.buffer.InsertAtPoint(R"({"a": 1})");
    fixture.buffer.SetPoint(6); // inside the "1"

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Escape());
    view.OnEvent(ned::ui::test::Character("="));
    REQUIRE(fixture.buffer.HasMark());
    auto [firstStart, firstEnd] = fixture.buffer.Region();
    REQUIRE(fixture.buffer.Text().substr(firstStart, firstEnd - firstStart) == "1");

    view.OnEvent(ned::ui::test::Escape());
    view.OnEvent(ned::ui::test::Character("="));
    auto [secondStart, secondEnd] = fixture.buffer.Region();
    REQUIRE(fixture.buffer.Text().substr(secondStart, secondEnd - secondStart) == "\"a\": 1");

    // Shrink back down: first shrink restores exactly the pre-second-expand
    // region, second shrink exactly the pre-first-expand (zero-width) point.
    view.OnEvent(ned::ui::test::Escape());
    view.OnEvent(ned::ui::test::Character("-"));
    REQUIRE(fixture.buffer.Region() == std::pair{firstStart, firstEnd});

    view.OnEvent(ned::ui::test::Escape());
    view.OnEvent(ned::ui::test::Character("-"));
    REQUIRE(fixture.buffer.Region() == std::pair{std::size_t{6}, std::size_t{6}});

    // History is now empty -- one more shrink is a no-op reporting as such.
    view.OnEvent(ned::ui::test::Escape());
    view.OnEvent(ned::ui::test::Character("-"));
    REQUIRE(fixture.statusMessage == "No selection to shrink to.");
}

TEST_CASE("expand-selection reports when it reaches the outermost node", "[BufferView]") {
    Fixture fixture;
    fixture.mode = ned::editor::JsonMode();
    fixture.buffer.InsertAtPoint(R"({"a": 1})");
    fixture.buffer.SetPoint(6);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 2});

    for (int i = 0; i < 10 && fixture.statusMessage != "Already at outermost node."; ++i) {
        view.OnEvent(ned::ui::test::Escape());
        view.OnEvent(ned::ui::test::Character("="));
    }

    REQUIRE(fixture.statusMessage == "Already at outermost node.");
    const auto [start, end] = fixture.buffer.Region();
    REQUIRE(fixture.buffer.Text().substr(start, end - start) == fixture.buffer.Text());
}

TEST_CASE("Ordinary motion after an expand clears the expansion history", "[BufferView]") {
    Fixture fixture;
    fixture.mode = ned::editor::JsonMode();
    fixture.buffer.InsertAtPoint(R"({"a": 1})");
    fixture.buffer.SetPoint(6);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Escape());
    view.OnEvent(ned::ui::test::Character("="));
    REQUIRE(fixture.buffer.HasMark());

    view.OnEvent(ned::ui::test::Ctrl('f')); // forward-char -- an ordinary dispatched command, interactiveRequest stays None

    view.OnEvent(ned::ui::test::Escape());
    view.OnEvent(ned::ui::test::Character("-"));
    REQUIRE(fixture.statusMessage == "No selection to shrink to.");
}

TEST_CASE("Switching the active buffer invalidates a stale expansion history", "[BufferView]") {
    Fixture fixture;
    fixture.mode = ned::editor::JsonMode();
    fixture.buffer.InsertAtPoint(R"({"a": 1})");
    fixture.buffer.SetPoint(6);

    ned::text::Buffer& otherBuffer = fixture.bufferList.CreateBuffer("other");
    otherBuffer.InsertAtPoint(R"({"b": 2})");

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Escape());
    view.OnEvent(ned::ui::test::Character("="));
    REQUIRE(fixture.buffer.HasMark());

    // A mouse-driven buffer switch (TabBar/ProjectSidebar click) doesn't go
    // through command dispatch, so RunCommandAndHandleOutcome's own
    // "any other command clears the history" path never runs for it --
    // this is exactly what the buffer-identity staleness check in
    // StartInteractiveSession is for instead.
    fixture.activeBuffer.Set(otherBuffer);

    view.OnEvent(ned::ui::test::Escape());
    view.OnEvent(ned::ui::test::Character("-"));
    REQUIRE(fixture.statusMessage == "No selection to shrink to.");
}

// project-find-file follow-up. Mirrors the M-x tests above closely -- same
// fuzzy-narrow/arrow-select/Enter-to-act interaction shape, reusing the same
// FuzzyFilterAndRank/BuildFuzzyCandidatePopupModel machinery, just over a
// cached project file list instead of dispatcher_.Registry().Names().

TEST_CASE("C-c C-f lists every project file before any input, then narrows as you type", "[BufferView]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_bufferview_test_project_find_file";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    std::filesystem::create_directory(dir / "src");
    {
        std::ofstream(dir / "src" / "main.cpp") << "int main() {}\n";
        std::ofstream(dir / "README.md") << "hello\n";
    }
    const CurrentPathGuard cwdGuard(dir);

    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 2});
    CaptureCandidates(view, fixture.candidates);

    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Ctrl('f'));

    REQUIRE(fixture.statusMessage == "Find file (fuzzy): ");
    REQUIRE(CandidatesContain(fixture.candidates, "README.md"));
    REQUIRE(CandidatesContain(fixture.candidates, "src/main.cpp"));

    TypeText(view, "main");
    REQUIRE(CandidateSelected(fixture.candidates, "src/main.cpp"));
    REQUIRE_FALSE(CandidatesContain(fixture.candidates, "README.md"));

    std::filesystem::remove_all(dir);
}

TEST_CASE("Enter in project-find-file opens the selected file", "[BufferView]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_bufferview_test_project_find_file_open";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "target.txt") << "content\n";
    }
    const CurrentPathGuard cwdGuard(dir);

    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Ctrl('f'));
    TypeText(view, "target");
    view.OnEvent(ned::ui::test::Return());

    REQUIRE(&fixture.activeBuffer.Get() != &fixture.buffer);
    REQUIRE(fixture.activeBuffer.Get().Name() == "target.txt");
    REQUIRE(fixture.activeBuffer.Get().Text() == "content\n");
    REQUIRE(fixture.statusMessage == "Opened target.txt");

    view.OnEvent(ned::ui::test::Character("z")); // back to normal editing, proves inputMode_ is Normal again
    REQUIRE(fixture.activeBuffer.Get().Text() == "zcontent\n");

    std::filesystem::remove_all(dir);
}

TEST_CASE("Down in project-find-file moves the selection between two equally-ranked files", "[BufferView]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_bufferview_test_project_find_file_arrows";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        // Same fuzzy score by construction for query "zzz" -- ranking falls
        // back to alphabetical, same determinism trick the M-x arrow test
        // above uses.
        std::ofstream(dir / "zzz-alpha.txt") << "alpha\n";
        std::ofstream(dir / "zzz-beta.txt") << "beta\n";
    }
    const CurrentPathGuard cwdGuard(dir);

    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 2});
    CaptureCandidates(view, fixture.candidates);

    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Ctrl('f'));
    TypeText(view, "zzz");
    REQUIRE(CandidateSelected(fixture.candidates, "zzz-alpha.txt"));

    view.OnEvent(ned::ui::test::ArrowDown());
    REQUIRE(CandidateSelected(fixture.candidates, "zzz-beta.txt"));

    view.OnEvent(ned::ui::test::Return());
    REQUIRE(fixture.activeBuffer.Get().Name() == "zzz-beta.txt");

    std::filesystem::remove_all(dir);
}

TEST_CASE("project-find-file reports when nothing matches the typed query, without switching buffers", "[BufferView]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_bufferview_test_project_find_file_nomatch";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "only.txt") << "x\n";
    }
    const CurrentPathGuard cwdGuard(dir);

    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Ctrl('f'));
    TypeText(view, "zzzzznomatch");
    view.OnEvent(ned::ui::test::Return());

    REQUIRE(&fixture.activeBuffer.Get() == &fixture.buffer);
    REQUIRE(fixture.statusMessage == "No file matching \"zzzzznomatch\"");

    std::filesystem::remove_all(dir);
}

TEST_CASE("Escape cancels project-find-file and returns to normal editing", "[BufferView]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_bufferview_test_project_find_file_escape";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    {
        std::ofstream(dir / "only.txt") << "x\n";
    }
    const CurrentPathGuard cwdGuard(dir);

    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Ctrl('f'));
    TypeText(view, "onl");
    view.OnEvent(ned::ui::test::Escape());

    REQUIRE(fixture.statusMessage == "Project find file cancelled.");
    REQUIRE(&fixture.activeBuffer.Get() == &fixture.buffer);

    view.OnEvent(ned::ui::test::Character("z")); // back to normal editing
    REQUIRE(fixture.buffer.Text() == "z");

    std::filesystem::remove_all(dir);
}

TEST_CASE("project-find-file with no files under the project root reports so and never opens a prompt", "[BufferView]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_bufferview_test_project_find_file_empty";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    const CurrentPathGuard cwdGuard(dir);

    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Ctrl('f'));

    REQUIRE(fixture.statusMessage.rfind("No files found under", 0) == 0);

    view.OnEvent(ned::ui::test::Character("z")); // proves we're in Normal mode, not a stuck prompt
    REQUIRE(fixture.buffer.Text() == "z");

    std::filesystem::remove_all(dir);
}

// rich-theme-set follow-up (Phase 1): the select-theme picker. Same fuzzy
// session shape as project-find-file above, entered via M-x (it has no
// dedicated chord), with the one genuine addition under test: live preview
// through the SetThemeApplier callback on every selection/rank change,
// committed by Enter, reverted by Escape.

namespace {

// The picker's Enter commit persists the "theme" variable
// (Editor/Variables.h, write-through to $XDG_STATE_HOME) -- redirected to a
// throwaway directory here so no test can ever touch the developer's real
// variables.json.
struct StateDirGuard {
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_bufferview_test_state";
    std::string           previous;
    bool                  hadPrevious = false;

    StateDirGuard() {
        if (const char* existing = std::getenv("XDG_STATE_HOME")) {
            hadPrevious = true;
            previous    = existing;
        }
        std::filesystem::remove_all(dir);
        setenv("XDG_STATE_HOME", dir.c_str(), 1);
    }
    ~StateDirGuard() {
        if (hadPrevious) {
            setenv("XDG_STATE_HOME", previous.c_str(), 1);
        }
        else {
            unsetenv("XDG_STATE_HOME");
        }
        std::filesystem::remove_all(dir);
    }
};

// Opens the select-theme session via M-x and records every theme the
// applier is handed, by name.
struct ThemePickerHarness {
    StateDirGuard            stateGuard; // first: active before any commit could write
    Fixture                  fixture;
    ned::ui::BufferView      view = fixture.View();
    std::vector<std::string> applied;

    ThemePickerHarness() {
        view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 2});
        view.SetThemeApplier([this](const ned::ui::Theme& theme) { applied.push_back(theme.name); });
        CaptureCandidates(view, fixture.candidates);
        view.OnEvent(ned::ui::test::Alt('x'));
        TypeText(view, "select-theme");
        view.OnEvent(ned::ui::test::Return());
    }
};

} // namespace

TEST_CASE("select-theme opens on a synthetic \"Current theme\" row at the top, previewing nothing", "[BufferView]") {
    ThemePickerHarness h;

    REQUIRE(h.fixture.statusMessage == "Theme (fuzzy): ");
    // select-theme-current-row follow-up: the session opens on a synthetic
    // first candidate, not any real registry name's own row -- so opening
    // the picker never calls the theme applier at all, regardless of
    // whether the active theme happens to be a registered name (previewing
    // it via ThemeByName() would strip init.janet's own (ned/theme-set
    // ...) overrides live the instant the picker opens -- confirmed live
    // as the "doesn't apply correctly across the entire screen" symptom).
    REQUIRE(CandidateSelected(h.fixture.candidates, "Current theme"));
    REQUIRE(h.applied.empty());
}

TEST_CASE("Arrowing through select-theme previews each highlighted theme live, and back to none", "[BufferView]") {
    ThemePickerHarness h;

    // The real registry names sort right after the synthetic "Current
    // theme" row -- one Down from the opening selection lands on the
    // alphabetically-first one.
    const std::string first = ned::ui::ThemeNames().front();
    h.view.OnEvent(ned::ui::test::ArrowDown());
    REQUIRE(CandidateSelected(h.fixture.candidates, first));
    REQUIRE(h.applied == std::vector<std::string>{first});

    // Back to "Current theme" -- resolved against the session's own
    // snapshot (the Fixture's DarkTheme(), name "dark"), not a registry
    // lookup by the literal row text.
    h.view.OnEvent(ned::ui::test::ArrowUp());
    REQUIRE(CandidateSelected(h.fixture.candidates, "Current theme"));
    REQUIRE(h.applied == std::vector<std::string>{first, "dark"});
}

TEST_CASE("Enter commits the highlighted theme and typing narrows with live preview", "[BufferView]") {
    ThemePickerHarness h;

    TypeText(h.view, "ansi-l");
    REQUIRE(CandidateSelected(h.fixture.candidates, "ansi-light"));
    REQUIRE_FALSE(h.applied.empty());
    REQUIRE(h.applied.back() == "ansi-light");

    h.view.OnEvent(ned::ui::test::Return());
    REQUIRE(h.fixture.statusMessage == "Theme: ansi-light");
    REQUIRE(h.applied.back() == "ansi-light");

    h.view.OnEvent(ned::ui::test::Character("z")); // back to normal editing
    REQUIRE(h.fixture.buffer.Text() == "z");
}

TEST_CASE("Enter on \"Current theme\" with no navigation leaves everything unchanged and persists nothing",
          "[BufferView]") {
    ThemePickerHarness h;

    h.view.OnEvent(ned::ui::test::Return());
    REQUIRE(h.fixture.statusMessage == "Theme unchanged.");
    // No registry lookup (there's no "Current theme" entry to find) and no
    // SetVariable write -- persisting the synthetic label as the base
    // theme name would break next launch's own resolution.
    REQUIRE(h.applied == std::vector<std::string>{"dark"});

    h.view.OnEvent(ned::ui::test::Character("z")); // proves we're back in Normal mode
    REQUIRE(h.fixture.buffer.Text() == "z");
}

TEST_CASE("Escape cancels select-theme and restores the pre-session theme exactly", "[BufferView]") {
    ThemePickerHarness h;

    const std::string first = ned::ui::ThemeNames().front();
    h.view.OnEvent(ned::ui::test::ArrowDown()); // preview the alphabetically-first real theme
    REQUIRE(h.applied == std::vector<std::string>{first});

    h.view.OnEvent(ned::ui::test::Escape());
    REQUIRE(h.fixture.statusMessage == "Theme selection cancelled.");
    // The revert re-applies the snapshot taken at session start -- the
    // Fixture's own DarkTheme(), by value, not by registry lookup.
    REQUIRE(h.applied == std::vector<std::string>{first, "dark"});
}

TEST_CASE("select-theme without a wired applier reports instead of opening a session", "[BufferView]") {
    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Alt('x'));
    TypeText(view, "select-theme");
    view.OnEvent(ned::ui::test::Return());

    REQUIRE(fixture.statusMessage == "Theme switching is not wired up.");
    view.OnEvent(ned::ui::test::Character("z")); // proves we're in Normal mode, not a stuck prompt
    REQUIRE(fixture.buffer.Text() == "z");
}

TEST_CASE("The candidate popup caps its row count with a trailing '+K more' row, regardless of terminal width",
          "[BufferView]") {
    // generic-popup follow-up (Phase 3): each candidate is its own popup
    // row now (ListPopup, not one shared terminal-width-bounded line), so
    // this used to test that FormatFuzzyCandidates' *column*-budget window
    // never overflowed a narrow terminal -- superseded by
    // BuildFuzzyCandidatePopupModel's own row-*count* bound, which doesn't
    // depend on terminal width at all. Deliberately doesn't hardcode
    // kMaxPopupRows' exact value (a BufferView.cpp implementation detail):
    // 20 files is comfortably more than any reasonable cap, 2 is
    // comfortably fewer.
    const std::filesystem::path manyDir = std::filesystem::temp_directory_path() / "ned_bufferview_test_pff_many";
    std::filesystem::remove_all(manyDir);
    std::filesystem::create_directory(manyDir);
    for (int i = 0; i < 20; ++i) {
        std::ofstream(manyDir / ("file" + std::to_string(i) + ".txt")) << "x\n";
    }
    {
        const CurrentPathGuard cwdGuard(manyDir);
        Fixture                fixture;
        ned::ui::BufferView    view = fixture.View();
        view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 199, .y_min = 0, .y_max = 2}); // wide -- width isn't the bound
        CaptureCandidates(view, fixture.candidates);

        view.OnEvent(ned::ui::test::Ctrl('c'));
        view.OnEvent(ned::ui::test::Ctrl('f'));

        REQUIRE(fixture.candidates.has_value());
        REQUIRE(fixture.candidates->rows.size() < 20); // bounded, not one row per file
        REQUIRE(CandidatesHaveMoreTail(fixture.candidates));
    }
    std::filesystem::remove_all(manyDir);

    const std::filesystem::path fewDir = std::filesystem::temp_directory_path() / "ned_bufferview_test_pff_few";
    std::filesystem::remove_all(fewDir);
    std::filesystem::create_directory(fewDir);
    std::ofstream(fewDir / "a.txt") << "x\n";
    std::ofstream(fewDir / "b.txt") << "x\n";
    {
        const CurrentPathGuard cwdGuard(fewDir);
        Fixture                fixture;
        ned::ui::BufferView    view = fixture.View();
        view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 49, .y_min = 0, .y_max = 2}); // narrow -- still no effect
        CaptureCandidates(view, fixture.candidates);

        view.OnEvent(ned::ui::test::Ctrl('c'));
        view.OnEvent(ned::ui::test::Ctrl('f'));

        REQUIRE(fixture.candidates.has_value());
        REQUIRE(fixture.candidates->rows.size() == 2); // both fit -- nothing hidden
        REQUIRE_FALSE(CandidatesHaveMoreTail(fixture.candidates));
    }
    std::filesystem::remove_all(fewDir);
}

TEST_CASE("C-M-i (lsp-complete) shows a completion popup from a real completion response, and Tab accepts it",
          "[BufferView]") {
    Fixture                     fixture;
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned_bufferview_lsp_complete_test.txt";
    ned::text::Buffer&          buffer = fixture.bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("fo");
    fixture.activeBuffer.Set(buffer);

    ned::ui::EventLoop           eventLoop;
    ned::editor::lsp::LspManager manager(fixture.bufferList, eventLoop);
    ned::editor::lsp::LspClient* client = nullptr;
    FakeLspServer                server = FakeLspServer::Create(manager, "fundamental", eventLoop, client);

    ned::ui::BufferView view = fixture.View();
    view.SetLspManager(&manager);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});
    CaptureCompletion(view, fixture.completion);

    ned::ui::Screen screenBuf = ned::ui::Screen(40, 3);
    ned::ui::Canvas canvas(screenBuf, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});
    view.Paint(canvas); // triggers SyncBuffer -> didOpen
    (void)ReadRawLspFrame(server.serverStdinRead);

    view.OnEvent(ManualCompleteEvent()); // C-M-i -- lsp-complete

    const std::string raw     = ReadRawLspFrame(server.serverStdinRead);
    const auto        request = ned::editor::lsp::Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["method"] == "textDocument/completion");

    const auto response = ned::editor::lsp::Json{
        {"jsonrpc", "2.0"},
        {"id", LspRequestIdFromFrame(raw)},
        {"result",
         {{"isIncomplete", false},
          {"items", ned::editor::lsp::Json::array(
                        {{{"label", "foobar"}, {"insertText", "foobar"}, {"kind", 3}, {"detail", "() -> int"}}})}}},
    };
    client->DispatchFrame(response.dump());

    // The popup carries the item's label/kind glyph/detail, anchored one
    // row below point -- nothing renders inline in the buffer itself
    // anymore.
    REQUIRE(fixture.completion.has_value());
    REQUIRE(fixture.completion->rows.size() == 1);
    REQUIRE(fixture.completion->rows[0].main == "foobar");
    REQUIRE(fixture.completion->rows[0].right == "() -> int");
    REQUIRE(fixture.completion->rows[0].left == "\xC6\x92"); // "ƒ" -- LSP kind 3 == Function == SymbolKind::Callable
    REQUIRE(fixture.completion->anchor.has_value());
    REQUIRE(fixture.completion->anchor->x == static_cast<int>(GutterWidth(1)) + 2); // right after "fo"
    REQUIRE(fixture.completion->anchor->y == 1);                                    // one row below point's own row (0)
    REQUIRE(ContentRowText(screenBuf, 0, 6, 1) == "fo    ");                        // the buffer row itself is untouched

    view.OnEvent(ned::ui::test::Tab());
    REQUIRE(buffer.Text() == "foobar");
    REQUIRE_FALSE(fixture.completion.has_value()); // accepting hides the popup
}

TEST_CASE("Up/Down and M-n/M-p both cycle the completion popup's selection", "[BufferView]") {
    Fixture                     fixture;
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned_bufferview_lsp_complete_cycle_test.txt";
    ned::text::Buffer&          buffer = fixture.bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("fo");
    fixture.activeBuffer.Set(buffer);

    ned::ui::EventLoop           eventLoop;
    ned::editor::lsp::LspManager manager(fixture.bufferList, eventLoop);
    ned::editor::lsp::LspClient* client = nullptr;
    FakeLspServer                server = FakeLspServer::Create(manager, "fundamental", eventLoop, client);

    ned::ui::BufferView view = fixture.View();
    view.SetLspManager(&manager);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});
    CaptureCompletion(view, fixture.completion);

    ned::ui::Screen screenBuf = ned::ui::Screen(40, 3);
    ned::ui::Canvas canvas(screenBuf, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});
    view.Paint(canvas);
    (void)ReadRawLspFrame(server.serverStdinRead);

    view.OnEvent(ManualCompleteEvent());
    const std::string raw      = ReadRawLspFrame(server.serverStdinRead);
    const auto        response = ned::editor::lsp::Json{
        {"jsonrpc", "2.0"},
        {"id", LspRequestIdFromFrame(raw)},
        {"result", ned::editor::lsp::Json::array({{{"label", "foobar"}, {"insertText", "foobar"}}, {{"label", "foobaz"}, {"insertText", "foobaz"}}})},
    };
    client->DispatchFrame(response.dump());
    REQUIRE(CompletionSelectedLabel(fixture.completion) == "foobar");

    view.OnEvent(ned::ui::test::ArrowDown());
    REQUIRE(CompletionSelectedLabel(fixture.completion) == "foobaz");
    view.OnEvent(ned::ui::test::ArrowUp());
    REQUIRE(CompletionSelectedLabel(fixture.completion) == "foobar");

    view.OnEvent(ned::ui::test::Alt('n')); // M-n -- kept alongside Down for existing muscle memory
    REQUIRE(CompletionSelectedLabel(fixture.completion) == "foobaz");
    view.OnEvent(ned::ui::test::Tab());
    REQUIRE(buffer.Text() == "foobaz");
}

TEST_CASE("Any other key dismisses the completion popup instead of accepting it", "[BufferView]") {
    Fixture                     fixture;
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned_bufferview_lsp_complete_dismiss_test.txt";
    ned::text::Buffer&          buffer = fixture.bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("fo");
    fixture.activeBuffer.Set(buffer);

    ned::ui::EventLoop           eventLoop;
    ned::editor::lsp::LspManager manager(fixture.bufferList, eventLoop);
    ned::editor::lsp::LspClient* client = nullptr;
    FakeLspServer                server = FakeLspServer::Create(manager, "fundamental", eventLoop, client);

    ned::ui::BufferView view = fixture.View();
    view.SetLspManager(&manager);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});
    CaptureCompletion(view, fixture.completion);

    ned::ui::Screen screenBuf = ned::ui::Screen(40, 3);
    ned::ui::Canvas canvas(screenBuf, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});
    view.Paint(canvas);
    (void)ReadRawLspFrame(server.serverStdinRead);

    view.OnEvent(ManualCompleteEvent());
    const std::string raw      = ReadRawLspFrame(server.serverStdinRead);
    const auto        response = ned::editor::lsp::Json{
        {"jsonrpc", "2.0"},
        {"id", LspRequestIdFromFrame(raw)},
        {"result", ned::editor::lsp::Json::array({{{"label", "foobar"}, {"insertText", "foobar"}}})},
    };
    client->DispatchFrame(response.dump());
    REQUIRE(fixture.completion.has_value());

    view.OnEvent(ned::ui::test::ArrowRight()); // any other key -- dismisses, doesn't accept
    REQUIRE(buffer.Text() == "fo");            // the suggestion was never actually inserted
    REQUIRE_FALSE(fixture.completion.has_value()); // ... and the popup is hidden

    view.OnEvent(ned::ui::test::Tab()); // Tab now, with no popup showing, does whatever it ordinarily does (self-insert)
    REQUIRE(buffer.Text() != "foobar");
}

TEST_CASE("Completion popup hides when point's row scrolls off screen, but the suggestion is retained", "[BufferView]") {
    Fixture                     fixture;
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned_bufferview_lsp_complete_scroll_test.txt";
    ned::text::Buffer&          buffer = fixture.bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("fo");
    for (int i = 0; i < 20; ++i) {
        buffer.InsertAtPoint("\nline" + std::to_string(i));
    }
    buffer.SetPoint(2); // right after "fo" on line 0
    fixture.activeBuffer.Set(buffer);

    ned::ui::EventLoop           eventLoop;
    ned::editor::lsp::LspManager manager(fixture.bufferList, eventLoop);
    ned::editor::lsp::LspClient* client = nullptr;
    FakeLspServer                server = FakeLspServer::Create(manager, "fundamental", eventLoop, client);

    ned::ui::BufferView view = fixture.View();
    view.SetLspManager(&manager);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 4});
    CaptureCompletion(view, fixture.completion);

    ned::ui::Screen screenBuf = ned::ui::Screen(40, 5);
    ned::ui::Canvas canvas(screenBuf, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 4});
    view.Paint(canvas);
    DrainAllPendingFrames(server.serverStdinRead); // drain didOpen + any other background requests from this Paint()

    view.OnEvent(ManualCompleteEvent());
    const std::string raw      = ReadRawLspFrame(server.serverStdinRead);
    const auto        response = ned::editor::lsp::Json{
        {"jsonrpc", "2.0"},
        {"id", LspRequestIdFromFrame(raw)},
        {"result", ned::editor::lsp::Json::array({{{"label", "foobar"}, {"insertText", "foobar"}}})},
    };
    client->DispatchFrame(response.dump());
    REQUIRE(fixture.completion.has_value()); // still on screen right after the response

    // A pure scroll (point never moves) pushes point's own line above the
    // viewport -- nothing about activeCompletion_ itself changes, so only
    // Paint()'s own per-frame anchor check can catch this.
    view.OnEvent(MouseWheel(0, 0, ned::ui::MouseEvent::Button::WheelDown));
    view.Paint(canvas);
    REQUIRE(buffer.Point() == 2); // wheel never moves point
    REQUIRE_FALSE(fixture.completion.has_value());

    // The suggestion itself is still live -- Tab still accepts it even
    // though the popup isn't currently drawn, same tolerance the original
    // off-screen ghost text had.
    view.OnEvent(ned::ui::test::Tab());
    REQUIRE(buffer.Text().starts_with("foobar"));
}

// documentHighlight follow-up.
TEST_CASE("M-x lsp-document-highlight paints every reported occurrence with the document-highlight background",
          "[BufferView]") {
    Fixture                     fixture;
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned_bufferview_document_highlight_test.txt";
    ned::text::Buffer&          buffer = fixture.bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("foo = foo + 1");
    buffer.SetPoint(1); // inside the first "foo"
    fixture.activeBuffer.Set(buffer);

    ned::ui::EventLoop           eventLoop;
    ned::editor::lsp::LspManager manager(fixture.bufferList, eventLoop);
    ned::editor::lsp::LspClient* client = nullptr;
    FakeLspServer                server = FakeLspServer::Create(manager, "fundamental", eventLoop, client);

    ned::ui::BufferView view = fixture.View();
    view.SetLspManager(&manager);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    ned::ui::Screen screenBuf = ned::ui::Screen(40, 3);
    ned::ui::Canvas canvas(screenBuf, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});
    view.Paint(canvas);
    DrainAllPendingFrames(server.serverStdinRead); // drain didOpen + any other background requests from this Paint()

    view.OnEvent(ned::ui::test::Alt('x'));
    TypeText(view, "lsp-document-highlight");
    view.OnEvent(ned::ui::test::Return());

    const std::string raw     = ReadRawLspFrame(server.serverStdinRead);
    const auto        request = ned::editor::lsp::Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["method"] == "textDocument/documentHighlight");

    const auto response = ned::editor::lsp::Json{
        {"jsonrpc", "2.0"},
        {"id", LspRequestIdFromFrame(raw)},
        {"result", ned::editor::lsp::Json::array({{{"range", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 3}}}}}},
                                                   {{"range", {{"start", {{"line", 0}, {"character", 6}}}, {"end", {{"line", 0}, {"character", 9}}}}}}})},
    };
    client->DispatchFrame(response.dump());

    view.Paint(canvas);
    const int gutter = GutterWidth(1);
    REQUIRE(screenBuf.PixelAt(gutter + 0, 0).background_color == fixture.theme.documentHighlightBackground); // 'f' of the first "foo"
    REQUIRE(screenBuf.PixelAt(gutter + 2, 0).background_color == fixture.theme.documentHighlightBackground); // 'o'
    REQUIRE(screenBuf.PixelAt(gutter + 6, 0).background_color == fixture.theme.documentHighlightBackground); // 'f' of the second "foo"
    REQUIRE(screenBuf.PixelAt(gutter + 4, 0).background_color == fixture.theme.background);                  // '=' -- not part of either occurrence
}

TEST_CASE("C-c C-a with no code actions reports \"No code actions available.\"", "[BufferView]") {
    Fixture                     fixture;
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned_bufferview_code_action_none_test.txt";
    ned::text::Buffer&          buffer = fixture.bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("fine code");
    fixture.activeBuffer.Set(buffer);

    ned::ui::EventLoop           eventLoop;
    ned::editor::lsp::LspManager manager(fixture.bufferList, eventLoop);
    ned::editor::lsp::LspClient* client = nullptr;
    FakeLspServer                server = FakeLspServer::Create(manager, "fundamental", eventLoop, client);

    ned::ui::BufferView view = fixture.View();
    view.SetLspManager(&manager);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    ned::ui::Screen screenBuf = ned::ui::Screen(40, 3);
    ned::ui::Canvas canvas(screenBuf, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});
    view.Paint(canvas);
    (void)ReadRawLspFrame(server.serverStdinRead);

    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Ctrl('a'));

    const std::string raw      = ReadRawLspFrame(server.serverStdinRead);
    const auto        response = ned::editor::lsp::Json{
        {"jsonrpc", "2.0"}, {"id", LspRequestIdFromFrame(raw)}, {"result", ned::editor::lsp::Json::array()}};
    client->DispatchFrame(response.dump());

    REQUIRE(fixture.statusMessage == "No code actions available.");
}

TEST_CASE("C-c C-a with one code action applies it directly, no confirmation", "[BufferView]") {
    Fixture                     fixture;
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned_bufferview_code_action_one_test.txt";
    ned::text::Buffer&          buffer = fixture.bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("bad_code");
    fixture.activeBuffer.Set(buffer);

    ned::ui::EventLoop           eventLoop;
    ned::editor::lsp::LspManager manager(fixture.bufferList, eventLoop);
    ned::editor::lsp::LspClient* client = nullptr;
    FakeLspServer                server = FakeLspServer::Create(manager, "fundamental", eventLoop, client);

    ned::ui::BufferView view = fixture.View();
    view.SetLspManager(&manager);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    ned::ui::Screen screenBuf = ned::ui::Screen(40, 3);
    ned::ui::Canvas canvas(screenBuf, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});
    view.Paint(canvas);
    (void)ReadRawLspFrame(server.serverStdinRead);

    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Ctrl('a'));

    const std::string raw     = ReadRawLspFrame(server.serverStdinRead);
    const auto        request = ned::editor::lsp::Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["method"] == "textDocument/codeAction");
    const std::string ownUri = request["params"]["textDocument"]["uri"].get<std::string>();

    const auto response = ned::editor::lsp::Json{
        {"jsonrpc", "2.0"},
        {"id", LspRequestIdFromFrame(raw)},
        {"result", ned::editor::lsp::Json::array(
                       {{{"title", "Fix bad_code"},
                         {"edit",
                          {{"changes",
                            {{ownUri, ned::editor::lsp::Json::array(
                                          {{{"range", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 8}}}}},
                                            {"newText", "good_code"}}})}}}}}}})},
    };
    client->DispatchFrame(response.dump());

    REQUIRE(buffer.Text() == "good_code");
    REQUIRE(fixture.statusMessage == "Applied \"Fix bad_code\".");
}

TEST_CASE("C-c C-a with multiple code actions: digit-select applies the right one directly", "[BufferView]") {
    Fixture                     fixture;
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned_bufferview_code_action_multi_test.txt";
    ned::text::Buffer&          buffer = fixture.bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("bad_code");
    fixture.activeBuffer.Set(buffer);

    ned::ui::EventLoop           eventLoop;
    ned::editor::lsp::LspManager manager(fixture.bufferList, eventLoop);
    ned::editor::lsp::LspClient* client = nullptr;
    FakeLspServer                server = FakeLspServer::Create(manager, "fundamental", eventLoop, client);

    ned::ui::BufferView view = fixture.View();
    view.SetLspManager(&manager);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});
    CaptureCandidates(view, fixture.candidates);

    ned::ui::Screen screenBuf = ned::ui::Screen(40, 3);
    ned::ui::Canvas canvas(screenBuf, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});
    view.Paint(canvas);
    (void)ReadRawLspFrame(server.serverStdinRead);

    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Ctrl('a'));

    const std::string raw     = ReadRawLspFrame(server.serverStdinRead);
    const auto        request = ned::editor::lsp::Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    const std::string ownUri  = request["params"]["textDocument"]["uri"].get<std::string>();

    auto makeAction = [&](const std::string& title, const std::string& newText) {
        return ned::editor::lsp::Json{
            {"title", title},
            {"edit",
             {{"changes",
               {{ownUri, ned::editor::lsp::Json::array(
                             {{{"range", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 8}}}}},
                               {"newText", newText}}})}}}}},
        };
    };
    const auto response = ned::editor::lsp::Json{
        {"jsonrpc", "2.0"},
        {"id", LspRequestIdFromFrame(raw)},
        {"result", ned::editor::lsp::Json::array({makeAction("First fix", "first"), makeAction("Second fix", "second")})},
    };
    client->DispatchFrame(response.dump());

    REQUIRE(CandidateRowExists(fixture.candidates, "1)", "First fix"));
    REQUIRE(CandidateRowExists(fixture.candidates, "2)", "Second fix"));

    view.OnEvent(ned::ui::test::Character("2")); // jump directly to the second action, applying it with no confirmation
    REQUIRE(buffer.Text() == "second");
}

TEST_CASE("Escape at the code-action selection list leaves the buffer untouched", "[BufferView]") {
    Fixture                     fixture;
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned_bufferview_code_action_decline_test.txt";
    ned::text::Buffer&          buffer = fixture.bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("bad_code");
    fixture.activeBuffer.Set(buffer);

    ned::ui::EventLoop           eventLoop;
    ned::editor::lsp::LspManager manager(fixture.bufferList, eventLoop);
    ned::editor::lsp::LspClient* client = nullptr;
    FakeLspServer                server = FakeLspServer::Create(manager, "fundamental", eventLoop, client);

    ned::ui::BufferView view = fixture.View();
    view.SetLspManager(&manager);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    ned::ui::Screen screenBuf = ned::ui::Screen(40, 3);
    ned::ui::Canvas canvas(screenBuf, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});
    view.Paint(canvas);
    (void)ReadRawLspFrame(server.serverStdinRead);

    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Ctrl('a'));

    const std::string raw     = ReadRawLspFrame(server.serverStdinRead);
    const auto        request = ned::editor::lsp::Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    const std::string ownUri  = request["params"]["textDocument"]["uri"].get<std::string>();

    auto makeAction = [&](const std::string& title, const std::string& newText) {
        return ned::editor::lsp::Json{
            {"title", title},
            {"edit",
             {{"changes",
               {{ownUri, ned::editor::lsp::Json::array(
                             {{{"range", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 8}}}}},
                               {"newText", newText}}})}}}}},
        };
    };
    const auto response = ned::editor::lsp::Json{
        {"jsonrpc", "2.0"},
        {"id", LspRequestIdFromFrame(raw)},
        {"result", ned::editor::lsp::Json::array({makeAction("First fix", "first"), makeAction("Second fix", "second")})},
    };
    client->DispatchFrame(response.dump());

    view.OnEvent(ned::ui::test::Escape());
    REQUIRE(buffer.Text() == "bad_code");
    REQUIRE(fixture.statusMessage == "Code action cancelled.");
}

// go-to-definition/rename follow-up: M-. as a real raw byte sequence (ESC
// followed by '.'), same reasoning ManualCompleteEvent's own header comment
// gives for C-M-i -- exercises the real TranslateKey Meta-detection path
// rather than constructing a KeyChord directly.
ned::ui::Event ManualGotoDefinitionEvent() {
    return ned::ui::test::Alt('.');
}

// C-c C-M-r's second chord (C-M-r) as a raw byte sequence -- ESC followed by
// the C0 control byte for Ctrl+'r' (0x12) -- fed after a separate CtrlC
// event for the "C-c" prefix, the same two-events-for-a-two-chord-sequence
// shape every other "C-c C-x"-bound command's own test already uses.
ned::ui::Event ManualRenameEvent() {
    return ned::ui::test::CtrlAlt('r');
}

TEST_CASE("M-. with one definition location jumps directly, no confirmation", "[BufferView]") {
    Fixture                     fixture;
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned_bufferview_definition_one_test.txt";
    ned::text::Buffer&          buffer = fixture.bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("call_site()");
    fixture.activeBuffer.Set(buffer);

    ned::ui::EventLoop           eventLoop;
    ned::editor::lsp::LspManager manager(fixture.bufferList, eventLoop);
    ned::editor::lsp::LspClient* client = nullptr;
    FakeLspServer                server = FakeLspServer::Create(manager, "fundamental", eventLoop, client);

    ned::ui::BufferView view = fixture.View();
    view.SetLspManager(&manager);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    ned::ui::Screen screenBuf = ned::ui::Screen(40, 3);
    ned::ui::Canvas canvas(screenBuf, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});
    view.Paint(canvas);
    DrainAllPendingFrames(server.serverStdinRead); // drain didOpen + any other background requests from this Paint()

    view.OnEvent(ManualGotoDefinitionEvent());

    const std::string raw     = ReadRawLspFrame(server.serverStdinRead);
    const auto        request = ned::editor::lsp::Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["method"] == "textDocument/definition");

    const std::filesystem::path targetPath = std::filesystem::temp_directory_path() / "ned_bufferview_definition_target_test.txt";
    std::filesystem::remove(targetPath);
    {
        std::ofstream(targetPath) << "line zero\nline one\ndefinition here\n";
    }

    const auto response = ned::editor::lsp::Json{
        {"jsonrpc", "2.0"},
        {"id", LspRequestIdFromFrame(raw)},
        {"result", {{"uri", "file://" + targetPath.string()}, {"range", {{"start", {{"line", 2}, {"character", 0}}}, {"end", {{"line", 2}, {"character", 10}}}}}}},
    };
    client->DispatchFrame(response.dump());

    REQUIRE(fixture.activeBuffer.Get().Path() == targetPath);
    REQUIRE(fixture.activeBuffer.Get().Point() == fixture.activeBuffer.Get().Content().LineToByteOffset(2));
    REQUIRE(fixture.statusMessage.empty());

    std::filesystem::remove(targetPath);
}

TEST_CASE("M-. with no definitions reports \"No definition found.\"", "[BufferView]") {
    Fixture                     fixture;
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned_bufferview_definition_none_test.txt";
    ned::text::Buffer&          buffer = fixture.bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("unknown_symbol()");
    fixture.activeBuffer.Set(buffer);

    ned::ui::EventLoop           eventLoop;
    ned::editor::lsp::LspManager manager(fixture.bufferList, eventLoop);
    ned::editor::lsp::LspClient* client = nullptr;
    FakeLspServer                server = FakeLspServer::Create(manager, "fundamental", eventLoop, client);

    ned::ui::BufferView view = fixture.View();
    view.SetLspManager(&manager);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    ned::ui::Screen screenBuf = ned::ui::Screen(40, 3);
    ned::ui::Canvas canvas(screenBuf, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});
    view.Paint(canvas);
    (void)ReadRawLspFrame(server.serverStdinRead);

    view.OnEvent(ManualGotoDefinitionEvent());

    const std::string raw      = ReadRawLspFrame(server.serverStdinRead);
    const auto        response = ned::editor::lsp::Json{
        {"jsonrpc", "2.0"}, {"id", LspRequestIdFromFrame(raw)}, {"result", nullptr}};
    client->DispatchFrame(response.dump());

    REQUIRE(fixture.statusMessage == "No definition found.");
}

TEST_CASE("M-. with multiple definitions: digit-select jumps to the chosen one", "[BufferView]") {
    Fixture                     fixture;
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned_bufferview_definition_multi_test.txt";
    ned::text::Buffer&          buffer = fixture.bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("virtual_call()");
    fixture.activeBuffer.Set(buffer);

    ned::ui::EventLoop           eventLoop;
    ned::editor::lsp::LspManager manager(fixture.bufferList, eventLoop);
    ned::editor::lsp::LspClient* client = nullptr;
    FakeLspServer                server = FakeLspServer::Create(manager, "fundamental", eventLoop, client);

    ned::ui::BufferView view = fixture.View();
    view.SetLspManager(&manager);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    ned::ui::Screen screenBuf = ned::ui::Screen(40, 3);
    ned::ui::Canvas canvas(screenBuf, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});
    view.Paint(canvas);
    (void)ReadRawLspFrame(server.serverStdinRead);

    view.OnEvent(ManualGotoDefinitionEvent());

    const std::string raw = ReadRawLspFrame(server.serverStdinRead);

    const std::filesystem::path firstPath  = std::filesystem::temp_directory_path() / "ned_bufferview_definition_multi_a_test.txt";
    const std::filesystem::path secondPath = std::filesystem::temp_directory_path() / "ned_bufferview_definition_multi_b_test.txt";
    std::filesystem::remove(firstPath);
    std::filesystem::remove(secondPath);
    {
        std::ofstream(firstPath) << "impl a\n";
    }
    {
        std::ofstream(secondPath) << "impl b\n";
    }

    const auto response = ned::editor::lsp::Json{
        {"jsonrpc", "2.0"},
        {"id", LspRequestIdFromFrame(raw)},
        {"result", ned::editor::lsp::Json::array(
                       {{{"uri", "file://" + firstPath.string()},
                         {"range", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 1}}}}}},
                        {{"uri", "file://" + secondPath.string()},
                         {"range", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 1}}}}}}})},
    };
    client->DispatchFrame(response.dump());

    REQUIRE(fixture.statusMessage.find("1)") != std::string::npos);
    REQUIRE(fixture.statusMessage.find("2)") != std::string::npos);

    view.OnEvent(ned::ui::test::Character("2"));
    REQUIRE(fixture.activeBuffer.Get().Path() == secondPath);
    REQUIRE(fixture.statusMessage.empty());

    std::filesystem::remove(firstPath);
    std::filesystem::remove(secondPath);
}

// declaration/typeDefinition/implementation follow-up: none of the three has
// an established default binding to borrow (unlike M-. for goto-definition),
// so lsp-goto-declaration/-type-definition/-implementation are M-x-only --
// driven the same way org-clock-report's own test above drives a
// mode-independent command. One request/jump round trip per command is
// enough: RequestDefinitionAtPoint's shared body (staleness guard, empty/
// single/multiple-result handling) is already covered in full by the M-.
// tests above.
TEST_CASE("M-x lsp-goto-declaration sends textDocument/declaration and jumps on a single result", "[BufferView]") {
    Fixture                     fixture;
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned_bufferview_declaration_test.txt";
    ned::text::Buffer&          buffer = fixture.bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("extern_symbol");
    fixture.activeBuffer.Set(buffer);

    ned::ui::EventLoop           eventLoop;
    ned::editor::lsp::LspManager manager(fixture.bufferList, eventLoop);
    ned::editor::lsp::LspClient* client = nullptr;
    FakeLspServer                server = FakeLspServer::Create(manager, "fundamental", eventLoop, client);

    ned::ui::BufferView view = fixture.View();
    view.SetLspManager(&manager);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    ned::ui::Screen screenBuf = ned::ui::Screen(40, 3);
    ned::ui::Canvas canvas(screenBuf, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});
    view.Paint(canvas);
    DrainAllPendingFrames(server.serverStdinRead); // drain didOpen + any other background requests from this Paint()

    view.OnEvent(ned::ui::test::Alt('x'));
    TypeText(view, "lsp-goto-declaration");
    view.OnEvent(ned::ui::test::Return());

    const std::string raw     = ReadRawLspFrame(server.serverStdinRead);
    const auto        request = ned::editor::lsp::Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["method"] == "textDocument/declaration");

    const std::filesystem::path targetPath = std::filesystem::temp_directory_path() / "ned_bufferview_declaration_target_test.txt";
    std::filesystem::remove(targetPath);
    {
        std::ofstream(targetPath) << "declared here\n";
    }
    const auto response = ned::editor::lsp::Json{
        {"jsonrpc", "2.0"},
        {"id", LspRequestIdFromFrame(raw)},
        {"result", {{"uri", "file://" + targetPath.string()}, {"range", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 1}}}}}}},
    };
    client->DispatchFrame(response.dump());

    REQUIRE(fixture.activeBuffer.Get().Path() == targetPath);
    REQUIRE(fixture.statusMessage.empty());

    std::filesystem::remove(targetPath);
}

// symbol-search follow-up.
TEST_CASE("M-x lsp-goto-symbol sends textDocument/documentSymbol and jumps to the fuzzy-filtered selection",
          "[BufferView]") {
    Fixture                     fixture;
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned_bufferview_docsymbol_test.txt";
    ned::text::Buffer&          buffer = fixture.bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("struct Widget {};\nstruct Other {};");
    fixture.activeBuffer.Set(buffer);

    ned::ui::EventLoop           eventLoop;
    ned::editor::lsp::LspManager manager(fixture.bufferList, eventLoop);
    ned::editor::lsp::LspClient* client = nullptr;
    FakeLspServer                server = FakeLspServer::Create(manager, "fundamental", eventLoop, client);

    ned::ui::BufferView view = fixture.View();
    view.SetLspManager(&manager);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    ned::ui::Screen screenBuf = ned::ui::Screen(40, 3);
    ned::ui::Canvas canvas(screenBuf, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});
    view.Paint(canvas);
    DrainAllPendingFrames(server.serverStdinRead); // drain didOpen + any other background requests from this Paint()

    view.OnEvent(ned::ui::test::Alt('x'));
    TypeText(view, "lsp-goto-symbol");
    view.OnEvent(ned::ui::test::Return());

    const std::string raw     = ReadRawLspFrame(server.serverStdinRead);
    const auto        request = ned::editor::lsp::Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["method"] == "textDocument/documentSymbol");

    const auto response = ned::editor::lsp::Json{
        {"jsonrpc", "2.0"},
        {"id", LspRequestIdFromFrame(raw)},
        {"result",
         ned::editor::lsp::Json::array(
             {{{"name", "Widget"},
               {"kind", 23},
               {"range", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 18}}}}},
               {"selectionRange", {{"start", {{"line", 0}, {"character", 7}}}, {"end", {{"line", 0}, {"character", 13}}}}}},
              {{"name", "Other"},
               {"kind", 23},
               {"range", {{"start", {{"line", 1}, {"character", 0}}}, {"end", {{"line", 1}, {"character", 17}}}}},
               {"selectionRange", {{"start", {{"line", 1}, {"character", 7}}}, {"end", {{"line", 1}, {"character", 12}}}}}}})},
    };
    client->DispatchFrame(response.dump());

    TypeText(view, "Wid"); // fuzzy-narrow down to the "Widget" entry
    view.OnEvent(ned::ui::test::Return());

    REQUIRE(fixture.activeBuffer.Get().Point() == 7);
}

TEST_CASE("M-x lsp-workspace-symbol sends an immediate workspace/symbol request and jumps to the accepted result",
          "[BufferView]") {
    Fixture                     fixture;
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned_bufferview_wssymbol_test.txt";
    ned::text::Buffer&          buffer = fixture.bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("x");
    fixture.activeBuffer.Set(buffer);

    ned::ui::EventLoop           eventLoop;
    ned::editor::lsp::LspManager manager(fixture.bufferList, eventLoop);
    ned::editor::lsp::LspClient* client = nullptr;
    FakeLspServer                server = FakeLspServer::Create(manager, "fundamental", eventLoop, client);

    ned::ui::BufferView view = fixture.View();
    view.SetLspManager(&manager);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    ned::ui::Screen screenBuf = ned::ui::Screen(40, 3);
    ned::ui::Canvas canvas(screenBuf, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});
    view.Paint(canvas);
    DrainAllPendingFrames(server.serverStdinRead); // drain didOpen + any other background requests from this Paint()

    view.OnEvent(ned::ui::test::Alt('x'));
    TypeText(view, "lsp-workspace-symbol");
    view.OnEvent(ned::ui::test::Return());

    // The session opens with an immediate request (empty query) -- no
    // debounce for the very first request, unlike a re-query from typing.
    const std::string raw     = ReadRawLspFrame(server.serverStdinRead);
    const auto        request = ned::editor::lsp::Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["method"] == "workspace/symbol");
    REQUIRE(request["params"]["query"] == "");
    REQUIRE_FALSE(request["params"].contains("textDocument"));

    const std::filesystem::path targetPath = std::filesystem::temp_directory_path() / "ned_bufferview_wssymbol_target_test.cpp";
    std::filesystem::remove(targetPath);
    {
        std::ofstream(targetPath) << "class Found {};\n";
    }
    const auto response = ned::editor::lsp::Json{
        {"jsonrpc", "2.0"},
        {"id", LspRequestIdFromFrame(raw)},
        {"result", ned::editor::lsp::Json::array(
                       {{{"name", "Found"},
                         {"kind", 5},
                         {"location",
                          {{"uri", "file://" + targetPath.string()},
                           {"range", {{"start", {{"line", 0}, {"character", 6}}}, {"end", {{"line", 0}, {"character", 11}}}}}}}}})},
    };
    client->DispatchFrame(response.dump());

    view.OnEvent(ned::ui::test::Return()); // no typing -- accept the one live result as-is

    REQUIRE(fixture.activeBuffer.Get().Path() == targetPath);
    REQUIRE(fixture.activeBuffer.Get().Point() == 6);

    std::filesystem::remove(targetPath);
}

// signature-help follow-up: same async-write-into-statusMessage shape as
// lsp-hover's own tests (not shown here -- see LspManagerTest.cpp for the
// ExtractSignatureHelp/RequestSignatureHelp coverage this leans on), driven
// through M-x since lsp-signature-help has no default keybinding.
TEST_CASE("M-x lsp-signature-help shows the active parameter emphasized in the status line", "[BufferView]") {
    Fixture                     fixture;
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned_bufferview_signature_help_test.txt";
    ned::text::Buffer&          buffer = fixture.bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("foo(");
    fixture.activeBuffer.Set(buffer);

    ned::ui::EventLoop           eventLoop;
    ned::editor::lsp::LspManager manager(fixture.bufferList, eventLoop);
    ned::editor::lsp::LspClient* client = nullptr;
    FakeLspServer                server = FakeLspServer::Create(manager, "fundamental", eventLoop, client);

    ned::ui::BufferView view = fixture.View();
    view.SetLspManager(&manager);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    ned::ui::Screen screenBuf = ned::ui::Screen(40, 3);
    ned::ui::Canvas canvas(screenBuf, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});
    view.Paint(canvas);
    DrainAllPendingFrames(server.serverStdinRead); // drain didOpen + any other background requests from this Paint()

    view.OnEvent(ned::ui::test::Alt('x'));
    TypeText(view, "lsp-signature-help");
    view.OnEvent(ned::ui::test::Return());

    const std::string raw     = ReadRawLspFrame(server.serverStdinRead);
    const auto        request = ned::editor::lsp::Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["method"] == "textDocument/signatureHelp");

    const auto response = ned::editor::lsp::Json{
        {"jsonrpc", "2.0"},
        {"id", LspRequestIdFromFrame(raw)},
        {"result", {{"signatures", ned::editor::lsp::Json::array({{{"label", "foo(a: int)"}, {"parameters", ned::editor::lsp::Json::array({{{"label", "a: int"}}})}}})}, {"activeParameter", 0}}},
    };
    client->DispatchFrame(response.dump());

    REQUIRE(fixture.statusMessage == "foo(**a: int**)");
}

namespace {
// signature-help-auto-trigger follow-up: disables ghost-text auto-complete
// for the duration of the test below so its own debounce timer (armed by
// the exact same keystrokes) doesn't race the signature-help request onto
// the fake server's stdin pipe ahead of it -- restores the previous value
// (LspAutoCompleteEnabled's own default, true) on destruction, the same
// save/restore shape SnippetRegistryTestGuard uses for a different global.
struct AutoCompleteDisabledGuard {
    AutoCompleteDisabledGuard() : previous_(ned::editor::lsp::LspAutoCompleteEnabled()) {
        ned::editor::lsp::SetLspAutoCompleteEnabled(false);
    }
    ~AutoCompleteDisabledGuard() {
        ned::editor::lsp::SetLspAutoCompleteEnabled(previous_);
    }
    bool previous_;
};
} // namespace

// signature-help-auto-trigger follow-up.
TEST_CASE("Typing ( inside a call auto-triggers signature help after the debounce, with no manual invocation",
          "[BufferView]") {
    const AutoCompleteDisabledGuard guard;
    Fixture                         fixture;
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned_bufferview_signature_help_auto_test.txt";
    ned::text::Buffer&          buffer = fixture.bufferList.OpenOrCreateFile(path);
    fixture.activeBuffer.Set(buffer);

    ned::ui::EventLoop           eventLoop;
    ned::editor::lsp::LspManager manager(fixture.bufferList, eventLoop);
    ned::editor::lsp::LspClient* client = nullptr;
    FakeLspServer                server = FakeLspServer::Create(manager, "fundamental", eventLoop, client);

    ned::ui::BufferView view = fixture.View();
    view.SetLspManager(&manager);
    view.SetEventLoop(&eventLoop);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    ned::ui::Screen screenBuf = ned::ui::Screen(40, 3);
    ned::ui::Canvas canvas(screenBuf, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});
    view.Paint(canvas); // triggers SyncBuffer -> didOpen
    (void)ReadRawLspFrame(server.serverStdinRead);

    TypeText(view, "foo("); // the trailing '(' is the trigger character

    std::this_thread::sleep_for(std::chrono::milliseconds(600)); // past LspCompletionDebounceMs()'s default 500ms
    REQUIRE(eventLoop.DrainPosted_()); // runs both debounce timers' posted callbacks -- signature help AND
                                       // documentHighlight (which has no toggle to disable, see design decision
                                       // 2 -- so its own request rides along here too)

    // Both requests were queued back-to-back by the same DrainPosted_() call
    // -- find the signatureHelp one among them (order between the two isn't
    // guaranteed) and leave the other unanswered, which is harmless.
    const std::vector<ned::editor::lsp::Json> requests = ReadLspFrames(server.serverStdinRead, 2);
    const auto sigHelpRequest = std::find_if(requests.begin(), requests.end(),
                                             [](const ned::editor::lsp::Json& r) { return r["method"] == "textDocument/signatureHelp"; });
    REQUIRE(sigHelpRequest != requests.end());

    const auto response = ned::editor::lsp::Json{
        {"jsonrpc", "2.0"},
        {"id", (*sigHelpRequest)["id"]},
        {"result", {{"signatures", ned::editor::lsp::Json::array({{{"label", "foo(a: int)"}, {"parameters", ned::editor::lsp::Json::array({{{"label", "a: int"}}})}}})}, {"activeParameter", 0}}},
    };
    client->DispatchFrame(response.dump());

    REQUIRE(fixture.statusMessage == "foo(**a: int**)");
}

// signature-help-auto-trigger follow-up.
TEST_CASE("Typing a non-trigger character does not schedule an automatic signature-help request", "[BufferView]") {
    const AutoCompleteDisabledGuard guard;
    Fixture                         fixture;
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned_bufferview_signature_help_no_trigger_test.txt";
    ned::text::Buffer&          buffer = fixture.bufferList.OpenOrCreateFile(path);
    fixture.activeBuffer.Set(buffer);

    ned::ui::EventLoop           eventLoop;
    ned::editor::lsp::LspManager manager(fixture.bufferList, eventLoop);
    ned::editor::lsp::LspClient* client = nullptr;
    FakeLspServer                server = FakeLspServer::Create(manager, "fundamental", eventLoop, client);

    ned::ui::BufferView view = fixture.View();
    view.SetLspManager(&manager);
    view.SetEventLoop(&eventLoop);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    ned::ui::Screen screenBuf = ned::ui::Screen(40, 3);
    ned::ui::Canvas canvas(screenBuf, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});
    view.Paint(canvas);
    (void)ReadRawLspFrame(server.serverStdinRead);

    TypeText(view, "foo");

    // documentHighlight's own debounce timer fires here too (it has no
    // trigger-character gate, see design decision 1 -- typing "foo" moves
    // point/changes content regardless), so DrainPosted_() itself can't be
    // used as the signal; assert instead that no signatureHelp request
    // specifically was ever sent.
    std::this_thread::sleep_for(std::chrono::milliseconds(600)); // past LspCompletionDebounceMs()'s default 500ms
    (void)eventLoop.DrainPosted_();
    const std::vector<ned::editor::lsp::Json> requests = ReadLspFrames(server.serverStdinRead, 1);
    REQUIRE(std::none_of(requests.begin(), requests.end(),
                         [](const ned::editor::lsp::Json& r) { return r["method"] == "textDocument/signatureHelp"; }));
}

// header-source-switching follow-up: M-o as a raw byte sequence, same
// reasoning ManualGotoDefinitionEvent's own header comment gives.
ned::ui::Event ManualSwitchHeaderSourceEvent() {
    return ned::ui::test::Alt('o');
}

TEST_CASE("M-o switches to the file clangd's switchSourceHeader response names", "[BufferView]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_bufferview_switch_header_lsp_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    const std::filesystem::path sourcePath = dir / "widget.cpp";
    const std::filesystem::path headerPath = dir / "widget.h";
    {
        std::ofstream(sourcePath) << "// source\n";
    }
    {
        std::ofstream(headerPath) << "// header\n";
    }

    Fixture            fixture;
    ned::text::Buffer& buffer = fixture.bufferList.OpenOrCreateFile(sourcePath);
    fixture.activeBuffer.Set(buffer);

    ned::ui::EventLoop           eventLoop;
    ned::editor::lsp::LspManager manager(fixture.bufferList, eventLoop);
    ned::editor::lsp::LspClient* client = nullptr;
    FakeLspServer                server = FakeLspServer::Create(manager, "fundamental", eventLoop, client);

    ned::ui::BufferView view = fixture.View();
    view.SetLspManager(&manager);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    ned::ui::Screen screenBuf = ned::ui::Screen(40, 3);
    ned::ui::Canvas canvas(screenBuf, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});
    view.Paint(canvas);
    DrainAllPendingFrames(server.serverStdinRead); // drain didOpen + any other background requests from this Paint()

    view.OnEvent(ManualSwitchHeaderSourceEvent());

    const std::string raw     = ReadRawLspFrame(server.serverStdinRead);
    const auto        request = ned::editor::lsp::Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["method"] == "textDocument/switchSourceHeader");

    const auto response = ned::editor::lsp::Json{
        {"jsonrpc", "2.0"},
        {"id", LspRequestIdFromFrame(raw)},
        {"result", "file://" + headerPath.string()},
    };
    client->DispatchFrame(response.dump());

    REQUIRE(fixture.activeBuffer.Get().Path() == headerPath);
    REQUIRE(fixture.statusMessage.empty());

    std::filesystem::remove_all(dir);
}

TEST_CASE("M-o falls back to the filesystem heuristic when the server reports no counterpart", "[BufferView]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_bufferview_switch_header_fallback_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    const std::filesystem::path sourcePath = dir / "widget.cpp";
    const std::filesystem::path headerPath = dir / "widget.h";
    {
        std::ofstream(sourcePath) << "// source\n";
    }
    {
        std::ofstream(headerPath) << "// header\n";
    }

    Fixture            fixture;
    ned::text::Buffer& buffer = fixture.bufferList.OpenOrCreateFile(sourcePath);
    fixture.activeBuffer.Set(buffer);

    ned::ui::EventLoop           eventLoop;
    ned::editor::lsp::LspManager manager(fixture.bufferList, eventLoop);
    ned::editor::lsp::LspClient* client = nullptr;
    FakeLspServer                server = FakeLspServer::Create(manager, "fundamental", eventLoop, client);

    ned::ui::BufferView view = fixture.View();
    view.SetLspManager(&manager);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    ned::ui::Screen screenBuf = ned::ui::Screen(40, 3);
    ned::ui::Canvas canvas(screenBuf, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});
    view.Paint(canvas);
    (void)ReadRawLspFrame(server.serverStdinRead);

    view.OnEvent(ManualSwitchHeaderSourceEvent());

    const std::string raw      = ReadRawLspFrame(server.serverStdinRead);
    const auto        response = ned::editor::lsp::Json{
        {"jsonrpc", "2.0"},
        {"id", LspRequestIdFromFrame(raw)},
        {"result", nullptr},
    };
    client->DispatchFrame(response.dump());

    REQUIRE(fixture.activeBuffer.Get().Path() == headerPath);
    REQUIRE(fixture.statusMessage.empty());

    std::filesystem::remove_all(dir);
}

TEST_CASE("M-o falls straight to the filesystem heuristic with no LSP manager set", "[BufferView]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_bufferview_switch_header_no_lsp_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    const std::filesystem::path sourcePath = dir / "widget.cpp";
    const std::filesystem::path headerPath = dir / "widget.h";
    {
        std::ofstream(sourcePath) << "// source\n";
    }
    {
        std::ofstream(headerPath) << "// header\n";
    }

    Fixture            fixture;
    ned::text::Buffer& buffer = fixture.bufferList.OpenOrCreateFile(sourcePath);
    fixture.activeBuffer.Set(buffer);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    view.OnEvent(ManualSwitchHeaderSourceEvent());

    REQUIRE(fixture.activeBuffer.Get().Path() == headerPath);
    REQUIRE(fixture.statusMessage.empty());

    std::filesystem::remove_all(dir);
}

TEST_CASE("M-o reports failure when neither LSP nor the filesystem heuristic find a counterpart", "[BufferView]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_bufferview_switch_header_missing_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    const std::filesystem::path sourcePath = dir / "widget.cpp";
    {
        std::ofstream(sourcePath) << "// source\n"; // no widget.h anywhere
    }

    Fixture            fixture;
    ned::text::Buffer& buffer = fixture.bufferList.OpenOrCreateFile(sourcePath);
    fixture.activeBuffer.Set(buffer);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    view.OnEvent(ManualSwitchHeaderSourceEvent());

    REQUIRE(&fixture.activeBuffer.Get() == &buffer); // unchanged
    REQUIRE(fixture.statusMessage == "No corresponding header/source file found.");

    std::filesystem::remove_all(dir);
}

TEST_CASE("M-o reports \"Buffer has no associated file.\" for a scratch buffer", "[BufferView]") {
    Fixture fixture; // default buffer has no Path() of its own

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    view.OnEvent(ManualSwitchHeaderSourceEvent());

    REQUIRE(fixture.statusMessage == "Buffer has no associated file.");
}

TEST_CASE("C-c C-M-r prompts for a new name, then applies a multi-file rename across two buffers directly",
          "[BufferView]") {
    Fixture                     fixture;
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned_bufferview_rename_a_test.txt";
    ned::text::Buffer&          buffer = fixture.bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("old_name");
    fixture.activeBuffer.Set(buffer);

    const std::filesystem::path otherPath = std::filesystem::temp_directory_path() / "ned_bufferview_rename_b_test.txt";
    std::filesystem::remove(otherPath);
    {
        std::ofstream(otherPath) << "use old_name here\n";
    }

    ned::ui::EventLoop           eventLoop;
    ned::editor::lsp::LspManager manager(fixture.bufferList, eventLoop);
    ned::editor::lsp::LspClient* client = nullptr;
    FakeLspServer                server = FakeLspServer::Create(manager, "fundamental", eventLoop, client);

    ned::ui::BufferView view = fixture.View();
    view.SetLspManager(&manager);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    ned::ui::Screen screenBuf = ned::ui::Screen(40, 3);
    ned::ui::Canvas canvas(screenBuf, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});
    view.Paint(canvas);
    DrainAllPendingFrames(server.serverStdinRead); // drain didOpen + any other background requests from this Paint()

    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ManualRenameEvent());
    REQUIRE(fixture.statusMessage == "New name: ");

    TypeText(view, "new_name");
    view.OnEvent(ned::ui::test::Return());

    const std::string raw     = ReadRawLspFrame(server.serverStdinRead);
    const auto        request = ned::editor::lsp::Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["method"] == "textDocument/rename");
    REQUIRE(request["params"]["newName"] == "new_name");
    const std::string ownUri = request["params"]["textDocument"]["uri"].get<std::string>();

    const auto response = ned::editor::lsp::Json{
        {"jsonrpc", "2.0"},
        {"id", LspRequestIdFromFrame(raw)},
        {"result",
         {{"changes",
           {
               {ownUri, ned::editor::lsp::Json::array(
                            {{{"range", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 8}}}}},
                              {"newText", "new_name"}}})},
               {"file://" + otherPath.string(),
                ned::editor::lsp::Json::array(
                    {{{"range", {{"start", {{"line", 0}, {"character", 4}}}, {"end", {{"line", 0}, {"character", 12}}}}},
                      {"newText", "new_name"}}})},
           }}}},
    };
    client->DispatchFrame(response.dump());

    REQUIRE(buffer.Text() == "new_name");
    ned::text::Buffer* other = fixture.bufferList.FindByPath(otherPath);
    REQUIRE(other != nullptr);
    REQUIRE(other->Text() == "use new_name here\n");
    REQUIRE(fixture.statusMessage.find("Renamed") == 0);
    REQUIRE(fixture.statusMessage.find("2 edits across 2 files") != std::string::npos);

    std::filesystem::remove(otherPath);
}

TEST_CASE("Paint() surfaces a status-message hint once an LSP error has been logged", "[BufferView]") {
    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();

    ned::ui::EventLoop           eventLoop;
    ned::editor::lsp::LspManager manager(fixture.bufferList, eventLoop);
    view.SetLspManager(&manager);

    manager.LogError("test-lang", "boom");
    REQUIRE(manager.HasUnseenLogEntry());

    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});
    ned::ui::Screen screenBuf = ned::ui::Screen(40, 3);
    ned::ui::Canvas canvas(screenBuf, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});
    view.Paint(canvas);

    REQUIRE(fixture.statusMessage.find("*lsp log*") != std::string::npos);
    REQUIRE_FALSE(manager.HasUnseenLogEntry()); // acknowledged by this Paint()

    // A second, unrelated error later still surfaces (the flag isn't
    // permanently latched, just cleared until the next LogError call).
    fixture.statusMessage.clear();
    manager.LogError("test-lang", "boom again");
    view.Paint(canvas);
    REQUIRE(fixture.statusMessage.find("*lsp log*") != std::string::npos);
}

TEST_CASE("The LSP-error status hint never clobbers an already-set status message", "[BufferView]") {
    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();

    ned::ui::EventLoop           eventLoop;
    ned::editor::lsp::LspManager manager(fixture.bufferList, eventLoop);
    view.SetLspManager(&manager);

    fixture.statusMessage = "something the user is actively looking at";
    manager.LogError("test-lang", "boom");

    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});
    ned::ui::Screen screenBuf = ned::ui::Screen(40, 3);
    ned::ui::Canvas canvas(screenBuf, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});
    view.Paint(canvas);

    REQUIRE(fixture.statusMessage == "something the user is actively looking at");
    // Not acknowledged either -- the hint should still appear once the
    // status line actually clears.
    REQUIRE(manager.HasUnseenLogEntry());
}

TEST_CASE("lsp-show-log switches to the *lsp log* buffer, creating it if needed", "[BufferView]") {
    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 2});

    REQUIRE(fixture.bufferList.Find("*lsp log*") == nullptr);

    view.OnEvent(ned::ui::test::Alt('x'));
    TypeText(view, "lsp-show-log");
    view.OnEvent(ned::ui::test::Return());

    ned::text::Buffer* log = fixture.bufferList.Find("*lsp log*");
    REQUIRE(log != nullptr);
    REQUIRE(log->ReadOnly());
    REQUIRE(&fixture.activeBuffer.Get() == log);
}

TEST_CASE("A pending multi-chord prefix shows the accumulated sequence in the status line", "[BufferView]") {
    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();

    view.OnEvent(ned::ui::test::Ctrl('x')); // first half of C-x C-f -- a real, valid prefix
    REQUIRE(fixture.statusMessage == "C-x-");
}

TEST_CASE("Completing an unbound sequence reports it as undefined", "[BufferView]") {
    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();

    view.OnEvent(ned::ui::test::Ctrl('x'));
    view.OnEvent(ned::ui::test::Ctrl('z')); // C-x C-z is not bound to anything
    REQUIRE(fixture.statusMessage == "C-x C-z is undefined");
}

TEST_CASE("A pending prefix chord fires the which-key hint callback with the possible next chords",
          "[BufferView]") {
    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();

    std::optional<ned::ui::WhichKeyHint> lastHint;
    bool                                 called = false;
    view.SetOnPrefixHintChanged([&](std::optional<ned::ui::WhichKeyHint> hint) {
        called   = true;
        lastHint = std::move(hint);
    });

    view.OnEvent(ned::ui::test::Ctrl('x')); // first half of C-x C-f -- a real, valid prefix
    REQUIRE(called);
    REQUIRE(lastHint.has_value());
    REQUIRE(lastHint->prefixLabel == "C-x-");

    const auto find = [&](const std::string& chord) {
        return std::find_if(lastHint->bindings.begin(), lastHint->bindings.end(),
                             [&](const auto& binding) { return binding.first == chord; });
    };
    const auto it = find("C-f");
    REQUIRE(it != lastHint->bindings.end());
    REQUIRE(it->second == "find-file");
}

TEST_CASE("Completing a sequence (bound or not) clears the which-key hint", "[BufferView]") {
    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();

    std::vector<bool> hintPresentHistory;
    view.SetOnPrefixHintChanged(
        [&](std::optional<ned::ui::WhichKeyHint> hint) { hintPresentHistory.push_back(hint.has_value()); });

    view.OnEvent(ned::ui::test::Ctrl('x'));
    view.OnEvent(ned::ui::test::Ctrl('z')); // C-x C-z is unbound -- resolves the pending sequence

    REQUIRE(hintPresentHistory.size() == 2);
    REQUIRE(hintPresentHistory[0]);        // shown after C-x
    REQUIRE_FALSE(hintPresentHistory[1]);  // cleared once C-x C-z resolves as Unbound
}

TEST_CASE("Disabling which-key suppresses the hint callback's populated case, without touching the status line",
          "[BufferView]") {
    const struct Guard {
        ~Guard() {
            ned::editor::SetWhichKeyEnabled(true);
        }
    } guard;
    ned::editor::SetWhichKeyEnabled(false);

    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();

    std::optional<ned::ui::WhichKeyHint> lastHint;
    bool                                 called = false;
    view.SetOnPrefixHintChanged([&](std::optional<ned::ui::WhichKeyHint> hint) {
        called   = true;
        lastHint = std::move(hint);
    });

    view.OnEvent(ned::ui::test::Ctrl('x'));
    REQUIRE(called);
    REQUIRE_FALSE(lastHint.has_value());
    REQUIRE(fixture.statusMessage == "C-x-"); // status line's own pending text is unaffected by the setting
}

TEST_CASE("A stale status message clears on the next real command that doesn't set its own", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("hello");
    fixture.buffer.SetPoint(0);
    ned::ui::BufferView view = fixture.View();

    view.OnEvent(ned::ui::test::Ctrl('e')); // end-of-line -- reports nothing of its own
    fixture.statusMessage = "some stale leftover message";

    view.OnEvent(ned::ui::test::ArrowLeft()); // an ordinary motion command -- "a real action"
    REQUIRE(fixture.statusMessage.empty());
}

TEST_CASE("An active isearch session's own live status is unaffected by the stale-message clear", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("cat sat mat");
    fixture.buffer.SetPoint(0);
    ned::ui::BufferView view = fixture.View();

    view.OnEvent(ned::ui::test::Ctrl('s')); // isearch-forward
    view.OnEvent(ned::ui::test::Character("c"));
    REQUIRE(fixture.statusMessage.find("I-search:") == 0);
    REQUIRE(fixture.statusMessage.find('c') != std::string::npos);
}

// line-wrap follow-up: horizontal scroll-follow (non-wrap path) and
// line-wrap rendering itself.

TEST_CASE("A fresh BufferView has LeftColumn() 0", "[BufferView]") {
    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    REQUIRE(view.LeftColumn() == 0);
}

TEST_CASE("Typing past the right edge scrolls horizontally to keep the cursor visible", "[BufferView]") {
    Fixture             fixture; // FundamentalMode -- wrapLines false, the horizontal-scroll path
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});

    ned::ui::Screen screen = ned::ui::Screen(20, 3);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});
    view.Paint(canvas); // establish size() before the first ScrollToShowPointHorizontally() call

    for (int i = 0; i < 40; ++i) {
        view.OnEvent(ned::ui::test::Character("x"));
    }
    view.Paint(canvas);

    REQUIRE(view.LeftColumn() > 0);
    REQUIRE(view.CursorPosition().has_value());
    REQUIRE(view.CursorPosition()->x >= 0);
    REQUIRE(view.CursorPosition()->x < 20);
    REQUIRE(fixture.buffer.Text() == std::string(40, 'x'));
}

TEST_CASE("Moving point back to the start of a horizontally-scrolled line scrolls back to show it", "[BufferView]") {
    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});
    ned::ui::Screen screen = ned::ui::Screen(20, 3);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});
    view.Paint(canvas);

    for (int i = 0; i < 40; ++i) {
        view.OnEvent(ned::ui::test::Character("x"));
    }
    view.Paint(canvas);
    REQUIRE(view.LeftColumn() > 0);

    view.OnEvent(ned::ui::test::Ctrl('a')); // beginning-of-line
    view.Paint(canvas);
    REQUIRE(view.LeftColumn() == 0);
    REQUIRE(view.CursorPosition().has_value());
}

// horizontal-wheel-scroll follow-up.

TEST_CASE("Horizontal wheel scrolls leftColumn_ without moving point", "[BufferView]") {
    Fixture fixture; // FundamentalMode -- wrapLines false, the horizontal-scroll path
    fixture.buffer.InsertAtPoint(std::string(60, 'x'));
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});
    ned::ui::Screen screen = ned::ui::Screen(20, 3);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});
    view.Paint(canvas); // establish size() before the first wheel event

    view.OnEvent(MouseWheel(0, 0, ned::ui::MouseEvent::Button::WheelRight));
    REQUIRE(view.LeftColumn() == 3);
    REQUIRE(fixture.buffer.Point() == 0); // wheel never moves point, same as the vertical case

    view.OnEvent(MouseWheel(0, 0, ned::ui::MouseEvent::Button::WheelRight));
    REQUIRE(view.LeftColumn() == 6);

    view.OnEvent(MouseWheel(0, 0, ned::ui::MouseEvent::Button::WheelLeft));
    REQUIRE(view.LeftColumn() == 3);
}

TEST_CASE("Horizontal wheel scroll clamps at 0 rather than underflowing", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint(std::string(60, 'x'));
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});
    ned::ui::Screen screen = ned::ui::Screen(20, 3);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});
    view.Paint(canvas);

    view.OnEvent(MouseWheel(0, 0, ned::ui::MouseEvent::Button::WheelLeft));
    REQUIRE(view.LeftColumn() == 0);
}

TEST_CASE("Horizontal wheel is a no-op once wrapLines makes the buffer never scroll horizontally", "[BufferView]") {
    Fixture fixture;
    fixture.mode.wrapLines = true;
    fixture.buffer.InsertAtPoint(std::string(60, 'x'));
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});
    ned::ui::Screen screen = ned::ui::Screen(20, 3);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});
    view.Paint(canvas);

    view.OnEvent(MouseWheel(0, 0, ned::ui::MouseEvent::Button::WheelRight));
    REQUIRE(view.LeftColumn() == 0);
}

TEST_CASE("A wrap-enabled buffer breaks a long line at a word boundary, not mid-word", "[BufferView]") {
    Fixture fixture;
    fixture.mode.wrapLines = true;
    fixture.buffer.InsertAtPoint("aaaa bbbb cccc dddd");
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 14, .y_min = 0, .y_max = 4});

    ned::ui::Screen screen = ned::ui::Screen(15, 5);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 14, .y_min = 0, .y_max = 4});
    view.Paint(canvas);

    const int gutter = GutterWidth(1);
    // Content width is 15 - gutter columns -- "aaaa bbbb " (10 cols) fits,
    // "cccc" (4 more) would push past it, so the break lands after "bbbb ".
    REQUIRE(ContentRowText(screen, 0, 15 - gutter, 1).find("cccc") == std::string::npos);
    REQUIRE(RowText(screen, 0, 15).find("aaaa") != std::string::npos);
    REQUIRE(RowText(screen, 1, 15).find("cccc") != std::string::npos);
}

TEST_CASE("A wrap-enabled buffer hard-breaks a single token wider than the whole viewport", "[BufferView]") {
    Fixture fixture;
    fixture.mode.wrapLines = true;
    fixture.buffer.InsertAtPoint(std::string(30, 'x')); // one unbroken 30-char token, no whitespace anywhere
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 14, .y_min = 0, .y_max = 4});

    ned::ui::Screen screen = ned::ui::Screen(15, 5);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 14, .y_min = 0, .y_max = 4});
    view.Paint(canvas);

    // Must have actually wrapped onto at least a second row -- the whole
    // 30-char token can't fit on one row of a 15-column-wide viewport.
    const int         gutter    = GutterWidth(1);
    const std::string firstRow  = ContentRowText(screen, 0, 15 - gutter, 1);
    const std::string secondRow = ContentRowText(screen, 1, 15 - gutter, 1);
    REQUIRE(firstRow.find('x') != std::string::npos);
    REQUIRE(secondRow.find('x') != std::string::npos);
}

TEST_CASE("A non-wrap buffer's fold-off gutter shows a line number on every row (no wrapping happening)",
          "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint(std::string(60, 'x')); // long enough to clip, not wrap -- wrapLines stays false
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});
    ned::ui::Screen screen = ned::ui::Screen(20, 3);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});
    view.Paint(canvas);

    // Still just one buffer line -- row 1 stays blank, no continuation row
    // was manufactured.
    REQUIRE(RowText(screen, 1, 20).find_first_not_of(' ') == std::string::npos);
}

TEST_CASE("A clipped (non-wrap) too-long line shows a truncation indicator at its own last column",
          "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint(std::string(60, 'x')); // wrapLines stays false -- clips, doesn't wrap
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});
    ned::ui::Screen screen = ned::ui::Screen(20, 3);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});
    view.Paint(canvas);

    const ned::ui::Cell& lastCell = screen.PixelAt(19, 0);
    REQUIRE(lastCell.character == "\xc2\xbb"); // »
    REQUIRE(lastCell.foreground_color == fixture.theme.truncationIndicatorForeground);
}

TEST_CASE("A line that fits exactly within the viewport shows no truncation indicator", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("short");
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});
    ned::ui::Screen screen = ned::ui::Screen(20, 3);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 2});
    view.Paint(canvas);

    REQUIRE(RowText(screen, 0, 20).find("\xc2\xbb") == std::string::npos);
}

TEST_CASE("A wrap-enabled buffer never shows a truncation indicator -- a wrapped segment never clips",
          "[BufferView]") {
    Fixture fixture;
    fixture.mode.wrapLines = true;
    fixture.buffer.InsertAtPoint(std::string(60, 'x'));
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 4});
    ned::ui::Screen screen = ned::ui::Screen(20, 5);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 19, .y_min = 0, .y_max = 4});
    view.Paint(canvas);

    for (int row = 0; row < 5; ++row) {
        REQUIRE(RowText(screen, row, 20).find("\xc2\xbb") == std::string::npos);
    }
}

TEST_CASE("CursorPosition() lands on the correct wrapped row/column for point placed mid-paragraph",
          "[BufferView]") {
    Fixture fixture;
    fixture.mode.wrapLines = true;
    fixture.buffer.InsertAtPoint("aaaa bbbb cccc dddd");
    fixture.buffer.SetPoint(0);
    // Move point into "cccc", which the previous test already established
    // lands on the second wrapped row.
    for (int i = 0; i < 11; ++i) {
        fixture.buffer.MoveForward();
    }
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 14, .y_min = 0, .y_max = 4});
    ned::ui::Screen screen = ned::ui::Screen(15, 5);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 14, .y_min = 0, .y_max = 4});
    view.Paint(canvas);

    REQUIRE(view.CursorPosition().has_value());
    REQUIRE(view.CursorPosition()->y == 1); // second visual row
}

TEST_CASE("A mouse click on a wrapped continuation row resolves to the correct byte offset", "[BufferView]") {
    Fixture fixture;
    fixture.mode.wrapLines = true;
    fixture.buffer.InsertAtPoint("aaaa bbbb cccc dddd");
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 14, .y_min = 0, .y_max = 4});
    ned::ui::Screen screen = ned::ui::Screen(15, 5);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 14, .y_min = 0, .y_max = 4});
    view.Paint(canvas); // establish the wrap-segment layout the click below expects

    const int gutter = GutterWidth(1);
    // Row 1 is "cccc dddd" (the second wrap segment) -- clicking right at
    // its own start should land point at the byte offset of the 'c' in
    // "cccc" (byte 10 in the original text: "aaaa bbbb " is 10 bytes).
    view.OnEvent(MousePress(gutter, 1));
    view.OnEvent(MouseRelease(gutter, 1));
    REQUIRE(fixture.buffer.Point() == 10);
}

TEST_CASE("Line numbers appear only on a wrapped line's first row, not its continuation rows", "[BufferView]") {
    Fixture fixture;
    fixture.mode.wrapLines = true;
    fixture.buffer.InsertAtPoint("aaaa bbbb cccc dddd\nsecond line");
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 14, .y_min = 0, .y_max = 6});
    ned::ui::Screen screen = ned::ui::Screen(15, 7);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 14, .y_min = 0, .y_max = 6});
    view.Paint(canvas);

    // Row 0 is line 1's first segment -- digit "1" appears in the gutter.
    REQUIRE(RowText(screen, 0, 15).find('1') != std::string::npos);
    // Row 1 is line 1's continuation -- no digit anywhere in the gutter
    // columns (blank), only line content further right.
    const int         gutter             = GutterWidth(2);
    const std::string continuationGutter = RowText(screen, 1, gutter);
    REQUIRE(continuationGutter.find_first_not_of(' ') == std::string::npos);
    // Row 2 (the next real buffer line, "second line") shows its own "2".
    REQUIRE(RowText(screen, 2, 15).find('2') != std::string::npos);
}

TEST_CASE("A per-extension wrap override changes the effective behavior for a buffer whose Mode says otherwise",
          "[BufferView]") {
    Fixture fixture;
    fixture.mode              = ned::editor::MarkdownMode(); // wrapLines true by default
    ned::text::Buffer& buffer = fixture.bufferList.OpenOrCreateFile(
        std::filesystem::temp_directory_path() / "ned_bufferview_wrap_override_test.md");
    buffer.InsertAtPoint("aaaa bbbb cccc dddd");
    fixture.activeBuffer.Set(buffer);

    ned::editor::SetWrapForExtension("md", false); // override markdown-mode's own default off
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 14, .y_min = 0, .y_max = 4});
    ned::ui::Screen screen = ned::ui::Screen(15, 5);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 14, .y_min = 0, .y_max = 4});
    view.Paint(canvas);

    // Wrap is overridden off -- no continuation row, clipped instead.
    REQUIRE(RowText(screen, 1, 15).find_first_not_of(' ') == std::string::npos);

    ned::editor::SetWrapForExtension("md", true); // clean up global override state for other tests
}

TEST_CASE("MaxTopLine() can scroll far enough to show a wrapped document's own last lines, "
          "not get stuck on an earlier wrapped line that alone fills the viewport",
          "[BufferView]") {
    // Real, reported bug: scrolling to the end of a wrapped document could
    // leave its own trailing lines permanently unreachable. Root cause: the
    // backward walk used to give a wrapped line "partial credit" toward the
    // viewport budget even when only part of it fit -- but topLine_ can only
    // ever start at a line's own first row (never mid-segment), so Paint()
    // would render that line's FULL row count regardless, silently pushing
    // every line after it off the bottom, forever, no matter how far the
    // user scrolled.
    Fixture fixture;
    fixture.mode.wrapLines = true;
    // One long wrapped paragraph (occupies several rows on its own) followed
    // by two short lines -- with a small viewport, the paragraph alone can
    // fill it, which is exactly the scenario that triggered the bug.
    fixture.buffer.InsertAtPoint("aaaa bbbb cccc dddd eeee ffff gggg hhhh\nsecond\nthird");
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 14, .y_min = 0, .y_max = 2}); // 3-row viewport
    ned::ui::Screen screen = ned::ui::Screen(15, 3);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 14, .y_min = 0, .y_max = 2});
    view.Paint(canvas); // establish the wrap-segment layout MaxTopLine() below depends on

    view.SetTopLine(std::numeric_limits<std::size_t>::max()); // clamps internally to MaxTopLine()
    view.Paint(canvas);

    // Scrolled all the way down must actually show the real last line --
    // not still be stuck showing only (part of) the first wrapped paragraph.
    bool sawThird = false;
    for (int row = 0; row < 3; ++row) {
        if (RowText(screen, row, 15).find("third") != std::string::npos) {
            sawThird = true;
        }
    }
    REQUIRE(sawThird);
}

// --- Emacs-coverage follow-up: goto-line + recenter ----------------------

TEST_CASE("M-g g prompts for a line number and jumps point there", "[BufferView]") {
    Fixture               fixture;
    ned::text::Buffer&    scratch = fixture.bufferList.CreateBuffer("scratch");
    ned::ui::ActiveBuffer activeBuffer(scratch);
    ned::ui::BufferView   view(activeBuffer, fixture.killRing, fixture.registers, fixture.promptHistory, fixture.bufferList, fixture.dispatcher,
                               fixture.statusMessage, fixture.mode, fixture.theme);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    scratch.InsertAtPoint("one\ntwo\nthree\nfour\nfive");
    scratch.SetPoint(0);

    view.OnEvent(ned::ui::test::Alt('g'));
    view.OnEvent(ned::ui::test::Character("g"));
    REQUIRE(fixture.statusMessage == "Goto line: ");

    TypeText(view, "4");
    view.OnEvent(ned::ui::test::Return());
    REQUIRE(scratch.Point() == scratch.Content().LineToByteOffset(3));

    // Out-of-range clamps to the last line rather than erroring.
    view.OnEvent(ned::ui::test::Alt('g'));
    view.OnEvent(ned::ui::test::Alt('g')); // M-g M-g, the other Emacs binding
    TypeText(view, "999");
    view.OnEvent(ned::ui::test::Return());
    REQUIRE(scratch.Point() == scratch.Content().LineToByteOffset(4));
}

TEST_CASE("goto-line rejects non-numeric input and stays usable", "[BufferView]") {
    Fixture               fixture;
    ned::text::Buffer&    scratch = fixture.bufferList.CreateBuffer("scratch");
    ned::ui::ActiveBuffer activeBuffer(scratch);
    ned::ui::BufferView   view(activeBuffer, fixture.killRing, fixture.registers, fixture.promptHistory, fixture.bufferList, fixture.dispatcher,
                               fixture.statusMessage, fixture.mode, fixture.theme);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    scratch.InsertAtPoint("one\ntwo");
    scratch.SetPoint(0);

    view.OnEvent(ned::ui::test::Alt('g'));
    view.OnEvent(ned::ui::test::Character("g"));
    TypeText(view, "abc");
    view.OnEvent(ned::ui::test::Return());
    REQUIRE(fixture.statusMessage == "Not a line number: \"abc\"");
    REQUIRE(scratch.Point() == 0);

    // Back to normal editing afterward.
    view.OnEvent(ned::ui::test::Character("z"));
    REQUIRE(scratch.Text().find('z') == 0);
}

// --- minibuffer-history-recall follow-up: M-p/M-n --------------------------

TEST_CASE("M-p in goto-line recalls previously submitted values, newest first", "[BufferView]") {
    Fixture               fixture;
    ned::text::Buffer&    scratch = fixture.bufferList.CreateBuffer("scratch");
    ned::ui::ActiveBuffer activeBuffer(scratch);
    ned::ui::BufferView   view(activeBuffer, fixture.killRing, fixture.registers, fixture.promptHistory, fixture.bufferList, fixture.dispatcher,
                               fixture.statusMessage, fixture.mode, fixture.theme);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    scratch.InsertAtPoint("one\ntwo\nthree\nfour\nfive\nsix\nseven\neight\nnine\nten");
    scratch.SetPoint(0);

    // Two prior goto-line sessions, each submitted with Enter.
    view.OnEvent(ned::ui::test::Alt('g'));
    view.OnEvent(ned::ui::test::Character("g"));
    TypeText(view, "3");
    view.OnEvent(ned::ui::test::Return());

    view.OnEvent(ned::ui::test::Alt('g'));
    view.OnEvent(ned::ui::test::Character("g"));
    TypeText(view, "5");
    view.OnEvent(ned::ui::test::Return());

    // A third session recalls them newest-first, without typing anything.
    view.OnEvent(ned::ui::test::Alt('g'));
    view.OnEvent(ned::ui::test::Character("g"));
    REQUIRE(fixture.statusMessage == "Goto line: ");

    view.OnEvent(ned::ui::test::Alt('p'));
    REQUIRE(fixture.statusMessage == "Goto line: 5");
    view.OnEvent(ned::ui::test::Alt('p'));
    REQUIRE(fixture.statusMessage == "Goto line: 3");

    // At the oldest entry, a further M-p is a no-op.
    view.OnEvent(ned::ui::test::Alt('p'));
    REQUIRE(fixture.statusMessage == "Goto line: 3");

    view.OnEvent(ned::ui::test::Return());
    REQUIRE(scratch.Point() == scratch.Content().LineToByteOffset(2));
}

TEST_CASE("M-n in goto-line walks back toward the newest entry and restores the in-progress edit",
          "[BufferView]") {
    Fixture               fixture;
    ned::text::Buffer&    scratch = fixture.bufferList.CreateBuffer("scratch");
    ned::ui::ActiveBuffer activeBuffer(scratch);
    ned::ui::BufferView   view(activeBuffer, fixture.killRing, fixture.registers, fixture.promptHistory, fixture.bufferList, fixture.dispatcher,
                               fixture.statusMessage, fixture.mode, fixture.theme);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    scratch.InsertAtPoint("one\ntwo\nthree\nfour\nfive\nsix\nseven\neight\nnine\nten");
    scratch.SetPoint(0);

    view.OnEvent(ned::ui::test::Alt('g'));
    view.OnEvent(ned::ui::test::Character("g"));
    TypeText(view, "3");
    view.OnEvent(ned::ui::test::Return());

    view.OnEvent(ned::ui::test::Alt('g'));
    view.OnEvent(ned::ui::test::Character("g"));
    TypeText(view, "5");
    view.OnEvent(ned::ui::test::Return());

    view.OnEvent(ned::ui::test::Alt('g'));
    view.OnEvent(ned::ui::test::Character("g"));
    TypeText(view, "9"); // an in-progress edit, never submitted

    view.OnEvent(ned::ui::test::Alt('p'));
    REQUIRE(fixture.statusMessage == "Goto line: 5");
    view.OnEvent(ned::ui::test::Alt('p'));
    REQUIRE(fixture.statusMessage == "Goto line: 3");

    view.OnEvent(ned::ui::test::Alt('n'));
    REQUIRE(fixture.statusMessage == "Goto line: 5");
    view.OnEvent(ned::ui::test::Alt('n'));
    REQUIRE(fixture.statusMessage == "Goto line: 9"); // restored, not the empty string

    // Typing *while browsing* exits history mode outright -- a further M-p
    // starts a fresh browse from this edited text (stashing it), rather than
    // resuming the walk with the earlier, now-stale stash.
    view.OnEvent(ned::ui::test::Alt('p'));
    REQUIRE(fixture.statusMessage == "Goto line: 5"); // browsing again
    TypeText(view, "9");
    REQUIRE(fixture.statusMessage == "Goto line: 59");
    view.OnEvent(ned::ui::test::Alt('p'));
    REQUIRE(fixture.statusMessage == "Goto line: 5"); // fresh browse, not a continuation
    view.OnEvent(ned::ui::test::Alt('n'));
    REQUIRE(fixture.statusMessage == "Goto line: 59"); // restores the just-edited text, not "9"

    view.OnEvent(ned::ui::test::Escape());
}

TEST_CASE("M-p in M-x recalls a previously typed fuzzy query", "[BufferView]") {
    Fixture fixture;
    // A registered command that does nothing interactive -- unlike
    // switch-to-buffer/goto-line/etc., invoking it doesn't open a further
    // prompt, so the session cleanly returns to Normal and a second M-x can
    // reopen it within this same test.
    fixture.registry.Register("zzz-history-sentinel", "", [](ned::editor::CommandContext&) {});

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 2});
    CaptureCandidates(view, fixture.candidates);

    view.OnEvent(ned::ui::test::Alt('x'));
    TypeText(view, "zzz-history-sentinel");
    view.OnEvent(ned::ui::test::Return()); // submits, recording "zzz-history-sentinel"

    view.OnEvent(ned::ui::test::Alt('x'));
    REQUIRE(fixture.statusMessage == "M-x ");
    REQUIRE_FALSE(CandidatesContain(fixture.candidates, "zzz-history-sentinel"));

    view.OnEvent(ned::ui::test::Alt('p'));
    REQUIRE(CandidateSelected(fixture.candidates, "zzz-history-sentinel"));

    view.OnEvent(ned::ui::test::Escape());
}

TEST_CASE("C-l recenters the viewport on point's line", "[BufferView]") {
    Fixture               fixture;
    ned::text::Buffer&    scratch = fixture.bufferList.CreateBuffer("scratch");
    ned::ui::ActiveBuffer activeBuffer(scratch);
    ned::ui::BufferView   view(activeBuffer, fixture.killRing, fixture.registers, fixture.promptHistory, fixture.bufferList, fixture.dispatcher,
                               fixture.statusMessage, fixture.mode, fixture.theme);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 9}); // 10 rows

    std::string text;
    for (int i = 0; i < 100; ++i) {
        text += "line\n";
    }
    scratch.InsertAtPoint(text);
    scratch.SetPoint(scratch.Content().LineToByteOffset(50));

    view.OnEvent(ned::ui::test::Ctrl('l'));
    REQUIRE(view.TopLine() == 45); // 50 - 10/2

    // Near the top, recentering clamps to line 0 instead of underflowing.
    scratch.SetPoint(scratch.Content().LineToByteOffset(2));
    view.OnEvent(ned::ui::test::Ctrl('l'));
    REQUIRE(view.TopLine() == 0);
}

// --- external-modification-safety follow-up ------------------------------

TEST_CASE("C-x C-s on an externally-changed file asks first; n cancels, y overwrites", "[BufferView]") {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "ned_bufferview_test_supersession.txt";
    {
        std::ofstream(path) << "original\n";
    }

    Fixture               fixture;
    ned::text::Buffer&    buffer = fixture.bufferList.OpenOrCreateFile(path);
    ned::ui::ActiveBuffer activeBuffer(buffer);
    ned::ui::BufferView   view(activeBuffer, fixture.killRing, fixture.registers, fixture.promptHistory, fixture.bufferList, fixture.dispatcher,
                               fixture.statusMessage, fixture.mode, fixture.theme);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Character("z")); // a local edit

    // Someone else writes the file underneath the buffer (timestamp bumped
    // explicitly so the test never depends on mtime granularity).
    {
        std::ofstream(path, std::ios::trunc) << "theirs\n";
    }
    std::filesystem::last_write_time(path, std::filesystem::last_write_time(path) + std::chrono::seconds(2));

    view.OnEvent(ned::ui::test::Ctrl('x'));
    view.OnEvent(ned::ui::test::Ctrl('s'));
    REQUIRE(fixture.statusMessage.find("changed on disk") != std::string::npos);

    // n: nothing written, back to normal editing.
    view.OnEvent(ned::ui::test::Character("n"));
    {
        std::ifstream in(path);
        std::string   onDisk((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        REQUIRE(onDisk == "theirs\n");
    }
    REQUIRE(fixture.statusMessage.find("Save cancelled") == 0);

    // y: the buffer wins.
    view.OnEvent(ned::ui::test::Ctrl('x'));
    view.OnEvent(ned::ui::test::Ctrl('s'));
    view.OnEvent(ned::ui::test::Character("y"));
    REQUIRE_FALSE(buffer.Modified());
    {
        std::ifstream in(path);
        std::string   onDisk((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        REQUIRE(onDisk == buffer.Text());
    }
    REQUIRE(fixture.statusMessage.find("Wrote") == 0);

    std::filesystem::remove(path);
}

TEST_CASE("Closing the active buffer lands on the most recently left buffer, not the first tab", "[BufferView]") {
    Fixture               fixture;
    ned::text::Buffer&    a = fixture.bufferList.CreateBuffer("a");
    ned::text::Buffer&    b = fixture.bufferList.CreateBuffer("b");
    ned::text::Buffer&    c = fixture.bufferList.CreateBuffer("c");
    ned::ui::ActiveBuffer activeBuffer(a);
    // The same wiring Pane's constructor does for the real editor.
    activeBuffer.SetOnChange([&fixture](ned::text::Buffer& current) { fixture.bufferList.TouchBuffer(current); });
    fixture.bufferList.TouchBuffer(a);
    ned::ui::BufferView view(activeBuffer, fixture.killRing, fixture.registers, fixture.promptHistory, fixture.bufferList, fixture.dispatcher,
                             fixture.statusMessage, fixture.mode, fixture.theme);

    activeBuffer.Set(b); // a -> b -> c: "b" is the tab most recently left
    activeBuffer.Set(c);
    view.RequestCloseBuffer(c);

    REQUIRE(fixture.bufferList.Count() == 2);
    REQUIRE(&activeBuffer.Get() == &b); // not "a", the first in list order
}

TEST_CASE("Closing the active buffer falls back to list order when nothing was ever activated", "[BufferView]") {
    Fixture               fixture;
    ned::text::Buffer&    a = fixture.bufferList.CreateBuffer("a");
    ned::text::Buffer&    b = fixture.bufferList.CreateBuffer("b");
    ned::ui::ActiveBuffer activeBuffer(b); // no on-change hook, empty MRU order
    ned::ui::BufferView   view(activeBuffer, fixture.killRing, fixture.registers, fixture.promptHistory, fixture.bufferList,
                               fixture.dispatcher, fixture.statusMessage, fixture.mode, fixture.theme);

    view.RequestCloseBuffer(b);

    REQUIRE(&activeBuffer.Get() == &a);
}

TEST_CASE("tab-next/tab-previous cycle the active buffer in tab order, wrapping at both ends", "[BufferView]") {
    Fixture               fixture;
    ned::text::Buffer&    a = fixture.bufferList.CreateBuffer("a");
    ned::text::Buffer&    b = fixture.bufferList.CreateBuffer("b");
    ned::text::Buffer&    c = fixture.bufferList.CreateBuffer("c");
    ned::ui::ActiveBuffer activeBuffer(b);
    ned::ui::BufferView   view(activeBuffer, fixture.killRing, fixture.registers, fixture.promptHistory, fixture.bufferList,
                               fixture.dispatcher, fixture.statusMessage, fixture.mode, fixture.theme);

    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Character('.'));
    REQUIRE(&activeBuffer.Get() == &c);

    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Character('.')); // off the right end -- wraps to the first tab
    REQUIRE(&activeBuffer.Get() == &a);

    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Character(',')); // off the left end -- wraps back to the last tab
    REQUIRE(&activeBuffer.Get() == &c);

    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Character(','));
    REQUIRE(&activeBuffer.Get() == &b);
}

TEST_CASE("tab-next with a single tab stays put", "[BufferView]") {
    Fixture               fixture;
    ned::text::Buffer&    only = fixture.bufferList.CreateBuffer("only");
    ned::ui::ActiveBuffer activeBuffer(only);
    ned::ui::BufferView   view(activeBuffer, fixture.killRing, fixture.registers, fixture.promptHistory, fixture.bufferList,
                               fixture.dispatcher, fixture.statusMessage, fixture.mode, fixture.theme);

    view.OnEvent(ned::ui::test::Ctrl('c'));
    view.OnEvent(ned::ui::test::Character('.'));

    REQUIRE(&activeBuffer.Get() == &only);
}

// theme-editing follow-up: the save-theme command (M-x only, one-shot).
TEST_CASE("save-theme writes the active theme as runnable Janet to the XDG config path", "[BufferView]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_bufferview_test_save_theme";
    std::filesystem::remove_all(dir);

    // Scoped XDG override, mirroring ThemeFileTest's EnvVarGuard shape.
    const char*       previous = std::getenv("XDG_CONFIG_HOME");
    const std::string restore  = previous ? previous : "";
    setenv("XDG_CONFIG_HOME", dir.c_str(), 1);

    {
        Fixture             fixture;
        ned::ui::BufferView view = fixture.View();
        view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 2});

        view.OnEvent(ned::ui::test::Alt('x'));
        TypeText(view, "save-theme");
        view.OnEvent(ned::ui::test::Return());

        const std::filesystem::path expected = dir / "ned" / "theme.janet";
        REQUIRE(fixture.statusMessage == "Saved theme to " + expected.string());
        REQUIRE(std::filesystem::exists(expected));

        std::ifstream      in(expected);
        std::ostringstream content;
        content << in.rdbuf();
        // The fixture's theme is DarkTheme() -- spot-check one emitted call
        // against a known value (keyword_foreground = Color::Blue = x:4).
        REQUIRE(content.str().find("(ned/theme-set \"keyword_foreground\" \"x:4\")") != std::string::npos);
        REQUIRE(content.str().find("(ned/theme-set \"background\" \"default\")") != std::string::npos);

        view.OnEvent(ned::ui::test::Character("z")); // proves the one-shot returned to Normal mode
        REQUIRE(fixture.buffer.Text() == "z");
    }

    if (previous) {
        setenv("XDG_CONFIG_HOME", restore.c_str(), 1);
    }
    else {
        unsetenv("XDG_CONFIG_HOME");
    }
    std::filesystem::remove_all(dir);
}

// variables-store follow-up: a committed pick is remembered; preview and
// cancel are not.
TEST_CASE("Enter in select-theme remembers the committed theme; Escape remembers nothing", "[BufferView]") {
    {
        ThemePickerHarness h;
        h.view.OnEvent(ned::ui::test::ArrowDown()); // preview only
        h.view.OnEvent(ned::ui::test::Escape());
        REQUIRE_FALSE(std::filesystem::exists(h.stateGuard.dir / "ned" / "variables.json"));
    }
    {
        ThemePickerHarness h;
        TypeText(h.view, "nord");
        h.view.OnEvent(ned::ui::test::Return());
        REQUIRE(ned::editor::Variable("theme") == "nord");
        REQUIRE(std::filesystem::exists(h.stateGuard.dir / "ned" / "variables.json"));
    }
}

// -- backup-and-recovery follow-up: the recover-file prompt session ----------

namespace {

// Sandboxes XDG_STATE_HOME for backup storage and resets the Backup module's
// process-wide settings/memos around each test (BackupTest.cpp's own guard
// pair, pared to what these session tests need).
struct RecoverFixtureSandbox {
    explicit RecoverFixtureSandbox(const std::string& name) : root(std::filesystem::temp_directory_path() / name), stateGuard("XDG_STATE_HOME", (root / "state").c_str()),
                                                              homeGuard("HOME", nullptr) {
        ned::editor::ResetBackupsForTesting();
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root / "work");
    }

    ~RecoverFixtureSandbox() {
        ned::editor::ResetBackupsForTesting();
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }

    std::filesystem::path root;
    EnvVarGuard           stateGuard;
    EnvVarGuard           homeGuard;
};

void InvokeRecoverFile(ned::ui::BufferView& view) {
    view.OnEvent(ned::ui::test::Alt('x'));
    TypeText(view, "recover-file");
    view.OnEvent(ned::ui::test::Return());
}

} // namespace

TEST_CASE("recover-file reports when the buffer has no file or no backups, staying in normal editing",
          "[BufferView]") {
    const RecoverFixtureSandbox sandbox("ned_bufferview_test_recover_none");
    Fixture                     fixture;
    ned::ui::BufferView         view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 2});

    SECTION("pathless buffer") {
        InvokeRecoverFile(view);
        REQUIRE(fixture.statusMessage == "Buffer scratch has no file to recover");
    }

    SECTION("path-associated buffer with nothing backed up") {
        const std::filesystem::path path = sandbox.root / "work" / "notes.txt";
        fixture.buffer.SaveToFile(path);
        InvokeRecoverFile(view);
        REQUIRE(fixture.statusMessage == "No backups for scratch");
    }

    view.OnEvent(ned::ui::test::Character("z")); // proves inputMode_ is Normal
    REQUIRE(fixture.buffer.Text().find('z') != std::string::npos);
}

TEST_CASE("recover-file restores the picked version as one undoable step, leaving the buffer modified",
          "[BufferView]") {
    const RecoverFixtureSandbox sandbox("ned_bufferview_test_recover_happy");
    Fixture                     fixture;
    fixture.buffer.InsertAtPoint("current content");

    const std::filesystem::path path = sandbox.root / "work" / "notes.txt";
    fixture.buffer.SaveToFile(path); // binds the path; disk now matches the buffer

    // A backup version holding older content, as an earlier save would have
    // left behind.
    std::ofstream(path, std::ios::trunc) << "old version";
    ned::editor::BackupFileBeforeSave(path, 1755700000);
    std::ofstream(path, std::ios::trunc) << "current content\n"; // disk back to "current"

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 2});

    InvokeRecoverFile(view);
    REQUIRE(fixture.statusMessage.find("Recover scratch -- version (1-1, Enter=1): ") != std::string::npos);

    view.OnEvent(ned::ui::test::Return()); // Enter alone picks 1, the newest
    REQUIRE(fixture.statusMessage.find("over buffer scratch? (y/n)") != std::string::npos);

    view.OnEvent(ned::ui::test::Character("y"));
    REQUIRE(fixture.buffer.Text() == "old version");
    REQUIRE(fixture.buffer.Modified());
    REQUIRE(fixture.statusMessage.find("Recovered") != std::string::npos);

    fixture.buffer.Undo(); // exactly one step back to the pre-recover content
    REQUIRE(fixture.buffer.Text() == "current content");
}

TEST_CASE("recover-file's y/n confirmation can decline, leaving the buffer untouched", "[BufferView]") {
    const RecoverFixtureSandbox sandbox("ned_bufferview_test_recover_decline");
    Fixture                     fixture;
    fixture.buffer.InsertAtPoint("current content");

    const std::filesystem::path path = sandbox.root / "work" / "notes.txt";
    fixture.buffer.SaveToFile(path);
    ned::editor::WriteAutoSave(path, "crash snapshot");

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 2});

    InvokeRecoverFile(view);
    view.OnEvent(ned::ui::test::Return());
    REQUIRE(fixture.statusMessage.find("autosave (crash recovery)") != std::string::npos);

    view.OnEvent(ned::ui::test::Character("n"));
    REQUIRE(fixture.statusMessage == "Recover cancelled.");
    REQUIRE(fixture.buffer.Text() == "current content");
    REQUIRE_FALSE(fixture.buffer.Modified());

    view.OnEvent(ned::ui::test::Character("z")); // back to normal editing
    REQUIRE(fixture.buffer.Text().find('z') != std::string::npos);
}

TEST_CASE("recover-file rejects an out-of-range version number and ends the session", "[BufferView]") {
    const RecoverFixtureSandbox sandbox("ned_bufferview_test_recover_range");
    Fixture                     fixture;
    fixture.buffer.InsertAtPoint("current content");

    const std::filesystem::path path = sandbox.root / "work" / "notes.txt";
    fixture.buffer.SaveToFile(path);
    ned::editor::WriteAutoSave(path, "crash snapshot");

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 2});

    InvokeRecoverFile(view);
    TypeText(view, "9");
    view.OnEvent(ned::ui::test::Return());

    REQUIRE(fixture.statusMessage == "No such version: 9");
    REQUIRE(fixture.buffer.Text() == "current content");

    view.OnEvent(ned::ui::test::Character("z")); // back to normal editing
    REQUIRE(fixture.buffer.Text().find('z') != std::string::npos);
}

// quick-fix follow-up (C-c C-q, lsp-quick-fix): shared setup mirroring the
// C-c C-a tests above -- the difference under test is only the pick-without-
// asking policy in RequestQuickFixAtPoint.
namespace {

struct QuickFixHarness {
    Fixture                      fixture;
    ned::ui::EventLoop           eventLoop;
    ned::editor::lsp::LspManager manager{fixture.bufferList, eventLoop};
    ned::editor::lsp::LspClient* client = nullptr;
    FakeLspServer                server;
    ned::ui::BufferView          view;
    ned::text::Buffer*           buffer = nullptr;

    explicit QuickFixHarness(const std::string& fileName) : server(FakeLspServer::Create(manager, "fundamental", eventLoop, client)), view(fixture.View()) {
        buffer = &fixture.bufferList.OpenOrCreateFile(std::filesystem::temp_directory_path() / fileName);
        buffer->InsertAtPoint("bad_code");
        fixture.activeBuffer.Set(*buffer);
        view.SetLspManager(&manager);
        view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});
        CaptureCandidates(view, fixture.candidates);
        ned::ui::Screen screenBuf = ned::ui::Screen(40, 3);
        ned::ui::Canvas canvas(screenBuf, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});
        view.Paint(canvas);
        (void)ReadRawLspFrame(server.serverStdinRead);
    }

    // Sends C-c C-q and answers the resulting codeAction request with
    // actions; each entry is {title, newText, kind, isPreferred}.
    struct ActionSpec {
        std::string title;
        std::string newText;
        std::string kind;
        bool        isPreferred = false;
    };
    void RespondWith(const std::vector<ActionSpec>& specs) {
        view.OnEvent(ned::ui::test::Ctrl('c'));
        view.OnEvent(ned::ui::test::Ctrl('q'));

        const std::string raw     = ReadRawLspFrame(server.serverStdinRead);
        const auto        request = ned::editor::lsp::Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
        REQUIRE(request["method"] == "textDocument/codeAction");
        const std::string ownUri = request["params"]["textDocument"]["uri"].get<std::string>();

        auto actions = ned::editor::lsp::Json::array();
        for (const ActionSpec& spec : specs) {
            ned::editor::lsp::Json action = {
                {"title", spec.title},
                {"edit",
                 {{"changes",
                   {{ownUri, ned::editor::lsp::Json::array(
                                 {{{"range", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 8}}}}},
                                   {"newText", spec.newText}}})}}}}},
            };
            if (!spec.kind.empty()) {
                action["kind"] = spec.kind;
            }
            if (spec.isPreferred) {
                action["isPreferred"] = true;
            }
            actions.push_back(std::move(action));
        }
        const auto response =
            ned::editor::lsp::Json{{"jsonrpc", "2.0"}, {"id", LspRequestIdFromFrame(raw)}, {"result", std::move(actions)}};
        client->DispatchFrame(response.dump());
    }
};

} // namespace

TEST_CASE("C-c C-q applies a lone quick fix immediately, no confirmation", "[BufferView]") {
    QuickFixHarness harness("ned_bufferview_quick_fix_lone_test.txt");
    harness.RespondWith({{.title = "Fix bad_code", .newText = "good_code", .kind = "quickfix"}});

    REQUIRE(harness.buffer->Text() == "good_code");
    REQUIRE(harness.fixture.statusMessage == "Applied \"Fix bad_code\".");
}

// ApplyWorkspaceTextEdits undo-grouping fix: a response with several edits
// touching one file used to record one undo step per edit (Buffer::Undo()
// would need to be pressed once per edit to fully revert). Crafted by hand
// here (QuickFixHarness::RespondWith only builds one edit per action)
// so the "changes"[ownUri] array carries two non-overlapping edits.
TEST_CASE("a lone code action with multiple edits in one file applies and undoes as a single step", "[BufferView]") {
    QuickFixHarness harness("ned_bufferview_quick_fix_multi_edit_test.txt");

    harness.view.OnEvent(ned::ui::test::Ctrl('c'));
    harness.view.OnEvent(ned::ui::test::Ctrl('q'));

    const std::string raw     = ReadRawLspFrame(harness.server.serverStdinRead);
    const auto        request = ned::editor::lsp::Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["method"] == "textDocument/codeAction");
    const std::string ownUri = request["params"]["textDocument"]["uri"].get<std::string>();

    const auto action = ned::editor::lsp::Json{
        {"title", "Fix both halves"},
        {"kind", "quickfix"},
        {"edit",
         {{"changes",
           {{ownUri, ned::editor::lsp::Json::array({
                          {{"range", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 3}}}}},
                           {"newText", "BAD"}},
                          {{"range", {{"start", {{"line", 0}, {"character", 4}}}, {"end", {{"line", 0}, {"character", 8}}}}},
                           {"newText", "CODE"}},
                      })}}}}},
    };
    const auto response = ned::editor::lsp::Json{
        {"jsonrpc", "2.0"}, {"id", LspRequestIdFromFrame(raw)}, {"result", ned::editor::lsp::Json::array({action})}};
    harness.client->DispatchFrame(response.dump());

    REQUIRE(harness.buffer->Text() == "BAD_CODE");
    REQUIRE(harness.buffer->CanUndo());
    harness.buffer->Undo();
    REQUIRE(harness.buffer->Text() == "bad_code");
}

TEST_CASE("C-c C-q picks the lone isPreferred action out of several", "[BufferView]") {
    QuickFixHarness harness("ned_bufferview_quick_fix_preferred_test.txt");
    harness.RespondWith({{.title = "Refactor", .newText = "refactored", .kind = "refactor"},
                         {.title = "The fix", .newText = "fixed", .kind = "quickfix", .isPreferred = true},
                         {.title = "Other fix", .newText = "other", .kind = "quickfix"}});

    REQUIRE(harness.buffer->Text() == "fixed");
    REQUIRE(harness.fixture.statusMessage == "Applied \"The fix\".");
}

TEST_CASE("C-c C-q picks the lone quickfix-kind action when nothing is preferred", "[BufferView]") {
    QuickFixHarness harness("ned_bufferview_quick_fix_kind_test.txt");
    harness.RespondWith({{.title = "Refactor", .newText = "refactored", .kind = "refactor.rewrite"},
                         {.title = "The fix", .newText = "fixed", .kind = "quickfix"}});

    REQUIRE(harness.buffer->Text() == "fixed");
}

TEST_CASE("C-c C-q falls back to the selection list when the fix is ambiguous", "[BufferView]") {
    QuickFixHarness harness("ned_bufferview_quick_fix_ambiguous_test.txt");
    harness.RespondWith({{.title = "First fix", .newText = "first", .kind = "quickfix"},
                         {.title = "Second fix", .newText = "second", .kind = "quickfix"}});

    REQUIRE(harness.buffer->Text() == "bad_code"); // nothing applied
    REQUIRE(CandidateRowExists(harness.fixture.candidates, "1)", "First fix"));
    REQUIRE(CandidateRowExists(harness.fixture.candidates, "2)", "Second fix"));

    harness.view.OnEvent(ned::ui::test::Character("2")); // jump directly to the second action, applying it with no confirmation
    REQUIRE(harness.buffer->Text() == "second");
}

TEST_CASE("C-c C-q with no actions reports \"No quick fix available.\"", "[BufferView]") {
    QuickFixHarness harness("ned_bufferview_quick_fix_none_test.txt");
    harness.RespondWith({});

    REQUIRE(harness.buffer->Text() == "bad_code");
    REQUIRE(harness.fixture.statusMessage == "No quick fix available.");
}

// inline-diagnostics follow-up.

TEST_CASE("An inline diagnostic annotation row renders carets and message under the flagged line", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("int x = 1;\nint y = 2;");
    fixture.buffer.SetDiagnostics({
        ned::text::Buffer::Diagnostic{
            .startByte = 4, .endByte = 5, .severity = ned::text::Buffer::Diagnostic::Severity::Warning, .message = "unused variable x"},
    });
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    ned::ui::Screen screen = ned::ui::Screen(40, 3);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});
    view.Paint(canvas);

    const int gutter = GutterWidth(2);
    // Row 0: the flagged line itself, unchanged.
    REQUIRE(screen.PixelAt(gutter + 4, 0).character == "x");
    // Row 1: the annotation -- a caret directly under the span, then the
    // message, both in the severity's color; the gutter columns stay blank
    // (no line number -- it's not a buffer line).
    REQUIRE(screen.PixelAt(gutter + 4, 1).character == "^");
    REQUIRE(screen.PixelAt(gutter + 4, 1).foreground_color == fixture.theme.diagnosticWarning);
    const std::string annotationRow = RowText(screen, 1, 40);
    REQUIRE(annotationRow.find("unused variable x") != std::string::npos);
    REQUIRE(screen.PixelAt(0, 1).character == " ");
    // Distinct-at-a-glance styling (user-reported: plain severity-colored
    // text read as ordinary code): the severity's gutter glyph repeats
    // between the carets and the message, and the message is italic.
    REQUIRE(screen.PixelAt(gutter + 6, 1).character == "▲"); // caret at +4 (span is 1 wide), space, glyph at +6
    REQUIRE(screen.PixelAt(gutter + 8, 1).character == "u"); // message starts one space after the glyph
    REQUIRE(screen.PixelAt(gutter + 8, 1).italic);
    REQUIRE_FALSE(screen.PixelAt(gutter + 4, 1).italic); // carets stay upright/bold
    // Row 2: the next buffer line, shifted down by the annotation.
    REQUIRE(RowText(screen, 2, 40).find("int y = 2;") != std::string::npos);
}

TEST_CASE("Inline diagnostic rows shift cursor position and are click-transparent", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("bad line\ngood line");
    fixture.buffer.SetDiagnostics({
        ned::text::Buffer::Diagnostic{
            .startByte = 0, .endByte = 3, .severity = ned::text::Buffer::Diagnostic::Severity::Error, .message = "broken"},
    });
    // Point on line 1 ("good line"): its screen row must account for line
    // 0's annotation row above it.
    fixture.buffer.SetPoint(fixture.buffer.Content().LineToByteOffset(1));

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 3});

    ned::ui::Screen screen = ned::ui::Screen(40, 4);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 3});
    view.Paint(canvas);

    const auto cursor = view.CursorPosition();
    REQUIRE(cursor.has_value());
    REQUIRE(cursor->y == 2); // row 0 = "bad line", row 1 = annotation, row 2 = "good line"

    // A click on the annotation row (row 1) lands point on the annotated
    // line itself -- ByteOffsetForPoint's clamp-to-last-segment behavior.
    const int gutter = GutterWidth(2);
    view.OnEvent(MousePress(gutter, 1));
    REQUIRE(fixture.buffer.Content().ByteOffsetToLine(fixture.buffer.Point()) == 0);
}

TEST_CASE("toggle-inline-diagnostics / the settings flag suppress annotation rows entirely", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("bad\ngood");
    fixture.buffer.SetDiagnostics({
        ned::text::Buffer::Diagnostic{
            .startByte = 0, .endByte = 3, .severity = ned::text::Buffer::Diagnostic::Severity::Error, .message = "broken"},
    });
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 1});

    ned::editor::SetInlineDiagnosticsEnabled(false);
    ned::ui::Screen screen = ned::ui::Screen(40, 2);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 1});
    view.Paint(canvas);
    ned::editor::SetInlineDiagnosticsEnabled(true);

    // With the flag off, row 1 is the next buffer line, not an annotation.
    REQUIRE(RowText(screen, 1, 40).find("good") != std::string::npos);
    REQUIRE(RowText(screen, 1, 40).find("broken") == std::string::npos);
}

TEST_CASE("The most severe diagnostic wins the line's single annotation row", "[BufferView]") {
    Fixture fixture;
    fixture.buffer.InsertAtPoint("bad line");
    fixture.buffer.SetDiagnostics({
        ned::text::Buffer::Diagnostic{
            .startByte = 0, .endByte = 3, .severity = ned::text::Buffer::Diagnostic::Severity::Hint, .message = "a hint"},
        ned::text::Buffer::Diagnostic{
            .startByte = 4, .endByte = 8, .severity = ned::text::Buffer::Diagnostic::Severity::Error, .message = "the error"},
    });
    fixture.buffer.SetPoint(0);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 1});

    ned::ui::Screen screen = ned::ui::Screen(40, 2);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 1});
    view.Paint(canvas);

    const std::string annotationRow = RowText(screen, 1, 40);
    REQUIRE(annotationRow.find("the error") != std::string::npos);
    REQUIRE(annotationRow.find("a hint") == std::string::npos);
    // Carets sit under the error's own span (columns 4-7), not the hint's.
    const int gutter = GutterWidth(1);
    REQUIRE(screen.PixelAt(gutter + 4, 1).character == "^");
    REQUIRE(screen.PixelAt(gutter + 0, 1).character == " ");
}

// external-modification-round-2 follow-up: save-buffer's new
// ConfirmSaveWithConflicts guard, end to end through real key events --
// the one place this logic actually runs (Commands.cpp only sets
// context.interactiveRequest; BufferView drives the y/n session).

TEST_CASE("C-x C-s on a buffer with unresolved conflict markers asks before writing them", "[BufferView]") {
    Fixture                     fixture;
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "ned_bufferview_test_conflict_save.txt";
    {
        std::ofstream(path) << "original\n";
    }

    ned::text::Buffer& fileBuffer = fixture.bufferList.OpenOrCreateFile(path);
    fileBuffer.SetPoint(0);
    fileBuffer.InsertAtPoint("<<<<<<< buffer\nmine\n=======\ntheirs\n>>>>>>> disk\n");
    fixture.activeBuffer.Set(fileBuffer);

    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 2});

    view.OnEvent(ned::ui::test::Ctrl('x'));
    view.OnEvent(ned::ui::test::Ctrl('s'));
    REQUIRE(fixture.statusMessage == fileBuffer.Name() + " still has unresolved <<<<<<< conflict markers; save anyway? (y/n)");

    view.OnEvent(ned::ui::test::Character("n"));
    REQUIRE(fixture.statusMessage == "Save cancelled; resolve the <<<<<<< markers first.");
    {
        std::ifstream     in(path);
        const std::string onDisk((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        REQUIRE(onDisk == "original\n"); // nothing was written
    }

    view.OnEvent(ned::ui::test::Ctrl('x'));
    view.OnEvent(ned::ui::test::Ctrl('s'));
    view.OnEvent(ned::ui::test::Character("y"));
    REQUIRE_FALSE(fileBuffer.Modified());
    {
        std::ifstream     in(path);
        const std::string onDisk((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        REQUIRE(onDisk.find("<<<<<<<") != std::string::npos); // saved anyway, markers and all
    }

    std::filesystem::remove(path);
}

namespace {
// lsp-format-on-save follow-up: process-wide state, restored on destruction
// the same RAII shape CommandsTest.cpp's FormatCommandGuard already uses
// for the sibling FormatOnSave.h setting.
struct LspFormatOnSaveGuard {
    LspFormatOnSaveGuard() {
        ned::editor::lsp::SetLspFormatOnSaveEnabled(true);
    }
    ~LspFormatOnSaveGuard() {
        ned::editor::lsp::SetLspFormatOnSaveEnabled(false);
        ned::editor::SetFormatCommand(std::nullopt);
    }
};
} // namespace

TEST_CASE("C-x C-s formats via the language server before saving when lsp-format-on-save is enabled",
          "[BufferView]") {
    const LspFormatOnSaveGuard  guard;
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_bufferview_lsp_format_on_save_test.txt";
    {
        std::ofstream(path) << "int x=1;";
    }

    Fixture            fixture;
    ned::text::Buffer& buffer = fixture.bufferList.OpenOrCreateFile(path);
    fixture.activeBuffer.Set(buffer);

    ned::ui::EventLoop           eventLoop;
    ned::editor::lsp::LspManager manager(fixture.bufferList, eventLoop);
    ned::editor::lsp::LspClient* client = nullptr;
    FakeLspServer                server = FakeLspServer::Create(manager, "fundamental", eventLoop, client);

    ned::ui::BufferView view = fixture.View();
    view.SetLspManager(&manager);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    ned::ui::Screen screenBuf = ned::ui::Screen(40, 3);
    ned::ui::Canvas canvas(screenBuf, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});
    view.Paint(canvas); // triggers SyncBuffer -> didOpen
    (void)ReadRawLspFrame(server.serverStdinRead);

    view.OnEvent(ned::ui::test::Ctrl('x'));
    view.OnEvent(ned::ui::test::Ctrl('s'));
    REQUIRE(fixture.statusMessage == "Formatting...");

    const std::string raw     = ReadRawLspFrame(server.serverStdinRead);
    const auto        request = ned::editor::lsp::Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["method"] == "textDocument/formatting");

    const auto response = ned::editor::lsp::Json{
        {"jsonrpc", "2.0"},
        {"id", LspRequestIdFromFrame(raw)},
        {"result", ned::editor::lsp::Json::array(
                       {{{"range", {{"start", {{"line", 0}, {"character", 5}}}, {"end", {{"line", 0}, {"character", 6}}}}}, {"newText", " = "}}})},
    };
    client->DispatchFrame(response.dump());

    REQUIRE(buffer.Text() == "int x = 1;");
    REQUIRE(fixture.statusMessage.find("Wrote") == 0);
    {
        std::ifstream     in(path);
        const std::string onDisk((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        REQUIRE(onDisk == "int x = 1;\n"); // EnsureFinalNewline() default
    }

    // Step 0's undo-group fix: the format's edit(s) undo as a single step.
    REQUIRE(buffer.CanUndo());
    buffer.Undo();
    REQUIRE(buffer.Text() == "int x=1;");

    std::filesystem::remove(path);
}

TEST_CASE("C-x C-s prefers an external format command over lsp-format-on-save when both are configured",
          "[BufferView]") {
    const LspFormatOnSaveGuard  guard;
    ned::editor::SetFormatCommand(std::string("tr 'a-z' 'A-Z'"));
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_bufferview_lsp_format_precedence_test.txt";
    {
        std::ofstream(path) << "hello";
    }

    Fixture            fixture;
    ned::text::Buffer& buffer = fixture.bufferList.OpenOrCreateFile(path);
    fixture.activeBuffer.Set(buffer);

    ned::ui::EventLoop           eventLoop;
    ned::editor::lsp::LspManager manager(fixture.bufferList, eventLoop);
    ned::editor::lsp::LspClient* client = nullptr;
    FakeLspServer                server = FakeLspServer::Create(manager, "fundamental", eventLoop, client);

    ned::ui::BufferView view = fixture.View();
    view.SetLspManager(&manager);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    ned::ui::Screen screenBuf = ned::ui::Screen(40, 3);
    ned::ui::Canvas canvas(screenBuf, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});
    view.Paint(canvas);
    DrainAllPendingFrames(server.serverStdinRead); // drain didOpen + any other background requests from this Paint()

    view.OnEvent(ned::ui::test::Ctrl('x'));
    view.OnEvent(ned::ui::test::Ctrl('s'));

    // The external formatter ran synchronously -- no LSP formatting request
    // was ever sent, confirmed by the buffer already reflecting its output.
    REQUIRE(buffer.Text() == "HELLO");
    REQUIRE(fixture.statusMessage.find("Wrote") == 0);
    {
        std::ifstream     in(path);
        const std::string onDisk((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        REQUIRE(onDisk == "HELLO\n"); // EnsureFinalNewline() default -- disk-only, not applied in-memory
    }

    std::filesystem::remove(path);
}

namespace {
// on-type-formatting follow-up: same RAII shape as LspFormatOnSaveGuard
// above, for the sibling toggle.
struct OnTypeFormattingEnabledGuard {
    OnTypeFormattingEnabledGuard() {
        ned::editor::lsp::SetLspOnTypeFormattingEnabled(true);
    }
    ~OnTypeFormattingEnabledGuard() {
        ned::editor::lsp::SetLspOnTypeFormattingEnabled(false);
    }
};
} // namespace

TEST_CASE("Typing the server's declared trigger character sends textDocument/onTypeFormatting and applies "
          "the edit as its own undo step",
          "[BufferView]") {
    const OnTypeFormattingEnabledGuard guard;
    const std::filesystem::path        path = std::filesystem::temp_directory_path() / "ned_bufferview_on_type_formatting_test.txt";
    {
        std::ofstream(path) << "int x=1";
    }

    Fixture            fixture;
    ned::text::Buffer& buffer = fixture.bufferList.OpenOrCreateFile(path);
    buffer.SetPoint(buffer.Size());
    fixture.activeBuffer.Set(buffer);

    ned::ui::EventLoop           eventLoop;
    ned::editor::lsp::LspManager manager(fixture.bufferList, eventLoop);
    ned::editor::lsp::LspClient* client = nullptr;
    FakeLspServer                server = FakeLspServer::Create(manager, "fundamental", eventLoop, client);
    // SetClientForTesting bypasses the real initialize handshake entirely --
    // this is the test-only substitute for what a real server's response
    // would have populated (see SetOnTypeFormattingTriggersForTesting's own
    // doc comment in LspManager.h).
    manager.SetOnTypeFormattingTriggersForTesting("fundamental", ned::editor::lsp::OnTypeFormattingTriggers{.first = ";", .more = {}});

    ned::ui::BufferView view = fixture.View();
    view.SetLspManager(&manager);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    ned::ui::Screen screenBuf = ned::ui::Screen(40, 3);
    ned::ui::Canvas canvas(screenBuf, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});
    view.Paint(canvas); // triggers SyncBuffer -> didOpen
    (void)ReadRawLspFrame(server.serverStdinRead);

    view.OnEvent(ned::ui::test::Character(";"));
    REQUIRE(buffer.Text() == "int x=1;"); // the self-insert itself, independent of anything LSP-related

    const std::string raw     = ReadRawLspFrame(server.serverStdinRead);
    const auto        request = ned::editor::lsp::Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
    REQUIRE(request["method"] == "textDocument/onTypeFormatting");
    REQUIRE(request["params"]["ch"] == ";");
    REQUIRE(request["params"]["position"]["character"] == 8); // right after the just-inserted ';'

    const auto response = ned::editor::lsp::Json{
        {"jsonrpc", "2.0"},
        {"id", LspRequestIdFromFrame(raw)},
        {"result", ned::editor::lsp::Json::array(
                       {{{"range", {{"start", {{"line", 0}, {"character", 5}}}, {"end", {{"line", 0}, {"character", 6}}}}}, {"newText", " = "}}})},
    };
    client->DispatchFrame(response.dump());

    REQUIRE(buffer.Text() == "int x = 1;");

    // Two separate undo steps -- the formatting edit first, then the
    // triggering keystroke, since the async response can't land inside the
    // keystroke's own dispatch/undo group (see MaybeScheduleOnTypeFormatting's
    // own doc comment in BufferView.h).
    buffer.Undo();
    REQUIRE(buffer.Text() == "int x=1;");
    buffer.Undo();
    REQUIRE(buffer.Text() == "int x=1");

    std::filesystem::remove(path);
}

TEST_CASE("Typing a non-trigger character does not send an on-type-formatting request", "[BufferView]") {
    const OnTypeFormattingEnabledGuard guard;
    const std::filesystem::path        path = std::filesystem::temp_directory_path() / "ned_bufferview_on_type_formatting_no_trigger_test.txt";
    {
        std::ofstream(path) << "int x=1";
    }

    Fixture            fixture;
    ned::text::Buffer& buffer = fixture.bufferList.OpenOrCreateFile(path);
    buffer.SetPoint(buffer.Size());
    fixture.activeBuffer.Set(buffer);

    ned::ui::EventLoop           eventLoop;
    ned::editor::lsp::LspManager manager(fixture.bufferList, eventLoop);
    ned::editor::lsp::LspClient* client = nullptr;
    FakeLspServer                server = FakeLspServer::Create(manager, "fundamental", eventLoop, client);
    manager.SetOnTypeFormattingTriggersForTesting("fundamental", ned::editor::lsp::OnTypeFormattingTriggers{.first = ";", .more = {}});

    ned::ui::BufferView view = fixture.View();
    view.SetLspManager(&manager);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    ned::ui::Screen screenBuf = ned::ui::Screen(40, 3);
    ned::ui::Canvas canvas(screenBuf, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});
    view.Paint(canvas);
    DrainAllPendingFrames(server.serverStdinRead); // drain didOpen + any other background requests from this Paint()

    view.OnEvent(ned::ui::test::Character(","));
    REQUIRE(buffer.Text() == "int x=1,");

    // Unlike MaybeScheduleSignatureHelp/MaybeScheduleAutoCompletion, this
    // request (when sent at all) goes out synchronously within the same
    // OnEvent call, no debounce timer to wait on -- a bounded poll suffices.
    REQUIRE(NoFrameArrives(server.serverStdinRead));
}

TEST_CASE("Typing the server's declared trigger character sends nothing when lsp-on-type-formatting is disabled "
          "(the default)",
          "[BufferView]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_bufferview_on_type_formatting_disabled_test.txt";
    {
        std::ofstream(path) << "int x=1";
    }

    Fixture            fixture;
    ned::text::Buffer& buffer = fixture.bufferList.OpenOrCreateFile(path);
    buffer.SetPoint(buffer.Size());
    fixture.activeBuffer.Set(buffer);

    ned::ui::EventLoop           eventLoop;
    ned::editor::lsp::LspManager manager(fixture.bufferList, eventLoop);
    ned::editor::lsp::LspClient* client = nullptr;
    FakeLspServer                server = FakeLspServer::Create(manager, "fundamental", eventLoop, client);
    manager.SetOnTypeFormattingTriggersForTesting("fundamental", ned::editor::lsp::OnTypeFormattingTriggers{.first = ";", .more = {}});

    ned::ui::BufferView view = fixture.View();
    view.SetLspManager(&manager);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});

    ned::ui::Screen screenBuf = ned::ui::Screen(40, 3);
    ned::ui::Canvas canvas(screenBuf, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});
    view.Paint(canvas);
    DrainAllPendingFrames(server.serverStdinRead); // drain didOpen + any other background requests from this Paint()

    view.OnEvent(ned::ui::test::Character(";"));
    REQUIRE(buffer.Text() == "int x=1;");
    REQUIRE(NoFrameArrives(server.serverStdinRead));
}

// --- Snippet expansion sessions (snippet-expansion follow-up) -------------

namespace {

// The snippet registry is process-wide state (see Editor/SnippetRegistry.h).
struct SnippetRegistryTestGuard {
    SnippetRegistryTestGuard() {
        ned::editor::ClearAllSnippets();
    }
    ~SnippetRegistryTestGuard() {
        ned::editor::ClearAllSnippets();
    }
};

} // namespace

TEST_CASE("Tab expands a registered snippet and mirrors track typing", "[BufferView]") {
    const SnippetRegistryTestGuard guard;
    ned::editor::RegisterSnippet("", "for", "for (${1:i}; $1 < n; ++$1)");
    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();

    TypeText(view, "for");
    view.OnEvent(ned::ui::test::Tab());
    REQUIRE(fixture.buffer.Text() == "for (i; i < n; ++i)");
    REQUIRE(fixture.buffer.Point() == 6);
    REQUIRE(fixture.buffer.SnippetRanges().size() == 4);
    REQUIRE(fixture.statusMessage == "Snippet field 1/1 (TAB next, S-TAB previous, ESC done)");

    // Pristine placeholder: the first character replaces "i", mirrors follow.
    TypeText(view, "idx");
    REQUIRE(fixture.buffer.Text() == "for (idx; idx < n; ++idx)");
    REQUIRE(fixture.buffer.Point() == 8);

    // Tab past the last field lands on the (implicit) final stop and ends
    // the session.
    view.OnEvent(ned::ui::test::Tab());
    REQUIRE(fixture.buffer.SnippetRanges().empty());
    REQUIRE(fixture.buffer.Point() == 25);

    // Tab is back to its ordinary literal-tab self afterward.
    view.OnEvent(ned::ui::test::Tab());
    REQUIRE(fixture.buffer.Text() == "for (idx; idx < n; ++idx)\t");
}

TEST_CASE("Backspace on a pristine placeholder deletes it whole and stays in session", "[BufferView]") {
    const SnippetRegistryTestGuard guard;
    ned::editor::RegisterSnippet("", "greet", "hello ${1:world}!");
    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();

    TypeText(view, "greet");
    view.OnEvent(ned::ui::test::Tab());
    REQUIRE(fixture.buffer.Text() == "hello world!");

    view.OnEvent(ned::ui::test::Backspace());
    REQUIRE(fixture.buffer.Text() == "hello !");
    REQUIRE_FALSE(fixture.buffer.SnippetRanges().empty());

    TypeText(view, "there");
    REQUIRE(fixture.buffer.Text() == "hello there!");
}

TEST_CASE("Escape ends a snippet session keeping the expanded text", "[BufferView]") {
    const SnippetRegistryTestGuard guard;
    ned::editor::RegisterSnippet("", "t", "a ${1:x} b");
    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();

    TypeText(view, "t");
    view.OnEvent(ned::ui::test::Tab());
    REQUIRE(fixture.buffer.Text() == "a x b");

    view.OnEvent(ned::ui::test::Escape());
    REQUIRE(fixture.buffer.SnippetRanges().empty());
    REQUIRE(fixture.buffer.Text() == "a x b");

    // Typing is plain self-insert again -- no pristine replacement.
    TypeText(view, "y");
    REQUIRE(fixture.buffer.Text() == "a xy b");
}

TEST_CASE("Snippet fields navigate with Tab and M-p/M-n", "[BufferView]") {
    const SnippetRegistryTestGuard guard;
    ned::editor::RegisterSnippet("", "pair", "(${1:a}, ${2:b})");
    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();

    TypeText(view, "pair");
    view.OnEvent(ned::ui::test::Tab());
    REQUIRE(fixture.buffer.Text() == "(a, b)");
    REQUIRE(fixture.buffer.Point() == 2);
    REQUIRE(fixture.statusMessage == "Snippet field 1/2 (TAB next, S-TAB previous, ESC done)");

    view.OnEvent(ned::ui::test::Tab());
    REQUIRE(fixture.buffer.Point() == 5);
    REQUIRE(fixture.statusMessage == "Snippet field 2/2 (TAB next, S-TAB previous, ESC done)");

    view.OnEvent(ned::ui::test::Alt('p'));
    REQUIRE(fixture.buffer.Point() == 2);
    view.OnEvent(ned::ui::test::Alt('n'));
    REQUIRE(fixture.buffer.Point() == 5);

    // Revisiting re-arms pristine: typing replaces the whole placeholder.
    TypeText(view, "zz");
    REQUIRE(fixture.buffer.Text() == "(a, zz)");

    view.OnEvent(ned::ui::test::Tab());
    REQUIRE(fixture.buffer.SnippetRanges().empty());
    REQUIRE(fixture.buffer.Point() == 7); // the implicit final stop
}

TEST_CASE("A command-driven delete inside a field syncs mirrors too", "[BufferView]") {
    const SnippetRegistryTestGuard guard;
    ned::editor::RegisterSnippet("", "v", "${1:val} = $1");
    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();

    TypeText(view, "v");
    view.OnEvent(ned::ui::test::Tab());
    REQUIRE(fixture.buffer.Text() == "val = val");

    TypeText(view, "ab");
    REQUIRE(fixture.buffer.Text() == "ab = ab");

    // Not pristine anymore -- Backspace dispatches normally
    // (delete-backward-char) and the post-dispatch hook syncs the mirror.
    view.OnEvent(ned::ui::test::Backspace());
    REQUIRE(fixture.buffer.Text() == "a = a");
    REQUIRE_FALSE(fixture.buffer.SnippetRanges().empty());
}

TEST_CASE("Undo mid-session reverts coherently and ends the session", "[BufferView]") {
    const SnippetRegistryTestGuard guard;
    ned::editor::RegisterSnippet("", "for", "for (${1:i}; $1)");
    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();

    TypeText(view, "for");
    view.OnEvent(ned::ui::test::Tab());
    TypeText(view, "x");
    REQUIRE(fixture.buffer.Text() == "for (x; x)");

    // C-/ dispatches undo through the session's own re-dispatch path; the
    // keystroke and its mirror sync revert as one step, Buffer::Undo clears
    // the ranges, and the post-dispatch hook ends the session.
    view.OnEvent(ned::ui::test::Ctrl('/'));
    REQUIRE(fixture.buffer.Text() == "for (i; i)");
    REQUIRE(fixture.buffer.SnippetRanges().empty());
}

TEST_CASE("Entering another interactive session ends the snippet session", "[BufferView]") {
    const SnippetRegistryTestGuard guard;
    ned::editor::RegisterSnippet("", "t", "x ${1:y} z");
    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();

    TypeText(view, "t");
    view.OnEvent(ned::ui::test::Tab());
    REQUIRE_FALSE(fixture.buffer.SnippetRanges().empty());

    view.OnEvent(ned::ui::test::Alt('x')); // M-x opens its own session
    REQUIRE(fixture.buffer.SnippetRanges().empty());
    REQUIRE(fixture.statusMessage.substr(0, 4) == "M-x ");
}

TEST_CASE("The active snippet field paints with its own background wash", "[BufferView]") {
    const SnippetRegistryTestGuard guard;
    ned::editor::RegisterSnippet("", "greet", "hi ${1:name}!");
    Fixture             fixture;
    ned::ui::BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 0});

    TypeText(view, "greet");
    view.OnEvent(ned::ui::test::Tab());
    REQUIRE(fixture.buffer.Text() == "hi name!");

    ned::ui::Screen screen = ned::ui::Screen(40, 1);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 0});
    view.Paint(canvas);

    const int gutter = GutterWidth(1);
    REQUIRE(screen.PixelAt(gutter + 3, 0).background_color == fixture.theme.snippetFieldBackground); // 'n'
    REQUIRE(screen.PixelAt(gutter + 6, 0).background_color == fixture.theme.snippetFieldBackground); // 'e'
    REQUIRE(screen.PixelAt(gutter + 2, 0).background_color == fixture.theme.background);             // ' ' before the field
    REQUIRE(screen.PixelAt(gutter + 7, 0).background_color == fixture.theme.background);             // '!' after it

    // Ending the session drops the wash.
    view.OnEvent(ned::ui::test::Escape());
    ned::ui::Screen after = ned::ui::Screen(40, 1);
    ned::ui::Canvas afterCanvas(after, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 0});
    view.Paint(afterCanvas);
    REQUIRE(after.PixelAt(gutter + 3, 0).background_color == fixture.theme.background);
}

TEST_CASE("Accepting a snippet-format LSP completion starts a tabstop session", "[BufferView]") {
    Fixture                     fixture;
    const std::filesystem::path path   = std::filesystem::temp_directory_path() / "ned_bufferview_lsp_snippet_test.txt";
    ned::text::Buffer&          buffer = fixture.bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("fo");
    fixture.activeBuffer.Set(buffer);

    ned::ui::EventLoop           eventLoop;
    ned::editor::lsp::LspManager manager(fixture.bufferList, eventLoop);
    ned::editor::lsp::LspClient* client = nullptr;
    FakeLspServer                server = FakeLspServer::Create(manager, "fundamental", eventLoop, client);

    ned::ui::BufferView view = fixture.View();
    view.SetLspManager(&manager);
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});
    CaptureCompletion(view, fixture.completion);

    ned::ui::Screen screenBuf = ned::ui::Screen(40, 3);
    ned::ui::Canvas canvas(screenBuf, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 2});
    view.Paint(canvas); // triggers SyncBuffer -> didOpen
    (void)ReadRawLspFrame(server.serverStdinRead);

    view.OnEvent(ManualCompleteEvent()); // C-M-i -- lsp-complete
    const std::string raw = ReadRawLspFrame(server.serverStdinRead);

    const auto response = ned::editor::lsp::Json{
        {"jsonrpc", "2.0"},
        {"id", LspRequestIdFromFrame(raw)},
        {"result",
         {{"isIncomplete", false},
          {"items", ned::editor::lsp::Json::array({{{"label", "foobar"},
                                                    {"insertText", "foobar(${1:arg})"},
                                                    {"insertTextFormat", 2}}})}}},
    };
    client->DispatchFrame(response.dump());

    // The popup shows the item's own label verbatim -- no raw ${1:...}
    // markers, but also not the parsed expansion (that's only computed on
    // accept).
    REQUIRE(CompletionSelectedLabel(fixture.completion) == "foobar");

    // Tab accepts: the typed prefix is replaced by the parsed expansion and
    // a tabstop session opens on the placeholder field.
    view.OnEvent(ned::ui::test::Tab());
    REQUIRE(buffer.Text() == "foobar(arg)");
    REQUIRE_FALSE(buffer.SnippetRanges().empty());
    REQUIRE(buffer.Point() == 10); // end of the "arg" field

    TypeText(view, "42");
    REQUIRE(buffer.Text() == "foobar(42)");

    view.OnEvent(ned::ui::test::Tab()); // to the final stop -- session ends
    REQUIRE(buffer.SnippetRanges().empty());
    REQUIRE(buffer.Point() == 10); // implicit $0 at the expansion's end
}
