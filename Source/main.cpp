#include <clocale>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <CLI/CLI.hpp>

#include "Application.h"

#include "Editor/BackgroundActivity.h"
#include "Editor/Backup.h"
#include "Editor/Commands.h"
#include "Editor/Dap/DapManager.h"
#include "Editor/Keymap.h"
#include "Editor/Lsp/LspManager.h"
#include "Editor/Mode.h"
#include "Editor/ModeOverrides.h"
#include "Editor/ProjectRoot.h"
#include "Editor/ProjectSession.h"
#include "Editor/ProjectTrust.h"
#include "Editor/Register.h"
#include "Editor/ScriptingSession.h"
#include "Editor/Session.h"
#include "Editor/TabWidth.h"
#include "Editor/Tasks/TaskRunner.h"
#include "Editor/Terminal/Config.h"
#include "Editor/ThemeSetting.h"
#include "Editor/Variables.h"
#include "Editor/Vcs/VcsRunner.h"

#include "Janet/EditorBindings.h"
#include "Janet/Environment.h"
#include "Janet/InitFile.h"
#include "Janet/PluginLoader.h"

#include "Text/BinaryDetect.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"

#include "UI/ActiveBuffer.h"
#include "UI/EchoArea.h"
#include "UI/EventLoop.h"
#include "UI/Layout.h"
#include "UI/Overlay.h"
#include "UI/ProjectSidebar.h"
#include "UI/TabBar.h"
#include "UI/TerminalColorProbe.h"
#include "UI/TerminalPanel.h"
#include "UI/Theme.h"
#include "UI/ThemeFile.h"
#include "UI/ThemeRegistry.h"
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
    // CLI11 parses argv[0] as the program name and everything from argv[1]
    // on as real arguments -- argv[1] here is literally the string
    // "--detect-theme" (that's how main() decided to call this function in
    // the first place), so it's handed to CLI11 as the (discarded) program
    // name by starting the parse one element past it, at argv[2].
    CLI::App app{"Probe the terminal's actual configured colors and write a Theme file, then exit."};

    bool        transparent = false;
    std::string outputPathArg;
    app.add_flag("--transparent", transparent,
                 "Treat the detected background as transparent instead of an opaque color");
    app.add_option("output-path", outputPathArg, "Where to write the theme file (default: the XDG theme file path)");

    try {
        app.parse(argc - 1, argv + 1);
    }
    catch (const CLI::ParseError& e) {
        return app.exit(e);
    }

    const std::optional<std::string> outputPath = outputPathArg.empty() ? std::nullopt : std::optional(outputPathArg);

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

