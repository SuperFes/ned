#include <clocale>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>

#include <ox/ox.hpp>

#include "Application.h"
#include "Editor/Commands.h"
#include "Editor/Dispatcher.h"
#include "Editor/Keymap.h"
#include "Editor/Mode.h"
#include "Editor/ModeOverrides.h"
#include "Editor/ProjectRoot.h"
#include "Editor/ScriptingSession.h"
#include "Janet/EditorBindings.h"
#include "Janet/Environment.h"
#include "Janet/InitFile.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"
#include "UI/ActiveBuffer.h"
#include "UI/BufferView.h"
#include "UI/EchoArea.h"
#include "UI/ModeLine.h"
#include "UI/ProjectSidebar.h"
#include "UI/ScrollArrowButton.h"
#include "UI/SidebarToggle.h"
#include "UI/TabBar.h"
#include "UI/TerminalColorProbe.h"
#include "UI/Theme.h"
#include "UI/ThemeFile.h"

using namespace ox;

namespace {

// `ned --detect-theme [--transparent] [output-path]`: probes the terminal's
// actual configured colors (see UI/TerminalColorProbe.h for why this can't
// just happen on every launch) and writes a Theme file, then exits without
// starting the editor UI at all -- this must run and finish strictly before
// any ox::Terminal is constructed.
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
        theme.background          = ox::TermColor::Default;
        theme.echoArea.background = ox::TermColor::Default;
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

    ned::text::KillRing killRing;

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
    // v1 scope cut, not an oversight; per-buffer Mode selection needs
    // BufferView to hold a rebindable Mode the same way ActiveBuffer now
    // makes the buffer rebindable, which is its own unit of work.
    ned::ui::ActiveBuffer activeBuffer(*buffer);

    // Priority order: the user's own Janet-defined bindings win over the
    // major mode's, which win over the global defaults.
    ned::editor::Dispatcher dispatcher(registry, ned::editor::KeymapStack({&janetKeymap, &mode.keymap, &globalKeymap}));

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

    auto head = Column{
        ned::ui::TabBar{activeBuffer, bufferList, theme} | SizePolicy::fixed(1),
        Row{
            // Always visible, even with the sidebar hidden -- the
            // mouse-clickable way to show/hide ProjectSidebar (round-2
            // sidebar follow-up); C-c C-p does the same thing from the
            // keyboard. Must live outside ProjectSidebar itself: once that
            // widget's own .active flips false it stops being laid out
            // entirely, so a toggle drawn inside it would disappear along
            // with it.
            ned::ui::SidebarToggle{theme.scrollBar} | SizePolicy::fixed(1),
            // Starting width: 30 columns total, 29 for the tree itself and 1
            // reserved for the divider against BufferView (see
            // ProjectSidebar::paint). Just the initial value -- dragging the
            // divider (round-2 sidebar follow-up) overwrites size_policy at
            // runtime; see ProjectSidebar::UpdateResize.
            ned::ui::ProjectSidebar{activeBuffer, bufferList, statusMessage, theme} | SizePolicy::fixed(30),
            ned::ui::BufferView{activeBuffer, killRing, bufferList, dispatcher, statusMessage, mode, theme},
            Column{
                ned::ui::ScrollArrowButton{U'▲', theme.scrollBar, theme.scrollBarDisabled} | SizePolicy::fixed(1),
                // ScrollBar's own default SizePolicy is fixed(1), meant for
                // its usual position as a direct Row child (1 column wide).
                // Nested one level deeper inside this Column, that same
                // fixed(1) would instead pin its *height* to 1 row -- override
                // it to flex so it fills the space between the two arrows.
                ScrollBar{ScrollBar::Options{.brush = theme.scrollBar}} | SizePolicy::flex(),
                ned::ui::ScrollArrowButton{U'▼', theme.scrollBar, theme.scrollBarDisabled} | SizePolicy::fixed(1),
            } | SizePolicy::fixed(1),
        },
        ned::ui::ModeLine{activeBuffer, mode, theme} | SizePolicy::fixed(1),
        ned::ui::EchoArea{statusMessage, theme} | SizePolicy::fixed(1),
    };

    auto& [tabBar, bufferRow, modeLine, echoArea]                   = head.children;
    auto& [sidebarToggle, projectSidebar, bufferView, scrollColumn] = bufferRow.children;
    auto& [scrollUpArrow, scrollBar, scrollDownArrow]               = scrollColumn.children;

    // Two-way sync, both driven from outside BufferView so it stays unaware
    // of sl::Signal: paint() pushes topLine_/total lines into the bar every
    // frame (see BufferView::SetScrollBar), and dragging/wheeling the bar
    // itself calls back into SetTopLine here. The arrow caps step by a
    // single line per click, deliberately finer-grained than the bar's own
    // wheel/drag gestures.
    bufferView.SetScrollBar(&scrollBar);
    bufferView.SetScrollArrows(&scrollUpArrow, &scrollDownArrow);
    bufferView.SetProjectSidebar(&projectSidebar);
    bufferView.SetSidebarRow(&bufferRow);
    sidebarToggle.SetSidebar(&projectSidebar);
    sidebarToggle.SetSidebarRow(&bufferRow);
    projectSidebar.SetSidebarRow(&bufferRow);
    // project-root-detection follow-up: makes it clear, right at startup,
    // which file in the (possibly VCS-root-detected, not just the opened
    // file's own directory) project tree corresponds to what's actually
    // open -- otherwise the file could be buried behind several collapsed
    // ancestor directories with no visible indication of where it is.
    if (buffer->Path()) {
        projectSidebar.RevealPath(*buffer->Path());
    }
    tabBar.SetOnCloseRequest([&bufferView](ned::text::Buffer& buffer) { bufferView.RequestCloseBuffer(buffer); });
    scrollBar.on_scroll.connect([&bufferView](int position) { bufferView.SetTopLine(static_cast<std::size_t>(position)); });
    scrollUpArrow.SetOnClick([&bufferView] {
        const std::size_t top = bufferView.TopLine();
        bufferView.SetTopLine(top > 0 ? top - 1 : 0);
    });
    scrollDownArrow.SetOnClick([&bufferView] { bufferView.SetTopLine(bufferView.TopLine() + 1); });

    // Auto-saved-scratch-pads follow-up: not started by BufferView's own
    // constructor (every test-constructed BufferView would otherwise spin up
    // a real background thread) -- only the real, running editor opts in.
    bufferView.StartAutoSaveTimer();

    Focus::set(bufferView);

    // Signals::Off is required, not cosmetic: with the default Signals::On,
    // the OS tty driver intercepts C-c/C-z/C-s/C-q/C-v itself (SIGINT/SIGTSTP/
    // XON-XOFF flow control) and they never reach key_press at all -- e.g.
    // C-x C-s (save-buffer) would silently never fire. Real terminal Emacs
    // disables this for the same reason; we bind what these keys do ourselves.
    // MouseMode::Drag (TermOx's default is already Basic -- press/release/wheel
    // for all buttons; Drag additionally reports move events while a button is
    // held, which BufferView needs for click-and-drag selection) rather than
    // Move, which would report movement even with no button held and isn't
    // needed here.
    auto terminal = Terminal{Terminal::Options{.mouse_mode = MouseMode::Drag, .signals = Signals::Off}};

    return Application{head, std::move(terminal)}.run();
}
