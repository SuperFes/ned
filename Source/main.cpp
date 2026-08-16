#include <clocale>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include "Application.h"
#include "Editor/Commands.h"
#include "Editor/Dispatcher.h"
#include "Editor/Keymap.h"
#include "Editor/Mode.h"
#include "Editor/ModeOverrides.h"
#include "Editor/ProjectRoot.h"
#include "Editor/Register.h"
#include "Editor/ScriptingSession.h"
#include "Janet/EditorBindings.h"
#include "Janet/Environment.h"
#include "Janet/InitFile.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"
#include "UI/ActiveBuffer.h"
#include "UI/EchoArea.h"
#include "UI/ProjectSidebar.h"
#include "UI/SidebarToggle.h"
#include "UI/TabBar.h"
#include "UI/TerminalColorProbe.h"
#include "UI/Theme.h"
#include "UI/ThemeFile.h"
#include "UI/WindowManager.h"

using namespace ftxui;

namespace {

// `ned --detect-theme [--transparent] [output-path]`: probes the terminal's
// actual configured colors (see UI/TerminalColorProbe.h for why this can't
// just happen on every launch) and writes a Theme file, then exits without
// starting the editor UI at all -- this must run and finish strictly before
// any ftxui::ScreenInteractive is constructed (TermOx -> FTXUI migration:
// was "before any ox::Terminal is constructed" -- same constraint, ScreenInteractive
// is what starts reading stdin now).
int RunDetectTheme(int argc, char** argv) {
    bool                       transparent = false;
    std::optional<std::string> outputPath;

    for (int i = 2; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--transparent") {
            transparent = true;
        }
        else if (!outputPath) {
            outputPath = std::string(arg);
        }
    }

    const ned::ui::DetectedColors detected = ned::ui::ProbeTerminalColors();
    ned::ui::Theme                theme    = ned::ui::BuildDetectedTheme(detected, ned::ui::DarkTheme());

    if (transparent) {
        theme.background          = ned::ui::Color::Default;
        theme.echoArea.background = ned::ui::Color::Default;
    }

    try {
        const std::filesystem::path path = outputPath ? std::filesystem::path(*outputPath) : ned::ui::ThemeFilePath();
        ned::ui::SaveThemeFile(theme, path);

        std::cout << "Wrote detected theme to " << path.string() << '\n';
        if (!detected.background) {
            std::cout << "Note: the terminal didn't respond to the background-color query in time; "
                         "using defaults for anything not detected.\n";
        }
    }
    catch (const std::exception& e) {
        std::cerr << "ned: " << e.what() << '\n';
        return 1;
    }

    return 0;
}