auto main(int argc, char** argv) -> int {
    if (argc > 1 && std::string_view(argv[1]) == "--detect-theme") {
        return RunDetectTheme(argc, argv);
    }

    std::setlocale(LC_ALL, "");

    Ned::Application::SetTitle("Ned");

    ned::text::BufferList bufferList;
    std::string           statusMessage;

    // command-line-parameter-handling follow-up: CLI11 (header-only,
    // FetchContent'd in CMakeLists.txt) replaces the old hand-rolled
    // argv loop -- --force-binary is still the CLI-argument-time escape
    // hatch for BufferList::OpenOrCreateFile's binary refusal (there's no
    // interactive session to ask a y/n confirmation through at this point
    // in startup, no EventLoop, no BufferView yet), and `paths` now accepts
    // any number of file/directory arguments instead of silently keeping
    // only the first one -- `ned a.txt b.txt c.txt` opens all three as
    // buffers (see the extra-paths loop below) instead of just `a.txt`.
    CLI::App app{"Ned -- a terminal-based, Janet-scriptable text editor.", "ned"};

    bool                     forceBinary = false;
    bool                     noRestore   = false;
    std::vector<std::string> paths;
    app.add_flag("--force-binary", forceBinary,
                 "Open files that look binary anyway, without an interactive confirmation");
    app.add_flag("--no-restore", noRestore,
                 "Don't restore the project's saved session (open buffers, breakpoints, sidebar state)");
    app.add_option("paths", paths, "Files or directories to open");

    try {
        app.parse(argc, argv);
    }
    catch (const CLI::ParseError& e) {
        return app.exit(e);
    }

    const char* pathArg = paths.empty() ? nullptr : paths.front().c_str();

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
    std::filesystem::path deferredLargeOpenPath;
    if (pathArg != nullptr && !argIsDirectory) {
        // loose-ends follow-up: a large, legitimate text file passed on the
        // command line used to be the one open path that still loaded
        // synchronously (BufferList's async path needs the opener hook,
        // which needs EventLoop, which doesn't exist yet) -- blocking the
        // splash for however long the read took. Now deferred, exactly the
        // deferredBinaryOpenPath shape below, to right after
        // EnableAsyncFileLoading: the same "try later, once the machinery
        // exists" trick, triggering a deferred *load* instead of a deferred
        // confirmation prompt. The binary check keeps a large binary file
        // on the sync path so it still gets its BinaryFileError ->
        // interactive-confirmation flow (cheap: LooksBinary reads only the
        // first 8 KiB).
        std::error_code      sizeEc;
        const std::uintmax_t size = std::filesystem::file_size(pathArg, sizeEc);
        if (!sizeEc && size > ned::text::AsyncLoadThreshold() &&
            (forceBinary || !ned::text::LooksBinary(pathArg))) {
            deferredLargeOpenPath = pathArg;
        }
        else {
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
    }
    // session-persistence slice 2: the scratch-buffer fallback that used to
    // live right here moved below the project-session restore -- a restored
    // session's own buffers should become the startup view instead of an
    // empty scratch, and creating scratch first would leave it lingering as
    // a stray extra tab. Nothing between here and there dereferences buffer.

    // command-line-parameter-handling follow-up: any path arguments beyond
    // the first are opened as ordinary background buffers -- not shown in
    // the initial pane, not consulted for project-root detection (there's
    // only one root to detect), reachable immediately via the tab bar or
    // switch-to-buffer once the editor UI exists below. A directory among
    // them is silently skipped (nothing to open as a file's content). A
    // binary file among them just gets BufferList::OpenOrCreateFile's normal
    // refusal (pass --force-binary if that's not wanted) rather than the
    // interactive y/n confirmation the *first* argument gets further down
    // via deferredBinaryOpenPath -- there's no focused pane yet to drive
    // more than one such confirmation through at this point in startup.
    for (std::size_t i = 1; i < paths.size(); ++i) {
        std::error_code extraIsDirectoryEc;
        if (std::filesystem::is_directory(paths[i], extraIsDirectoryEc)) {
            continue;
        }
        try {
            bufferList.OpenOrCreateFile(paths[i], forceBinary);
        }
        catch (const std::exception&) {
            // Best-effort: one bad extra path shouldn't stop the rest of
            // startup, including the other paths still left to open.
        }
    }

    // project-root-detection follow-up: computed once here from whatever
    // was opened, not re-derived later (see ProjectRoot.h's own doc
    // comment). session-persistence slice 2 changed the no-CLI-argument
    // case: it used to fall back to "cwd is the root outright," which
    // can't tell a project from just-some-directory -- now it walks upward
    // from cwd for a VCS/.ned marker (FindProjectMarkerRoot), so `ned` run
    // from anywhere inside a project roots at, and restores the session
    // of, that project; a walk that finds nothing keeps the old plain-cwd
    // behavior. An *explicitly* opened directory keeps its "is the root
    // outright, no walk" rule unchanged.
    const std::filesystem::path projectRoot = [&]() -> std::filesystem::path {
        if (pathArg != nullptr) {
            return ned::editor::DetectProjectRoot(pathArg);
        }
        const std::filesystem::path cwd = std::filesystem::current_path();
        if (const auto markerRoot = ned::editor::FindProjectMarkerRoot(cwd)) {
            return *markerRoot;
        }
        return cwd;
    }();
    ned::editor::SetProjectRoot(projectRoot);

    // Only a real project -- a root actually carrying a VCS/.ned marker --
    // ever reads or writes a session file: without this, running `ned` from
    // $HOME would accumulate a junk "session" for the home directory.
    // --no-restore leaves the root unset entirely, making the whole run
    // session-inert in BOTH directions -- restoring nothing is easy, but it
    // must also not SAVE at quit, or `ned --no-restore quickfix.cpp` would
    // silently overwrite the project's real saved session with just that
    // one file. Establishing the root here (before init.janet loads,
    // below) is what lets ned/set-session-restore still veto everything:
    // every load/save path re-checks SessionRestoreEnabled() at use time.
    if (!noRestore && ned::editor::HasProjectMarker(projectRoot)) {
        ned::editor::SetActiveProjectSessionRoot(projectRoot);
    }

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
        // Registers the bundled reference VCS plugins (e.g. git) -- before
        // LoadInitFile so a user's own init.janet can override/unregister one
        // of them afterward (ned/vcs-register-provider re-registering under
        // the same name replaces it).
        ned::janet::LoadBundledPlugins(janetEnv);
    }
    catch (const std::exception& e) {
        statusMessage = std::string("bundled vcs plugin error: ") + e.what();
    }

    try {
        ned::janet::LoadInitFile(janetEnv);
    }
    catch (const std::exception& e) {
        statusMessage = std::string("init.janet error: ") + e.what();
    }

    // session-persistence slice 3: project-local .ned/init.janet, loaded
    // after the global init.janet so project config overrides user config
    // -- but NEVER silently: this is arbitrary code execution triggered by
    // opening a directory (the same concern class ROADMAP.md records
    // against Org Babel). A file whose exact content was previously
    // "always"-approved (and whose trust hasn't aged out unused -- see
    // ProjectTrust.h) loads right here, early enough for its mode
    // overrides/grammars to affect the initial buffer; anything else
    // defers to a y/n/a prompt once the UI exists, below. The trust store
    // loads after init.janet so a configured expiry window governs its
    // prune.
    const std::filesystem::path projectInitPath = projectRoot / ".ned" / "init.janet";
    std::filesystem::path       deferredTrustPromptPath;
    {
        std::error_code projectInitEc;
        if (std::filesystem::is_regular_file(projectInitPath, projectInitEc)) {
            ned::editor::LoadProjectTrust();
            const std::optional<std::string> hash = ned::editor::HashFileContent(projectInitPath);
            if (hash && ned::editor::IsProjectInitTrusted(projectInitPath, *hash)) {
                try {
                    janetEnv.DoFile(projectInitPath);
                }
                catch (const std::exception& e) {
                    statusMessage = std::string("project init.janet error: ") + e.what();
                }
                ned::editor::TouchProjectTrust(projectInitPath);
                ned::editor::SaveProjectTrust();
            }
            else {
                deferredTrustPromptPath = projectInitPath;
            }
        }
    }

    // session-persistence slice 1: deliberately after LoadInitFile, not
    // beside the CLI opens above -- init.janet is where ned/set-save-place
    // can turn this off, so the startup buffers' restore has to wait for it
    // (they were opened before the hook below existed, hence the explicit
    // loop). Every later open (find-file, sidebar click, LSP jump, ...)
    // funnels through BufferList's own on-file-opened seam instead.
    ned::editor::LoadFilePlaces();

    // backup-and-recovery follow-up: startup backup pruning -- after
    // LoadInitFile for the same reason as LoadFilePlaces above, so a
    // ned/set-backup-max-* retention knob configured there governs it.
    try {
        ned::editor::PruneBackups();
    }
    catch (const std::exception&) {
        // Unprunable backups must never block startup.
    }

    // variables-store follow-up: editor-remembered key/value facts
    // ($XDG_STATE_HOME/ned/variables.json) -- the "theme" variable
    // participates in theme selection below, so this must load before it.
    ned::editor::LoadVariables();
    if (ned::editor::SavePlaceEnabled()) {
        for (const auto& openBuffer : bufferList.Buffers()) {
            ned::editor::RestoreFilePlace(*openBuffer, static_cast<std::size_t>(ned::editor::TabWidth()));
        }
    }
    bufferList.SetOnFileOpened([](ned::text::Buffer& opened) -> void {
        ned::editor::RestoreFilePlace(opened, static_cast<std::size_t>(ned::editor::TabWidth()));
    });

    // session-persistence slice 2, Kate-style per the user's explicit call:
    // the project session's buffers restore even when a specific file was
    // named on the command line -- the named file just wins focus, the
    // session's own buffers fill in behind it (FindByPath dedupes any
    // overlap). Runs after init.janet (ned/set-session-restore honored --
    // LoadActiveProjectSession no-ops when it's off, or when no project
    // root was established above) and after SetOnFileOpened, so every
    // restored buffer gets its save-place restore through the same hook as
    // any other open. restoredSession outlives this block: breakpoints are
    // applied later, once dapManager exists, and the sidebar state once
    // projectSidebar does.
    const std::optional<ned::editor::ProjectSessionData> restoredSession = ned::editor::LoadActiveProjectSession();
    if (restoredSession) {
        for (const auto& file : restoredSession->openFiles) {
            std::error_code existsEc;
            if (!std::filesystem::exists(file, existsEc) || bufferList.FindByPath(file) != nullptr) {
                continue; // gone since last session, or already opened via the CLI
            }
            try {
                bufferList.OpenOrCreateFile(file, forceBinary);
            }
            catch (const std::exception&) {
                // Best-effort, same as the extra-CLI-paths loop above.
            }
        }
        if (buffer == nullptr && restoredSession->activeFile) {
            buffer = bufferList.FindByPath(*restoredSession->activeFile);
        }
        if (buffer == nullptr) {
            for (const auto& file : restoredSession->openFiles) {
                if ((buffer = bufferList.FindByPath(file)) != nullptr) {
                    break;
                }
            }
        }
    }
    // The scratch fallback moved here from right after the CLI open (see
    // the comment there) -- only a launch with no CLI file AND no restored
    // session buffer still needs one. When it exists only as a stand-in for
    // a deferred large-file open (loose-ends follow-up), it's remembered so
    // the deferred block below can retire it once the real buffer arrives,
    // instead of leaving a stray empty scratch tab.
    ned::text::Buffer* startupScratch = nullptr;
    if (buffer == nullptr) {
        buffer = &bufferList.CreateBuffer("scratch");
        if (!deferredLargeOpenPath.empty()) {
            startupScratch = buffer;
        }
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
    // Theme precedence (rich-theme-set follow-up, Phase 1; variables-store
    // follow-up): the remembered "theme" variable (whatever the select-theme
    // picker last committed) wins the *base* selection -- the newer
    // expression of intent than init.janet's static (ned/set-theme ...) --
    // then that explicit set-theme name, then a previously
    // `ned --detect-theme`-generated file if one exists (never probes the
    // terminal on a normal launch -- see UI/TerminalColorProbe.h), else the
    // fixed DarkTheme() default. An unresolvable name at any step falls
    // through to the next source rather than aborting, reported via the
    // status line the same way a failed startup file open already is. And
    // regardless of which base wins, the (ned/theme-set ...) overrides
    // below apply last -- the user's explicit call: "the theme overrides
    // should win out in the end," so a dofile'd theme.janet always
    // determines the final look.
    // Not const: the ansi-fallback-theme check below (which can't run until
    // EventLoop exists) may swap the whole value in place, and the
    // select-theme picker's applier (wired below) reassigns it live.
    ned::ui::Theme theme = [&statusMessage] {
        if (const auto remembered = ned::editor::Variable("theme")) {
            if (auto named = ned::ui::ThemeByName(*remembered)) {
                return *std::move(named);
            }
            statusMessage = "Unknown remembered theme \"" + *remembered + "\" (variables.json)";
        }
        const std::string preferred = ned::editor::PreferredThemeName();
        if (!preferred.empty()) {
            if (auto named = ned::ui::ThemeByName(preferred)) {
                return *std::move(named);
            }
            statusMessage = "Unknown theme \"" + preferred + "\" (ned/set-theme)";
        }
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

    // theme-editing follow-up: init.janet's accumulated (ned/theme-set ...)
    // overrides, applied on top of whichever base won above -- typically a
    // whole (dofile ".../theme.janet") worth from save-theme's output, which
    // sets every field and makes the base moot; a handful of targeted
    // tweaks over a named theme works the same way. Insertion order, so a
    // later call for the same key wins. Unknown keys/tokens are counted and
    // reported once rather than silently dropped -- unlike a theme *file*'s
    // forward-compatibility case, these were typed by the user against this
    // exact build, so a typo'd key is worth a message.
    {
        int rejected = 0;
        for (const auto& [key, token] : ned::editor::ThemeColorOverrides()) {
            if (!ned::ui::SetThemeColorByKey(theme, key, token)) {
                ++rejected;
            }
        }
        if (rejected > 0) {
            statusMessage = std::to_string(rejected) + " unrecognized ned/theme-set key(s)/color(s) ignored";
        }
    }

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

    // Chrome-redesign follow-up: the old SidebarToggle («/») column is
    // gone -- hiding the sidebar is a collapse to a 1-column border strip
    // now (ProjectSidebar stays active and always paints *something*
    // clickable; see its own header comment), so the "toggle must live
    // outside the widget or it vanishes with it" problem no longer exists.
    auto projectSidebar = std::make_shared<ned::ui::ProjectSidebar>(
        [wm = windowManager.get()]() -> ned::ui::ActiveBuffer& { return wm->FocusedActiveBuffer(); }, bufferList,
        statusMessage, theme);

    auto echoArea = std::make_shared<ned::ui::EchoArea>(statusMessage, theme);

    windowManager->SetProjectSidebar(projectSidebar.get());

    projectSidebar->SetOnBufferClosed(
        [wm = windowManager.get()](ned::text::Buffer& buffer) { wm->NotifyBufferClosing(buffer); });

    projectSidebar->SetOnBinaryFileOpenRequest(
        [wm = windowManager.get()](const std::filesystem::path& path) { wm->RequestOpenBinaryFile(path); });

    // sidebar-keyboard-focus follow-up: Escape/C-g (or Enter opening a
    // file) hands the keyboard back to the focused pane's BufferView --
    // WindowManager::TakeFocus already handles the "no pane currently
    // reports Focused()" state this necessarily runs in (the sidebar holds
    // focus at that moment) via its first-leaf fallback.
    projectSidebar->SetOnFocusReturn([wm = windowManager.get()] { wm->TakeFocus(); });

    // session-persistence slice 2: the restored session's sidebar state.
    // Applied before the first frame ever paints, so there's no visible
    // flash of the default state.
    if (restoredSession) {
        if (restoredSession->sidebarVisible) {
            // Chrome-redesign follow-up: hidden means collapsed-to-a-strip
            // now, not Widget::active (see ProjectSidebar.h) -- the stored
            // bool's meaning is unchanged, so old session files restore
            // correctly.
            projectSidebar->SetCollapsed(!*restoredSession->sidebarVisible);
        }
        if (restoredSession->sidebarWidth) {
            projectSidebar->SetWidth(*restoredSession->sidebarWidth);
        }
    }

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

    // Tab-reorder follow-up: dragging a tab reorders the BufferList itself
    // -- Buffers() order is also what SaveProjectSessionNow persists, so a
    // dragged-into-place order survives a restart with no extra state.
    tabBar->SetOnReorder([&bufferList](ned::text::Buffer& buffer, std::size_t targetIndex) {
        bufferList.MoveBufferToIndex(buffer, targetIndex);
    });

    // Chrome-focus follow-up: the tab underline is the editor region's
    // frame -- lit in the accent while an editor pane holds the keyboard,
    // the counterpart of ProjectSidebar's own focused accent frame.
    tabBar->SetFocusProvider([wm = windowManager.get()] { return wm->HasFocusedPane(); });

    // ProjectSidebar's own width is drag-resizable at runtime (divider drag
    // -- see ProjectSidebar::UpdateResize), so it can't be a fixed value
    // computed once at composition time the way every other widget's is --
    // SizeSpec::DynamicFixed (Layout.h) is read fresh every single
    // Container::Paint() call (the direct replacement for FTXUI's own
    // per-frame ElementDecorator lambda, confirmed during the original
    // TermOx -> FTXUI migration to be re-invoked every Render() call), so
    // it always reflects whatever ProjectSidebar::Width() currently is --
    // including the 1-column strip Width() reports while collapsed
    // (chrome-redesign follow-up: hiding the sidebar is a collapse now,
    // never an active-flag flip, so its double-click-to-expand border strip
    // always stays laid out and clickable).

    // sidebar-header follow-up: tabBar now sits only above the pane area,
    // not above ProjectSidebar too -- ProjectSidebar spans the row that
    // used to belong to tabBar instead, using it for its own header (see
    // ProjectSidebar::Paint's own comment on that row).
    // Back to Fixed(1) (tab-restyle follow-up): the chrome redesign's
    // second underline row read as too tall in daily use -- distinct tab
    // blocks and the active tab's own focus accent do that row's jobs now;
    // see TabBar.h.
    Container mainColumn(Axis::Vertical, {
                                             {tabBar.get(), SizeSpec::Fixed(1)},
                                             {&windowManager->RootComponent(), SizeSpec::Flex()},
                                         });

    Container bufferRow(Axis::Horizontal, {
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

    // session-persistence slice 3: the untrusted-.ned/init.janet prompt
    // deferred from the load site above, now that a focused pane exists to
    // drive it (same deferral RequestOpenBinaryFile gets, and the same
    // "must run after TakeFocus()" reason). Loading this late instead of
    // beside the global init.janet is the accepted cost of prompting at
    // all: mode overrides a first-time project init registers won't affect
    // the already-selected initial Mode until the next buffer switch. The
    // hash is recomputed at decision time so what gets recorded as trusted
    // is exactly what got loaded, not what was on disk at startup.
    if (!deferredTrustPromptPath.empty()) {
        windowManager->RequestTrustProjectInit(
            deferredTrustPromptPath,
            [&janetEnv, &statusMessage](const std::filesystem::path&     initPath,
                                        ned::editor::ProjectInitDecision decision) {
                if (decision == ned::editor::ProjectInitDecision::Decline) {
                    statusMessage = "Project init.janet not loaded.";
                    return;
                }
                if (decision == ned::editor::ProjectInitDecision::LoadAlways) {
                    if (const auto hash = ned::editor::HashFileContent(initPath)) {
                        ned::editor::RecordProjectInitTrust(initPath, *hash);
                    }
                }
                try {
                    janetEnv.DoFile(initPath);
                    ned::editor::TouchProjectTrust(initPath);
                    statusMessage = "Loaded " + initPath.string();
                }
                catch (const std::exception& e) {
                    statusMessage = std::string("project init.janet error: ") + e.what();
                }
            });
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

    // ansi-fallback-theme follow-up: with neither truecolor nor a 256-color
    // palette (e.g. the Linux framebuffer console, TERM=linux: 8 colors),
    // every TrueColor field in the theme above gets quantized down to those
    // 8 and washes out -- or lands black-on-black outright -- so swap in
    // the curated Palette16-only fallback instead (Theme.h). Assigning the
    // local in place, after every widget is already constructed, is
    // deliberate and sufficient: they all hold `const Theme&` (or a
    // `const Brush&` into it) bound to this same object and repaint fresh
    // every frame. It couldn't happen any earlier -- the capability queries
    // need the live notcurses context EventLoop's constructor just created.
    // This also intentionally overrides a --detect-theme file, which is
    // just as TrueColor as the built-ins and washes out the same way.
    const bool limitedTerminal = !eventLoop.CanTrueColor() && eventLoop.PaletteSize() < 256;
    if (limitedTerminal) {
        theme = ned::ui::AnsiFallbackFor(theme);
    }

    // rich-theme-set follow-up (Phase 1): the select-theme picker's applier
    // -- the same in-place assignment the ANSI fallback just above
    // established as safe (every widget holds `const Theme&`, or a
    // `const Brush&` into it, bound to this same local and repaints fresh
    // every frame). Routing the limited-terminal gate through here too
    // means a live-picked TrueColor theme still degrades to its ANSI
    // counterpart on a terminal that can't show it, exactly like the
    // startup path. `theme` outlives eventLoop.Run() below as a plain
    // local, so the reference captures are safe for every event this
    // applier could ever run from.
    windowManager->SetThemeApplier([&theme, limitedTerminal](const ned::ui::Theme& next) {
        theme = limitedTerminal ? ned::ui::AnsiFallbackFor(next) : next;
    });

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

    // task-runner follow-up: same "constructed here, needs a real EventLoop&"
    // shape as lspManager just above, and the same "wired into windowManager,
    // connect after construction" convention.
    ned::editor::tasks::TaskRunner taskRunner(bufferList, eventLoop);
    windowManager->SetTaskRunner(&taskRunner);

    // VCS blame gutter follow-up: same "constructed here, needs a real
    // EventLoop&" shape as taskRunner just above, and the same "wired into
    // windowManager, connect after construction" convention.
    ned::editor::vcs::VcsRunner vcsRunner(eventLoop);
    windowManager->SetVcsRunner(&vcsRunner);

    // DAP client slice 1: same shape as vcsRunner just above.
    // SetDapManager also wires the session's async callbacks (breakpoint
    // hits jumping the focused pane, session-end status text) -- see its
    // own doc comment in WindowManager.h.
    ned::editor::dap::DapManager dapManager(eventLoop);
    windowManager->SetDapManager(&dapManager);

    // session-persistence slice 2: the restored session's breakpoints,
    // applied as soon as the store they live in exists -- long before any
    // debug session could, so this never races an adapter.
    if (restoredSession && !restoredSession->breakpoints.empty()) {
        dapManager.RestoreBreakpoints(restoredSession->breakpoints);
    }

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

    // loose-ends follow-up: the large CLI file deferred at the top of
    // main() (see deferredLargeOpenPath's own comment there) -- now that
    // the async opener hook is wired, this open returns immediately with an
    // IsLoading() placeholder the background loader fills in, exactly like
    // any interactive open of the same file. Deliberately after the
    // session-restore/breakpoint blocks: the CLI-named file wins focus,
    // same as the sync path. A stand-in scratch buffer created above just
    // for this launch is retired once the real buffer is showing.
    if (!deferredLargeOpenPath.empty()) {
        try {
            ned::text::Buffer& opened = bufferList.OpenOrCreateFile(deferredLargeOpenPath, forceBinary);
            windowManager->FocusedActiveBuffer().Set(opened);
            projectSidebar->RevealPath(deferredLargeOpenPath);
            if (startupScratch != nullptr && !startupScratch->Modified()) {
                windowManager->NotifyBufferClosing(*startupScratch);
                bufferList.Close(startupScratch->Name());
            }
        }
        catch (const std::exception& e) {
            statusMessage = e.what();
        }
    }

    // EventLoop's constructor too, via notcurses_mice_enable(NCMICE_ALL_EVENTS)
    // -- there's nothing left to set explicitly here for either concern.
    // A single Screen (Widget.h) reused across every frame, resized to
    // match the terminal on every onResize callback -- this composition
    // root's own direct replacement for what used to be an implicit
    // ftxui::Screen FTXUI itself owned and rebuilt every Render() call.
    Screen screenBuffer(0, 0);

    // background-activity-spinner follow-up: while any BackgroundActivity is
    // live, the mode line's spinner needs frames the event loop otherwise
    // has no reason to produce (repaints are earned by input/posted work,
    // not a clock). Re-armed from the render callback below on every frame
    // that still shows activity -- the fired callback's body is empty on
    // purpose, because merely draining a Post()ed task earns the next loop
    // iteration a repaint (see EventLoop.cpp's needsRepaint comment), which
    // re-runs render, which re-arms. Self-stopping: an idle frame doesn't
    // re-arm, so the chain ends one no-op repaint after the last activity.
    DeadlineTimer activityAnimationTimer;

    // Terminal-panel follow-up: the floating-widget layer (see Overlay.h's
    // own header comment for the three hooks below and why keyboard needs
    // none). Inert until something is Show()n.
    OverlayHost overlays;

    // The built-in terminal drawer: full width, bottom
    // TerminalHeightPercent() of the screen, floating just above the echo
    // area row -- the placement function re-reads the Janet-configurable
    // percentage on every Reflow/Show, the same pull-fresh convention
    // ProjectSidebar's width already follows. Declared after eventLoop so
    // its PtyProcess (background read thread + shell) is torn down first on
    // the way out of main, the same owner-destroys-after-Run ordering every
    // TaskProcess/LspClient owner relies on.
    auto terminalPanel = std::make_shared<ned::ui::TerminalPanel>(theme);
    terminalPanel->SetEventLoop(&eventLoop);
    overlays.Add(*terminalPanel, [panel = terminalPanel.get()](Size size) {
        // Maximized ([▲] button) covers the whole buffer area below the tab
        // bar; otherwise the configured percentage of the screen.
        const int yMax = std::max(1, size.height - 2); // above the echo area row
        const int height =
            panel->Maximized() ? yMax : std::max(4, size.height * ned::editor::terminal::TerminalHeightPercent() / 100);
        return Box{.x_min = 0, .x_max = size.width - 1, .y_min = std::max(1, yMax - height + 1), .y_max = yMax};
    });
    // The maximize toggle changes what the placement above computes; Show on
    // an already-visible overlay is exactly a re-box from the current size.
    terminalPanel->SetOnLayoutChange([&overlays, panel = terminalPanel.get()] { overlays.Show(*panel); });
    overlays.SetFocusReturn(*terminalPanel, [wm = windowManager.get()] { wm->TakeFocus(); });
    // The toggle: hidden -> show+focus; visible -> hide (the focus-return
    // above hands the keyboard back only if the panel actually held it).
    // Deliberately NOT VS Code's three-state (visible-but-unfocused ->
    // focus): C-` is only deliverable under the kitty keyboard protocol, so
    // on a legacy-encoding terminal "C-c t, click into the buffer, C-c t"
    // must be a complete keyboard show/hide cycle -- confirmed stuck-drawer
    // feedback from real use, not a guess. Refocusing a visible panel is a
    // click on it (or C-c t twice). Reached from the editor via
    // toggle-terminal (C-` / C-c t), from the focused panel via its one
    // reserved chord (C-`), and from the title row's close button (both in
    // TerminalPanel.h).
    auto toggleTerminal = [&overlays, panel = terminalPanel.get()] {
        if (!overlays.IsVisible(*panel)) {
            overlays.Show(*panel);
            panel->EnsureStarted();
            panel->TakeFocus();
        }
        else {
            overlays.Hide(*panel);
        }
    };
    windowManager->SetOnTerminalToggle(toggleTerminal);
    terminalPanel->SetOnToggleRequest(toggleTerminal);

    EventLoopCallbacks callbacks;

    callbacks.onResize = [&](Size size) {
        screenBuffer = Screen(size.width, size.height);

        head.SetBox_(Box{.x_min = 0, .x_max = size.width - 1, .y_min = 0, .y_max = size.height - 1});
        overlays.Reflow(size);
    };

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
            // A visible overlay owns clicks inside its own Box; everything
            // else keeps the broadcast dispatch (Container::OnEvent
            // forwards to every active leaf) several widgets actively
            // depend on -- see Overlay.h's header comment.
            if (!overlays.OnMouseEvent(event)) {
                head.OnEvent(event);
            }
        }
        else if (Widget* focused = FocusedWidget()) {
            focused->OnEvent(event);
        }
    };

    callbacks.render = [&]() -> std::optional<Point> {
        head.Paint(Canvas(screenBuffer, head.Box_()));
        overlays.Paint(screenBuffer);
        screenBuffer.Flush(eventLoop.StdPlane());

        if (!ned::editor::ActiveBackgroundActivities().empty()) {
            activityAnimationTimer.Arm(eventLoop, ned::editor::kBackgroundActivitySpinnerInterval, [] {});
        }

        if (const Widget* focused = FocusedWidget()) {
            if (const std::optional<Point> local = focused->CursorPosition()) {
                const Box& box = focused->Box_();

                return Point{box.x_min + local->x, box.y_min + local->y};
            }
        }

        return std::nullopt;
    };

    eventLoop.Run(callbacks);

    // NED_DEBUG_SHUTDOWN (terminal-panel follow-up, mirroring
    // NED_DEBUG_MOUSE's env-var-to-file pattern): if set to a file path,
    // appends one line per post-Run stage -- the screen still shows the
    // frozen "Shutting down..." frame through all of this (the terminal
    // isn't restored until ~EventLoop's notcurses_stop), so a hang here is
    // otherwise undiagnosable from the outside. The last line in the file
    // names the stage that stuck. Next to free when unset.
    const char* shutdownLogPath = std::getenv("NED_DEBUG_SHUTDOWN");
    const auto  logShutdown     = [shutdownLogPath](const char* stage) {
        if (shutdownLogPath != nullptr) {
            std::ofstream(shutdownLogPath, std::ios::app) << stage << std::endl;
        }
    };

    // session-persistence slice 1: one final record+save on clean exit --
    // forced, so lastUsed refreshes (which deliberately don't mark the
    // store dirty, see FilePlaceStore::Record) still reach disk. Everything
    // recorded from is still alive here: bufferList/windowManager are
    // locals destroyed after this returns.
    logShutdown("post-run: recording session places");
    windowManager->RecordSessionPlaces();
    logShutdown("post-run: saving file places");
    ned::editor::SaveFilePlaces(/*force=*/true);
    logShutdown("post-run: saving project session");
    windowManager->SaveProjectSessionNow();
    logShutdown("post-run: explicit steps done; entering local destruction "
                "(terminal pty, DAP, VCS, task runner, LSP clients, window tree, Janet, EventLoop/terminal restore)");

    return 0;
}
