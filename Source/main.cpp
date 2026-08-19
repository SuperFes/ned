#include <clocale>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include "Application.h"
#include "Editor/Commands.h"
#include "Editor/Dispatcher.h"
#include "Editor/Keymap.h"
#include "Editor/Lsp/LspManager.h"
#include "Editor/Mode.h"
#include "Editor/ModeOverrides.h"
#include "Editor/ProjectRoot.h"
#include "Editor/Register.h"
#include "Editor/ScriptingSession.h"
#include "Janet/EditorBindings.h"
#include "Janet/Environment.h"
#include "Janet/InitFile.h"
#include "Text/BinaryDetect.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"
#include "UI/ActiveBuffer.h"
#include "UI/EchoArea.h"
#include "UI/EventLoop.h"
#include "UI/Layout.h"
#include "UI/ProjectSidebar.h"
#include "UI/SidebarToggle.h"
#include "UI/TabBar.h"
#include "UI/TerminalColorProbe.h"
#include "UI/Theme.h"
#include "UI/ThemeFile.h"
#include "UI/WindowManager.h"

using namespace ned::ui;

namespace {

// `ned --detect-theme [--transparent] [output-path]`: probes the terminal's
// actual configured colors (see UI/TerminalColorProbe.h for why this can't
// just happen on every launch) and writes a Theme file, then exits without
// starting the editor UI at all -- this must run and finish strictly before
// any ned::ui::EventLoop is constructed (FTXUI -> Notcurses migration: was
// "before any ftxui::ScreenInteractive is constructed" -- same constraint,
// EventLoop's constructor is what calls notcurses_core_init, which is what
// starts reading stdin now).
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

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string_view(argv[1]) == "--detect-theme") {
        return RunDetectTheme(argc, argv);
    }

    std::setlocale(LC_ALL, "");

    Ned::Application::SetTitle("Ned");

    ned::text::BufferList bufferList;
    std::string           statusMessage;

    // open-binary-anyway follow-up: --force-binary is the CLI-argument-time
    // escape hatch for BufferList::OpenOrCreateFile's binary refusal --
    // there's no interactive session to ask a y/n confirmation through at
    // this point in startup (no EventLoop, no BufferView yet), so this is
    // the only override available for a file passed directly on the
    // command line. Accepted anywhere among the arguments, not just
    // immediately after the program name, so `ned --force-binary path` and
    // `ned path --force-binary` both work; the first non-flag argument is
    // taken as the path.
    bool        forceBinary = false;
    const char* pathArg     = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--force-binary") {
            forceBinary = true;
        }
        else if (pathArg == nullptr) {
            pathArg = argv[i];
        }
    }

    // Whether the path argument is a directory decides two independent
    // things below: it's never handed to OpenOrCreateFile (a directory
    // can't be opened as a file's content -- Buffer::FromFile would just
    // throw), and per DetectProjectRoot's own rule it becomes the project
    // root outright, bypassing VCS detection ("if we just open a
    // directory, that can be the project root regardless" -- the user's
    // own words).
    std::error_code argIsDirectoryEc;
    const bool      argIsDirectory = pathArg != nullptr && std::filesystem::is_directory(pathArg, argIsDirectoryEc);

    // open-binary-anyway follow-up: a plain BinaryFileError (not
    // --force-binary'd) falls all the way through to below, after the
    // whole UI/EventLoop exists, rather than being reported and dropped
    // here -- see the deferredBinaryOpenPath_ use site (right after
    // windowManager->TakeFocus()) for why: it hands off to
    // WindowManager::RequestOpenBinaryFile, the exact same y/n
    // confirmation pathway find-file and a sidebar click already use, so
    // `ned somebinaryfile` behaves consistently with every other way of
    // opening the same file instead of just refusing outright.
    ned::text::Buffer*    buffer = nullptr;
    std::filesystem::path deferredBinaryOpenPath;
    if (pathArg != nullptr && !argIsDirectory) {
        const bool isNewFile = !std::filesystem::exists(pathArg);
        try {
            buffer = &bufferList.OpenOrCreateFile(pathArg, forceBinary);
            if (isNewFile) {
                statusMessage = "(New file)";
            }
        }
        catch (const ned::text::BinaryFileError&) {
            deferredBinaryOpenPath = pathArg;
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
        ned::editor::DetectProjectRoot(pathArg != nullptr ? std::filesystem::path(pathArg) : std::filesystem::current_path()));

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

    ned::editor::Mode mode = ned::editor::ModeForBuffer(*buffer);

    // per-buffer-mode follow-up: this is just the *initial* Mode for
    // whichever pane WindowManager constructs first -- each Pane resyncs its
    // own Mode fresh (via ned::editor::ModeForBuffer, the same function used
    // here) whenever its active buffer actually changes (find-file,
    // switch-to-buffer, a tab/sidebar click, visiting a search result,
    // etc.), see BufferView::SetOnActiveBufferChanged and Pane's own wiring
    // of it in WindowManager.cpp. Mode is a property of the buffer being
    // viewed, not the pane; a pane's Mode is only ever "whatever its current
    // buffer resolves to."
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

    // FTXUI -> Notcurses migration: every widget is still heap-allocated
    // via shared_ptr, but no longer because anything requires it the way
    // ftxui::ComponentBase did -- ned::ui::Widget has no such ownership
    // contract at all (see Widget.h's own header comment). Kept anyway,
    // unchanged, purely so widget-specific methods (SetScrollBar,
    // RevealPath, etc.) can still be called directly by typed pointer, the
    // same cross-widget wiring this composition root already established.
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
    projectSidebar->SetOnBinaryFileOpenRequest(
        [wm = windowManager.get()](const std::filesystem::path& path) { wm->RequestOpenBinaryFile(path); });
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
    // -- see ProjectSidebar::UpdateResize), so it can't be a fixed value
    // computed once at composition time the way every other widget's is --
    // SizeSpec::DynamicFixed (Layout.h) is read fresh every single
    // Container::Paint() call (the direct replacement for FTXUI's own
    // per-frame ElementDecorator lambda, confirmed during the original
    // TermOx -> FTXUI migration to be re-invoked every Render() call), so
    // it always reflects whatever ProjectSidebar::Width() currently is.
    // There's no Maybe(...)-equivalent wrapper needed for
    // projectSidebar->active the way there was under FTXUI -- Container
    // itself already skips an inactive child's layout/paint/event-dispatch
    // entirely (see Layout.h's own header comment), so ProjectSidebar is
    // just handed to bufferRow directly below.

    // sidebar-header follow-up: tabBar now sits only above the pane area,
    // not above ProjectSidebar too -- ProjectSidebar spans the row that
    // used to belong to tabBar instead, using it for its own header (see
    // ProjectSidebar::Paint's own comment on that row).
    Container mainColumn(Axis::Vertical, {
                                             {tabBar.get(), SizeSpec::Fixed(1)},
                                             {&windowManager->RootComponent(), SizeSpec::Flex()},
                                         });

    Container bufferRow(Axis::Horizontal, {
                                              {sidebarToggle.get(), SizeSpec::Fixed(1)},
                                              {projectSidebar.get(), SizeSpec::DynamicFixed([raw = projectSidebar.get()] { return raw->Width(); })},
                                              {&mainColumn, SizeSpec::Flex()},
                                          });

    Container head(Axis::Vertical, {
                                       {&bufferRow, SizeSpec::Flex()},
                                       {echoArea.get(), SizeSpec::Fixed(1)},
                                   });

    // FTXUI -> Notcurses migration: WindowManager::TakeFocus's own doc
    // comment used to explain why this had to run here, after head was
    // fully assembled, rather than from WindowManager's own constructor --
    // ftxui::ComponentBase::TakeFocus() walked up through real *parent*
    // pointers that didn't exist yet at construction time. Widget::TakeFocus
    // (Widget.h) has no such dependency at all anymore -- it's a flat,
    // direct write to a process-wide registry, indifferent to whatever tree
    // shape does or doesn't exist around the target widget -- so this call
    // would now work identically from inside WindowManager's own
    // constructor too. Left at this exact call site anyway: moving it would
    // be a pure refactor with no behavior change, and keeping it here needs
    // no new reasoning to justify.
    windowManager->TakeFocus();

    // open-binary-anyway follow-up: deferred from the initial CLI-argument
    // open above, now that a focused pane actually exists to drive the y/n
    // confirmation through (RequestOpenBinaryFile routes to whichever pane
    // is focused -- see WindowManager.cpp). Must run after TakeFocus(), not
    // before: RequestOpenBinaryFile is a no-op unless FocusedPane() finds a
    // real focused pane.
    if (!deferredBinaryOpenPath.empty()) {
        windowManager->RequestOpenBinaryFile(deferredBinaryOpenPath);
    }

    // FTXUI -> Notcurses migration: the Konsole-specific workaround that
    // used to live here (entering the alternate screen buffer and homing
    // the cursor manually, before FTXUI's own ScreenInteractive::Fullscreen()
    // did, to sidestep a real FTXUI Screen::ToString()-specific first-frame
    // cursor-position bug -- see this file's own git history for the full
    // root-cause account) doesn't carry over: it was a workaround for a bug
    // in FTXUI's own frame-0 rendering logic specifically, and Notcurses
    // has an entirely different rendering pipeline (EventLoop's constructor
    // -- notcurses_core_init -- already owns entering the alternate screen
    // buffer and placing the cursor itself). Flagged here as a known Phase
    // 4 item: if a similar first-launch rendering glitch resurfaces on
    // Konsole under Notcurses, it needs fresh root-causing against
    // Notcurses' own renderer, not a blind reapplication of this exact fix.

    // FTXUI -> Notcurses migration: EventLoop's constructor is what starts
    // reading stdin now (see the --detect-theme branch's own comment above
    // for why RunDetectTheme must finish strictly before this point).
    EventLoop eventLoop;

    // LSP client follow-up: constructed here, not alongside bufferList/
    // killRing/registers above, since it needs a real EventLoop& to marshal
    // its background read-loop threads' work back onto the main thread
    // (LspClient.h's own header comment has the full lifetime requirement
    // -- must outlive eventLoop.Run() below, which this satisfies for free
    // as a plain local: ordinary reverse-declaration-order destruction at
    // the end of main() runs this after eventLoop.Run() has already
    // returned). Wired into windowManager via SetLspManager the same
    // "connect after construction, unset is a safe no-op" way
    // SetProjectSidebar already is -- every pane, present and future
    // (including ones created by a later split), gets it.
    ned::editor::lsp::LspManager lspManager(bufferList, eventLoop);
    windowManager->SetLspManager(&lspManager);

    // FTXUI -> Notcurses migration: BufferView's completion-debounce/
    // status-message-idle-timeout DeadlineTimers and ScrollArrowButton's
    // press-and-hold repeat both need a real EventLoop& too (see their own
    // SetEventLoop doc comments) -- forwarded to every pane, present and
    // future, the same "connect after construction" shape SetProjectSidebar/
    // SetLspManager already establish.
    windowManager->SetEventLoop(&eventLoop);

    // Auto-saved-scratch-pads follow-up: not started by BufferView's own
    // constructor (every test-constructed BufferView would otherwise spin up
    // a real background thread) -- only the real, running editor opts in.
    // Needs the owning EventLoop so its background thread can safely
    // marshal the actual auto-save call back onto the main loop thread via
    // Post (documented thread-safe -- see EventLoop.h's own header
    // comment). Window-splitting follow-up: moved from BufferView to
    // WindowManager, the genuinely whole-session-lifetime owner this timer
    // semantically needs (see WindowManager.h's own header comment).
    windowManager->StartAutoSaveTimer(eventLoop);

    // large-file-async-load follow-up: same "only the real, running editor
    // opts in, needs the owning EventLoop" reasoning as StartAutoSaveTimer
    // just above. Only takes effect for files opened from here on --
    // bufferList.OpenOrCreateFile(pathArg) already ran synchronously above
    // this point (before EventLoop existed at all), a documented, accepted
    // v1 gap: a huge file passed directly on the command line still blocks
    // the initial splash briefly, but every interactive open (find-file,
    // sidebar click, LSP jump-to-definition, etc.) happens well after this
    // and gets the async path.
    windowManager->EnableAsyncFileLoading(eventLoop);

    // FTXUI -> Notcurses migration: replaces ScreenInteractive::
    // ForceHandleCtrlC(false)/ForceHandleCtrlZ(false) -- see EventLoop's own
    // constructor comment for why Notcurses' own notcurses_linesigs_disable
    // (called there) is actually a strictly better fix for the exact same
    // "our own key bindings always win" C-c-prefixed-binding bug FTXUI's
    // ForceHandleCtrlC(false) used to guard against: no signal is ever
    // raised by the terminal's line discipline in the first place now,
    // rather than raised and then suppressed. TrackMouse(true)'s own
    // FTXUI-era equivalent (motion events reported while a button is held,
    // which BufferView needs for click-and-drag selection) is handled by
    // EventLoop's constructor too, via notcurses_mice_enable(NCMICE_ALL_EVENTS)
    // -- there's nothing left to set explicitly here for either concern.

    // A single Screen (Widget.h) reused across every frame, resized to
    // match the terminal on every onResize callback -- this composition
    // root's own direct replacement for what used to be an implicit
    // ftxui::Screen FTXUI itself owned and rebuilt every Render() call.
    Screen screenBuffer(0, 0);

    EventLoopCallbacks callbacks;
    callbacks.onResize = [&](Size size) {
        screenBuffer = Screen(size.width, size.height);
        head.SetBox_(Box{.x_min = 0, .x_max = size.width - 1, .y_min = 0, .y_max = size.height - 1});
    };
    // FTXUI -> Notcurses migration: replaces FTXUI's own ContainerBase
    // event-routing split -- a keyboard Event only ever reached whichever
    // child a container's internal focus-selector currently pointed at
    // (the real mechanism WindowManager::TakeFocus's whole walk-up-through-
    // parents machinery used to drive), while a mouse Event was broadcast
    // to every leaf regardless (see Widget.h's own header comment). Same
    // two-way split here, just far more directly: a keyboard Event goes
    // straight to FocusedWidget() (Widget.h's own flat registry, no tree
    // walk needed at all), and only a mouse Event is broadcast, via head's
    // own Container::OnEvent, to the whole tree.
    callbacks.onEvent = [&](const Event& event) {
        if (event.is_mouse()) {
            head.OnEvent(event);
        }
        else if (Widget* focused = FocusedWidget()) {
            focused->OnEvent(event);
        }
    };
    callbacks.render = [&]() -> std::optional<Point> {
        head.Paint(Canvas(screenBuffer, head.Box_()));
        screenBuffer.Flush(eventLoop.StdPlane());
        if (const Widget* focused = FocusedWidget()) {
            if (const std::optional<Point> local = focused->CursorPosition()) {
                const Box& box = focused->Box_();
                return Point{box.x_min + local->x, box.y_min + local->y};
            }
        }
        return std::nullopt;
    };

    eventLoop.Run(callbacks);

    return 0;
}