// Picks a Mode by a file's full path (Phase 5, extended by the tree-sitter
// foundation follow-up, the bundle-remaining-grammars follow-up, and the
// mode-overrides follow-up) -- still a single lookup done once at startup
// from the initial file, not per-buffer; see this function's caller for why
// that scope cut predates tree-sitter and isn't being revisited here. Perl
// has no extension mapping -- no bundled grammar exists for it, see
// Languages.h.
//
// Checks ned::editor::ModeForFileOverride first (mode-overrides follow-up,
// widening the dynamic-grammar-loading follow-up's original dynamic-only
// override into a general one) -- a filename or extension a Janet
// init.janet mapped via ned/set-mode-for-filename/ned/set-mode-for-extension
// wins over this function's own hardcoded table below, whether it points at
// a grammar loaded at runtime (ned/register-language-grammar) or at one of
// the bundled *Mode() functions by name, so an override can replace a
// bundled mapping too, not just add a new one. This is also what makes a
// filename with no distinguishing extension -- "CMakeLists.txt" being the
// motivating case, ".txt" alone can't tell it apart from any other text
// file -- reachable at all: ModeForFileOverride checks the *filename* table
// before the extension table, matching Emacs' auto-mode-alist convention
// that a more specific pattern wins. Safe to call unconditionally even if
// nothing was ever registered/mapped -- ModeForFileOverride returns
// std::nullopt cleanly in that case, falling straight through to the
// existing bundled table below.
ned::editor::Mode ModeForPath(const std::filesystem::path& path) {
    if (auto overrideMode = ned::editor::ModeForFileOverride(path); overrideMode) {
        return std::move(*overrideMode);
    }
    const std::filesystem::path extension = path.extension();
    if (extension == ".janet") {
        return ned::editor::JanetMode();
    }
    if (extension == ".json") {
        return ned::editor::JsonMode();
    }
    if (extension == ".c" || extension == ".h") {
        return ned::editor::CMode();
    }
    if (extension == ".cpp" || extension == ".cc" || extension == ".cxx" || extension == ".hpp" || extension == ".hh") {
        return ned::editor::CppMode();
    }
    if (extension == ".php" || extension == ".phtml") {
        return ned::editor::PhpMode();
    }
    if (extension == ".js" || extension == ".mjs" || extension == ".cjs") {
        return ned::editor::JavaScriptMode();
    }
    if (extension == ".ts" || extension == ".mts" || extension == ".cts") {
        return ned::editor::TypeScriptMode();
    }
    if (extension == ".tsx") {
        return ned::editor::TsxMode();
    }
    if (extension == ".html" || extension == ".htm") {
        return ned::editor::HtmlMode();
    }
    if (extension == ".css") {
        return ned::editor::CssMode();
    }
    if (extension == ".py" || extension == ".pyw") {
        return ned::editor::PythonMode();
    }
    if (extension == ".sh" || extension == ".bash") {
        return ned::editor::BashMode();
    }
    if (extension == ".md" || extension == ".markdown") {
        return ned::editor::MarkdownMode();
    }
    return ned::editor::FundamentalMode();
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string_view(argv[1]) == "--detect-theme") {
        return RunDetectTheme(argc, argv);
    }

    std::setlocale(LC_ALL, "");

    Ned::Application::SetTitle("Ned");

    ned::text::BufferList bufferList;
    std::string           statusMessage;

    // Whether argv[1] is a directory decides two independent things below:
    // it's never handed to OpenOrCreateFile (a directory can't be opened as
    // a file's content -- Buffer::FromFile would just throw), and per
    // DetectProjectRoot's own rule it becomes the project root outright,
    // bypassing VCS detection ("if we just open a directory, that can be
    // the project root regardless" -- the user's own words).
    std::error_code argIsDirectoryEc;
    const bool      argIsDirectory = argc > 1 && std::filesystem::is_directory(argv[1], argIsDirectoryEc);

    ned::text::Buffer* buffer = nullptr;
    if (argc > 1 && !argIsDirectory) {
        const bool isNewFile = !std::filesystem::exists(argv[1]);
        try {
            buffer = &bufferList.OpenOrCreateFile(argv[1]);
            if (isNewFile) {
                statusMessage = "(New file)";
            }
        }
        catch (const std::exception& e) {
            statusMessage = e.what();
        }
    }
    if (buffer == nullptr) {
        buffer = &bufferList.CreateBuffer("scratch");
    }

    // project-root-detection follow-up: computed once here from whatever
    // was opened, not re-derived later (see ProjectRoot.h's own doc
    // comment). No CLI argument at all falls back to the existing "just use
    // cwd" behavior, unchanged -- an explicit, narrow scope cut, not an
    // oversight: DetectProjectRoot's "a directory is always the root
    // outright" rule is specifically for an *explicitly* opened directory,
    // and there's no file path to derive a smarter default from otherwise.
    ned::editor::SetProjectRoot(
        ned::editor::DetectProjectRoot(argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::current_path()));

    ned::text::KillRing        killRing;
    ned::editor::RegisterTable registers;

    ned::editor::CommandRegistry registry;
    ned::editor::RegisterBuiltinCommands(registry);
    ned::editor::Keymap globalKeymap = ned::editor::BuildDefaultGlobalKeymap();
    ned::editor::Keymap janetKeymap;

    ned::janet::Environment janetEnv;
    ned::janet::InstallEditorBindings(janetEnv);

    // Kept alive for the whole run: ned/* Janet functions resolve "the
    // current CommandContext" through this for as long as it's in scope.
    const ned::editor::ScriptingSessionScope scriptingSession(ned::editor::ScriptingSession{registry, janetKeymap});

    try {
        ned::janet::LoadInitFile(janetEnv);
    }
    catch (const std::exception& e) {
        statusMessage = std::string("init.janet error: ") + e.what();
    }

    ned::editor::Mode mode = buffer->Path() ? ModeForPath(*buffer->Path()) : ned::editor::FundamentalMode();

    // The active Mode is still selected once at startup from the initial
    // file's extension, not per-buffer -- switching to a differently-typed
    // file via find-file/switch-to-buffer (multi-file-support follow-up)
    // does not change which Mode/highlighting is active. A known, explicit
    // v1 scope cut, not an oversight (window-splitting follow-up: each
    // WindowManager pane now owns its own independent copy of whatever Mode
    // it was created with -- see WindowManager.h's own header comment -- so
    // this gap is now "a pane keeps the Mode it was split/created with,
    // even after switching buffers inside it," the same shape as before,
    // just genuinely per-pane instead of a single global).
    //
    // Uses a previously `ned --detect-theme`-generated file if one exists
    // (never probes the terminal on a normal launch -- see
    // UI/TerminalColorProbe.h), else the fixed DarkTheme() default. Theme
    // selection is still not Janet-scriptable -- see ROADMAP.md's Phase 6
    // notes for that scope call.
    const ned::ui::Theme theme = [] {
        try {
            if (const auto loaded = ned::ui::LoadThemeFile(ned::ui::ThemeFilePath())) {
                return *loaded;
            }
        }
        catch (const std::exception&) {
            // Missing XDG_CONFIG_HOME/HOME or an unreadable file -- fall back below.
        }
        return ned::ui::DarkTheme();
    }();

    // TermOx -> FTXUI migration: every widget is now a heap-allocated,
    // shared_ptr-owned ftxui::Component (Widget derives from ComponentBase,
    // which requires shared_ptr ownership throughout FTXUI) rather than a
    // stack-allocated aggregate member decomposed via structured bindings.
    // Typed shared_ptrs are kept around so widget-specific methods
    // (SetScrollBar, RevealPath, etc.) can still be called directly, the
    // same cross-widget wiring the pre-migration version did through its
    // own structured-binding references.
    //
    // Window-splitting follow-up: WindowManager now owns everything that
    // used to be a single BufferView/ModeLine/ScrollBar/pair-of-
    // ScrollArrowButtons -- see WindowManager.h's own header comment for
    // why each of those had to become genuinely per-pane rather than one
    // shared instance (a prefix-key sequence in progress belongs to
    // whichever pane is receiving keystrokes, a rebindable ActiveBuffer
    // belongs to whichever pane is showing it, etc.). mode is moved in,
    // not copied here -- WindowManager's own initial pane takes ownership
    // of exactly this one value; every later pane created by a split gets
    // its own independent copy (see WindowManager.cpp's own comment on
    // where that copy is taken from).
    auto windowManager = std::make_shared<ned::ui::WindowManager>(
        *buffer, killRing, registers, bufferList, registry, janetKeymap, globalKeymap, std::move(mode),
        statusMessage, theme);

    // TabBar/ProjectSidebar are still single, shared-app-wide widgets (real
    // Emacs has no per-window tab strip or file browser either) -- but a
    // click on either should always target whichever pane currently has
    // keyboard focus, which changes over time as the user splits/switches
    // windows. Both now take a provider callback instead of a fixed
    // ActiveBuffer& for exactly this reason (see TabBar.h/ProjectSidebar.h's
    // own header comments).
    auto tabBar = std::make_shared<ned::ui::TabBar>(
        [wm = windowManager.get()]() -> ned::ui::ActiveBuffer& { return wm->FocusedActiveBuffer(); }, bufferList,
        theme);

    // Always visible, even with the sidebar hidden -- the mouse-clickable
    // way to show/hide ProjectSidebar (round-2 sidebar follow-up); C-c C-p
    // does the same thing from the keyboard. Must live outside
    // ProjectSidebar itself: once that widget's own .active flips false it
    // stops being rendered entirely (see the Maybe() wrapping below), so a
    // toggle drawn inside it would disappear along with it.
    auto sidebarToggle = std::make_shared<ned::ui::SidebarToggle>(theme.scrollBar);

    auto projectSidebar = std::make_shared<ned::ui::ProjectSidebar>(
        [wm = windowManager.get()]() -> ned::ui::ActiveBuffer& { return wm->FocusedActiveBuffer(); }, bufferList,
        statusMessage, theme);

    auto echoArea = std::make_shared<ned::ui::EchoArea>(statusMessage, theme);

    windowManager->SetProjectSidebar(projectSidebar.get());
    projectSidebar->SetOnBufferClosed(
        [wm = windowManager.get()](ned::text::Buffer& buffer) { wm->NotifyBufferClosing(buffer); });
    sidebarToggle->SetSidebar(projectSidebar.get());
    // project-root-detection follow-up: makes it clear, right at startup,
    // which file in the (possibly VCS-root-detected, not just the opened
    // file's own directory) project tree corresponds to what's actually
    // open -- otherwise the file could be buried behind several collapsed
    // ancestor directories with no visible indication of where it is.
    if (buffer->Path()) {
        projectSidebar->RevealPath(*buffer->Path());
    }
    tabBar->SetOnCloseRequest(
        [wm = windowManager.get()](ned::text::Buffer& buffer) { wm->RequestCloseBuffer(buffer); });

    // ProjectSidebar's own width is drag-resizable at runtime (divider drag
    // -- see ProjectSidebar::UpdateResize), so its size() decorator can't be
    // a fixed value computed once at composition time the way every other
    // widget's is -- the lambda below is re-invoked fresh every frame (this
    // project's own FTXUI migration confirmed operator|(Component,
    // ElementDecorator) rebuilds the wrapping Renderer's Element fresh per
    // Render() call, by reading FTXUI's actual renderer.cpp source, not
    // assumed), so it always reflects whatever ProjectSidebar::Width()
    // currently is. Maybe(..., &projectSidebar->active) is the direct FTXUI
    // answer to the old TermOx ox::Widget::active flag: when false, it
    // swaps in an empty zero-size placeholder instead of ever calling
    // ProjectSidebar::Render() at all, matching the old "excluded from
    // layout entirely" behavior exactly, and (per FTXUI's own Maybe
    // implementation) suppresses OnEvent delivery to it too while hidden.
    Component projectSidebarSized = projectSidebar | [raw = projectSidebar.get()](Element e) {
        return e | size(WIDTH, EQUAL, raw->Width());
    };
    Component projectSidebarFinal = Maybe(projectSidebarSized, &projectSidebar->active);

    Component bufferRow = Container::Horizontal({
        sidebarToggle | size(WIDTH, EQUAL, 1),
        projectSidebarFinal,
        windowManager->RootComponent() | [](Element e) { return flex(std::move(e)); },
    });

    Component head = Container::Vertical({
        tabBar | size(HEIGHT, EQUAL, 1),
        bufferRow | [](Element e) { return flex(std::move(e)); },
        echoArea | size(HEIGHT, EQUAL, 1),
    });

    // Must run here, after head is fully assembled -- not any earlier, and
    // WindowManager's own constructor deliberately doesn't call this either
    // (see WindowManager::TakeFocus's own doc comment for the real,
    // confirmed-via-manual-pty-testing reason: ComponentBase::TakeFocus()
    // walks up through real parent pointers, and none of bufferRow/head's
    // exist as real ancestors until this exact point).
    windowManager->TakeFocus();

    // Konsole-specific workaround, user-confirmed: TabBar (and everything
    // else) failed to show up at all on first launch, until the terminal
    // window was resized -- other terminals showed TabBar fine but still
    // had the (separately fixed, see BufferView::CursorPosition's own
    // comment) missing-cursor bug, so this is specifically about Konsole.
    // Root cause, traced through FTXUI's own source rather than guessed:
    // Screen::ToString() (screen.cpp) -- what paints every single frame --
    // emits row content via plain \r\n line breaks with no absolute
    // cursor-positioning escape of its own, entirely trusting the cursor is
    // already at (0,0) before the first byte is written. Every frame after
    // the first explicitly re-homes the cursor first (App::Internal::
    // Draw's own ResetPosition() call), but that call is unconditionally
    // skipped for frame 0, which instead relies entirely on entering the
    // terminal's alternate screen buffer (\033[?1049h, sent moments later
    // by ScreenInteractive::Fullscreen()'s own startup) having already
    // homed the cursor as a side effect -- true per the xterm spec and most
    // terminals' own behavior (confirmed: a real resize, which forces
    // FTXUI's full ResetPosition(resized=true) path on the very next frame,
    // fixes it every time), but apparently not reliably true in Konsole.
    // Entering the alternate screen buffer and homing the cursor ourselves,
    // first, sidesteps the bug entirely: FTXUI's own \033[?1049h moments
    // later becomes a harmless, idempotent re-entry into the buffer we
    // already switched to, leaving our own explicit home in place
    // regardless of whether Konsole's own entry would have preserved it.
    std::cout << "\033[?1049h\033[H" << std::flush;

    auto screen = ScreenInteractive::Fullscreen();

    // Auto-saved-scratch-pads follow-up: not started by BufferView's own
    // constructor (every test-constructed BufferView would otherwise spin up
    // a real background thread) -- only the real, running editor opts in.
    // Needs the owning ScreenInteractive so its background thread can
    // safely marshal the actual auto-save call back onto the main loop
    // thread via Post (documented thread-safe by FTXUI). Window-splitting
    // follow-up: moved from BufferView to WindowManager, the genuinely
    // whole-session-lifetime owner this timer semantically needs (see
    // WindowManager.h's own header comment).
    windowManager->StartAutoSaveTimer(screen);

    // ForceHandleCtrlC/Z(false) is required, not cosmetic -- TermOx ->
    // FTXUI migration: was Terminal::Options{.signals = Signals::Off}.
    // Confirmed by reading App::Internal's real event loop (app.cpp), not
    // assumed from the (easy-to-misread-backwards) header doc comment
    // alone: `force_handle_ctrl_c_` defaults to true, and true means
    // "always run FTXUI's own exit-on-Ctrl+C handling, even if the
    // component's OnEvent claims the event" -- i.e. the default is the
    // TermOx Signals::On-style trap this project needs off, not already
    // off. Leaving this at its default caused a real, reproducible crash-
    // shaped bug during this migration's own manual pty smoke test: any
    // C-c-prefixed binding (e.g. C-c C-p, toggle-project-sidebar) exited
    // the whole process the instant the first chord's Ctrl+C byte arrived,
    // before Dispatcher ever saw the full two-chord sequence. false makes
    // FTXUI only fall back to its own SIGINT/SIGTSTP handling when our own
    // component genuinely didn't handle the event, matching Signals::Off's
    // original intent (bindings we own always win).
    //
    // TrackMouse(true) is FTXUI's equivalent of TermOx's non-default
    // MouseMode::Drag -- motion events reported while a button is held,
    // which BufferView needs for click-and-drag selection (already FTXUI's
    // own default, set explicitly here so the intent isn't silently
    // dependent on that default never changing).
    screen.ForceHandleCtrlC(false);
    screen.ForceHandleCtrlZ(false);
    screen.TrackMouse(true);

    screen.Loop(head);

    return 0;
}
