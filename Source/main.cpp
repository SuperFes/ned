#include <clocale>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>

#include "Application.h"

#include "Editor/Acp/AcpManager.h"
#include "Editor/Acp/AcpPanelConfig.h"
#include "Editor/BackgroundActivity.h"
#include "Editor/Backup.h"
#include "Editor/Bookmark.h"
#include "Editor/Commands.h"
#include "Editor/Dap/DapManager.h"
#include "Editor/Keymap.h"
#include "Editor/Lsp/BrokerSocketPath.h"
#include "Editor/Lsp/LspBrokerMain.h"
#include "Editor/Lsp/LspManager.h"
#include "Editor/Lsp/Transport.h"
#include "Editor/MinimapSettings.h"
#include "Editor/Mode.h"
#include "Editor/ModeOverrides.h"
#include "Editor/ModePrewarm.h"
#include "Editor/PersistentUndo.h"
#include "Editor/ProjectPlugins.h"
#include "Editor/ProjectRoot.h"
#include "Editor/ProjectSession.h"
#include "Editor/ProjectTrust.h"
#include "Editor/ProjectUndo.h"
#include "Editor/PromptHistory.h"
#include "Editor/RecentFiles.h"
#include "Editor/Register.h"
#include "Editor/ScriptingSession.h"
#include "Editor/Session.h"
#include "Editor/TabWidth.h"
#include "Editor/Tasks/TaskRunner.h"
#include "Editor/Terminal/Config.h"
#include "Editor/TestRun/TestResultsBuffer.h"
#include "Editor/TestRun/TestRunner.h"
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

#include "UI/AcpPanel.h"
#include "UI/ActiveBuffer.h"
#include "UI/BufferListPanel.h"
#include "UI/DebugConsolePanel.h"
#include "UI/DesktopThemeProbe.h"
#include "UI/EchoArea.h"
#include "UI/EventLoop.h"
#include "UI/Layout.h"
#include "UI/ListPopup.h"
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
// any ned::ui::EventLoop is constructed, since EventLoop's constructor is
// what calls notcurses_core_init, which is what starts reading stdin.
//
// startup-mode-unification follow-up: transparent/outputPath are already
// parsed by main()'s own single CLI::App (see main() below) -- this used to
// construct and re-parse argv through a second, private CLI::App (shifting
// past the "--detect-theme" argument main() had already consumed to decide
// to call this function at all), which is what kept this mode's own
// --transparent/output-path options invisible to `ned --help`.
int RunDetectTheme(bool transparent, const std::optional<std::string>& outputPath) {
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

// `ned --lsp-broker-stop`: connects to the running LSP broker daemon (see
// Editor/Lsp/LspBrokerMain.h) and sends it the ned/broker-shutdown control
// message -- every real language-server subprocess gets a genuine LSP
// shutdown/exit before the daemon exits (Editor/Lsp/LspBroker.h's own
// Shutdown()), not a bare kill. Same early-return placement as
// RunDetectTheme/the --lsp-broker dispatch below -- no EventLoop/Notcurses
// needed for a one-shot control message. Deliberately idempotent: no
// broker currently running is reported and treated as success (0), not an
// error, so this is safe to call from a shell script or a systemd unit's
// ExecStop without checking first.
int RunLspBrokerStop() {
    const std::string socketPathStr = ned::editor::lsp::BrokerSocketPath().string();

    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        std::cerr << "ned: lsp-broker-stop: socket() failed: " << std::strerror(errno) << '\n';
        return 1;
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (socketPathStr.size() >= sizeof(addr.sun_path)) {
        std::cerr << "ned: lsp-broker-stop: socket path too long: " << socketPathStr << '\n';
        ::close(fd);
        return 1;
    }
    std::strncpy(addr.sun_path, socketPathStr.c_str(), sizeof(addr.sun_path) - 1);

    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::cout << "ned: no LSP broker is currently running.\n";
        ::close(fd);
        return 0;
    }

    try {
        const int                   dupFd = ::dup(fd);
        ned::editor::lsp::Transport transport(fd, dupFd, -1);
        transport.WriteFrame(nlohmann::json{{"jsonrpc", "2.0"}, {"method", "ned/broker-shutdown"}}.dump());
        std::cout << "ned: sent shutdown to the LSP broker.\n";
    }
    catch (const std::exception& e) {
        std::cerr << "ned: lsp-broker-stop: " << e.what() << '\n';
        return 1;
    }
    return 0;
}

// acp-panel-minimap-overlap follow-up: how many columns a full-width,
// bottom-docked (or right-docked) OverlayHost overlay must stay clear of on
// the right edge -- confirmed live that an active Minimap owns a real,
// persistent Notcurses ncplane for its pixel graphics (NCBLIT_PIXEL, see
// Minimap.h's own header comment), composited by the terminal at a z-order
// *above* the ordinary Screen/Cell buffer every overlay paints into via
// Canvas. No amount of correctly painting "on top" in ned's own Cell-based
// Paint() can occlude that plane, since it isn't part of this Screen at
// all -- the only fix is to stop short of it. A plain Cell-based ScrollBar
// has no such plane and needs no reservation, hence the MinimapEnabled()
// gate. Shared by terminalPanel/acpPanel/dapConsolePanel's placement
// functions below; doesn't account for a minimap belonging to some *other*
// pane in a multi-way split (every one of these panels spans the whole
// width, not one pane) -- a real gap, just a narrower one than before this
// existed.
int MinimapOverlayReserve() {
    return ned::editor::MinimapEnabled() ? ned::editor::MinimapWidth() : 0;
}

} // namespace

auto main(int argc, char** argv) -> int {
    // startup-mode-unification follow-up: one CLI::App now owns every
    // top-level flag ned recognizes, --detect-theme/--lsp-broker/
    // --lsp-broker-stop included -- previously each was a hand-rolled
    // `argv[1] == "..."` check run *before* any real parsing, so `ned
    // --help` never documented them (and --detect-theme's own
    // --transparent/output-path options, parsed by a second private
    // CLI::App re-fed a shifted argv, weren't even reachable from here).
    // They stay plain flags on this one App rather than becoming CLI11
    // subcommands specifically to keep the exact existing invocations
    // (`ned --lsp-broker`, notably self-exec'd by
    // Lsp/LspBrokerConnect.cpp, and documented as a systemd ExecStart
    // line) working unchanged -- a subcommand would mean `ned lsp-broker`
    // instead, a real breaking syntax change for no behavioral gain here.
    // ->excludes() catches the nonsensical case of passing more than one
    // of the three as a real CLI11 error instead of silently letting
    // whichever this code happened to check first win.
    CLI::App app{"Ned -- a terminal-based, Janet-scriptable text editor.", "ned"};

    bool detectTheme  = false;
    bool transparent  = false;
    bool lspBroker    = false;
    bool lspBrokerStop = false;
    bool forceBinary  = false;
    bool noRestore    = false;
    std::vector<std::string> paths;

    CLI::Option* detectThemeOpt = app.add_flag(
        "--detect-theme", detectTheme,
        "Probe the terminal's actual configured colors, write a Theme file, and exit")
        ->group("Startup modes");
    app.add_flag("--transparent", transparent,
                 "With --detect-theme: treat the detected background as transparent instead of an opaque color")
        ->needs(detectThemeOpt)
        ->group("Startup modes");
    CLI::Option* lspBrokerOpt =
        app.add_flag("--lsp-broker", lspBroker, "Run the headless LSP broker daemon and exit")
            ->excludes(detectThemeOpt)
            ->group("Startup modes");
    app.add_flag("--lsp-broker-stop", lspBrokerStop, "Stop a running LSP broker daemon and exit")
        ->excludes(detectThemeOpt)
        ->excludes(lspBrokerOpt)
        ->group("Startup modes");
    app.add_flag("--force-binary", forceBinary,
                 "Open files that look binary anyway, without an interactive confirmation");
    app.add_flag("--no-restore", noRestore,
                 "Don't restore the project's saved session (open buffers, breakpoints, sidebar state)");
    app.add_option("paths", paths,
                   "Files or directories to open (or, with --detect-theme, an optional single output path)");

    try {
        app.parse(argc, argv);
    }
    catch (const CLI::ParseError& e) {
        return app.exit(e);
    }

    if (detectTheme) {
        return RunDetectTheme(transparent, paths.empty() ? std::nullopt : std::optional(paths.front()));
    }

    // `ned --lsp-broker`: runs the headless LSP broker daemon itself (see
    // Editor/Lsp/LspBrokerMain.h) instead of the interactive editor --
    // dispatched here, strictly before EventLoop/Notcurses construct, same
    // reasoning as --detect-theme above. This is what a `ned` process
    // auto-forks-and-execve's into when no daemon is already reachable, and
    // is equally the right ExecStart line for a `systemd --user` unit that
    // starts it explicitly at login instead.
    if (lspBroker) {
        return ned::editor::lsp::RunLspBrokerDaemon();
    }

    if (lspBrokerStop) {
        return RunLspBrokerStop();
    }

    std::setlocale(LC_ALL, "");

    Ned::Application::SetTitle("Ned");

    ned::text::BufferList bufferList;
    std::string           statusMessage;

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
        //
        // huge-file-cli-threshold-gap follow-up: also checked against
        // HugeFileThreshold(), not just AsyncLoadThreshold() -- both hooks
        // (SetAsyncFileOpener/SetAsyncHugeFileOpener) are wired at the same
        // later point, and BufferList::OpenFile itself checks
        // HugeFileThreshold() first, so a file that's "huge" by a
        // lowered-below-AsyncLoadThreshold() HugeFileThreshold() config
        // still needs deferring here, or it hits OpenOrCreateFile below with
        // neither hook wired yet and opens fully synchronously regardless.
        std::error_code      sizeEc;
        const std::uintmax_t size = std::filesystem::file_size(pathArg, sizeEc);
        if (!sizeEc && (size > ned::text::AsyncLoadThreshold() || size > ned::text::HugeFileThreshold()) &&
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
    ned::editor::PromptHistory promptHistory;

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

    // session-persistence slice 3 (+ project-plugin-autoload follow-up):
    // project-local .ned/plugins/*.janet, then .ned/init.janet, loaded after
    // the global init.janet so project config overrides user config -- but
    // NEVER silently: each is arbitrary code execution triggered by opening
    // a directory (the same concern class ROADMAP.md records against Org
    // Babel). A file whose exact content was previously "always"-approved
    // (and whose trust hasn't aged out unused -- see ProjectTrust.h) loads
    // right here, early enough for its mode overrides/grammars to affect the
    // initial buffer; anything else defers to its own y/n/a prompt once the
    // UI exists, below -- queued so a project with several new/changed files
    // gets asked about all of them, not just the first. Plugins load first,
    // mirroring LoadBundledPlugins-before-LoadInitFile: project init.janet
    // can then override a project plugin's own registration, same as a
    // user's init.janet can override a bundled plugin's. The trust store
    // loads after init.janet so a configured expiry window governs its
    // prune.
    const std::filesystem::path projectInitPath = projectRoot / ".ned" / "init.janet";

    std::vector<std::filesystem::path> projectTrustCandidates = ned::editor::ProjectPluginFiles(projectRoot);
    {
        std::error_code projectInitEc;
        if (std::filesystem::is_regular_file(projectInitPath, projectInitEc)) {
            projectTrustCandidates.push_back(projectInitPath);
        }
    }

    std::deque<std::filesystem::path> deferredTrustPrompts;
    if (!projectTrustCandidates.empty()) {
        ned::editor::LoadProjectTrust();
        for (const std::filesystem::path& candidate : projectTrustCandidates) {
            const std::optional<std::string> hash = ned::editor::HashFileContent(candidate);
            if (hash && ned::editor::IsProjectInitTrusted(candidate, *hash)) {
                try {
                    janetEnv.DoFile(candidate);
                }
                catch (const std::exception& e) {
                    statusMessage = candidate.string() + " error: " + e.what();
                }
                ned::editor::TouchProjectTrust(candidate);
            }
            else {
                deferredTrustPrompts.push_back(candidate);
            }
        }
        ned::editor::SaveProjectTrust();
    }

    // session-persistence slice 1: deliberately after LoadInitFile, not
    // beside the CLI opens above -- init.janet is where ned/set-save-place
    // can turn this off, so the startup buffers' restore has to wait for it
    // (they were opened before the hook below existed, hence the explicit
    // loop). Every later open (find-file, sidebar click, LSP jump, ...)
    // funnels through BufferList's own on-file-opened seam instead.
    ned::editor::LoadFilePlaces();

    // editor-ergonomics follow-up: recent-files/bookmarks stores, loaded
    // the same "after LoadInitFile" way as LoadFilePlaces just above so a
    // ned/set-recentf-enabled call there is honored from the first record.
    ned::editor::LoadRecentFiles();
    ned::editor::LoadBookmarks();

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
    // Same precedence as the "theme" variable above: the last live C-c m
    // toggle is a newer expression of intent than a static
    // ned/set-minimap-enabled default, so it wins if present.
    if (const auto remembered = ned::editor::Variable("minimap-enabled")) {
        ned::editor::SetMinimapEnabled(*remembered == "true");
    }
    if (ned::editor::SavePlaceEnabled()) {
        for (const auto& openBuffer : bufferList.Buffers()) {
            ned::editor::RestoreFilePlace(*openBuffer, static_cast<std::size_t>(ned::editor::TabWidth()));
        }
    }
    // persistent-undo follow-up: same "buffers opened before the hook
    // existed need their own explicit loop" reasoning as the save-place
    // restore just above -- TryRestoreUndoHistory does its own
    // PersistentUndoEnabled() check internally, so no outer guard here.
    for (const auto& openBuffer : bufferList.Buffers()) {
        ned::editor::TryRestoreUndoHistory(*openBuffer);
    }
    bufferList.SetOnFileOpened([](ned::text::Buffer& opened) -> void {
        ned::editor::RestoreFilePlace(opened, static_cast<std::size_t>(ned::editor::TabWidth()));
        ned::editor::TryRestoreUndoHistory(opened);
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
    // huge-file-session-restore follow-up: a restored file over
    // AsyncLoadThreshold() can't go through OpenOrCreateFile here -- the
    // async/huge opener hooks (SetAsyncFileOpener/SetAsyncHugeFileOpener)
    // aren't wired until EnableAsyncFileLoading/EnableAsyncHugeFileLoading
    // run, ~500 lines below, once EventLoop exists -- same ordering
    // constraint deferredLargeOpenPath exists to work around for the CLI
    // arg. Deferred here the same way: opened for real right after those
    // hooks are wired, so a large/huge file inside a restored session
    // streams in instead of blocking the splash. huge-file-cli-threshold-gap
    // follow-up: also checked against HugeFileThreshold() -- see
    // deferredLargeOpenPath's own comment above for why AsyncLoadThreshold()
    // alone isn't enough once HugeFileThreshold() can be configured lower.
    std::vector<std::filesystem::path> deferredSessionOpenPaths;
    if (restoredSession) {
        for (const auto& file : restoredSession->openFiles) {
            std::error_code existsEc;
            if (!std::filesystem::exists(file, existsEc) || bufferList.FindByPath(file) != nullptr) {
                continue; // gone since last session, or already opened via the CLI
            }
            std::error_code      sizeEc;
            const std::uintmax_t size = std::filesystem::file_size(file, sizeEc);
            if (!sizeEc && (size > ned::text::AsyncLoadThreshold() || size > ned::text::HugeFileThreshold())) {
                deferredSessionOpenPaths.push_back(file);
                continue;
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
        if (!deferredLargeOpenPath.empty() || !deferredSessionOpenPaths.empty()) {
            startupScratch = buffer;
        }
    }

    ned::editor::Mode mode = ned::editor::CachedModeForBuffer(*buffer);

    // per-buffer-mode follow-up: this is just the *initial* Mode for
    // whichever pane WindowManager constructs first -- each Pane resyncs its
    // own Mode fresh (via ned::editor::CachedModeForBuffer, the same
    // function used here) whenever its active buffer actually changes (find-file,
    // switch-to-buffer, a tab/sidebar click, visiting a search result,
    // etc.), see BufferView::SetOnActiveBufferChanged and Pane's own wiring
    // of it in WindowManager.cpp. Mode is a property of the buffer being
    // viewed, not the pane; a pane's Mode is only ever "whatever its current
    // buffer resolves to."
    //
    // Theme precedence (rich-theme-set follow-up, Phase 1; variables-store
    // follow-up; theme-polish follow-up, Phase 4): the remembered "theme"
    // variable (whatever the select-theme picker last committed) wins the
    // *base* selection -- the newer expression of intent than init.janet's
    // static (ned/set-theme ...) -- then that explicit set-theme name, then
    // a previously `ned --detect-theme`-generated file if one exists (never
    // probes the terminal on a normal launch -- see
    // UI/TerminalColorProbe.h), then a live desktop-environment probe (see
    // UI/DesktopThemeProbe.h -- GNOME/KDE light-vs-dark preference plus
    // accent color, via the freedesktop portal or a DE-specific fallback;
    // unlike the terminal probe this is cheap and side-effect-free, so it
    // runs unconditionally rather than needing an explicit --detect-theme
    // invocation), else the fixed DarkTheme() default. An unresolvable name
    // at any step falls through to the next source rather than aborting,
    // reported via the status line the same way a failed startup file open
    // already is. And regardless of which base wins, the (ned/theme-set
    // ...) overrides below apply last -- the user's explicit call: "the
    // theme overrides should win out in the end," so a dofile'd theme.janet
    // always determines the final look.
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
        if (const auto desktop = ned::ui::ProbeDesktopTheme()) {
            return ned::ui::BuildDesktopTheme(*desktop);
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

    // Heap-allocated via shared_ptr -- ned::ui::Widget itself has no
    // ownership contract requiring this (see Widget.h's own header comment)
    // -- purely so widget-specific methods (SetScrollBar, RevealPath, etc.)
    // can still be called directly by typed pointer, the same cross-widget
    // wiring this composition root already established.
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
        *buffer, killRing, registers, promptHistory, bufferList, registry, janetKeymap, globalKeymap, std::move(mode),
        statusMessage, theme);

    // session-persistence-window-layout follow-up: RestoreWindowLayout used
    // to run right here -- moved below, after EnableAsyncFileLoading/
    // EnableAsyncHugeFileLoading and the deferred-session-open loop (see
    // huge-file-session-restore follow-up there), since a leaf referencing a
    // deferred large/huge file can't resolve yet at this point and
    // BuildNodeFromLayout would otherwise discard the whole restored split
    // tree (not just misfocus one pane -- see that follow-up's own comment).

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
        [wm = windowManager.get()](ned::text::Buffer& buffer) -> void { wm->NotifyBufferClosing(buffer); });

    projectSidebar->SetOnBinaryFileOpenRequest(
        [wm = windowManager.get()](const std::filesystem::path& path) { wm->RequestOpenBinaryFile(path); });

    // sidebar-keyboard-focus follow-up: Escape/C-g (or Enter opening a
    // file) hands the keyboard back to the focused pane's BufferView --
    // WindowManager::TakeFocus already handles the "no pane currently
    // reports Focused()" state this necessarily runs in (the sidebar holds
    // focus at that moment) via its first-leaf fallback.
    projectSidebar->SetOnFocusReturn([wm = windowManager.get()] { wm->TakeFocus(); });

    // sidebar-width-memory follow-up: a committed divider drag becomes the
    // remembered global default width (read back a few lines below on the
    // next launch).
    projectSidebar->SetOnWidthCommitted(
        [](int width) { ned::editor::SetVariable("sidebar-width", std::to_string(width)); });

    // ...and a deliberate open/close toggle becomes the remembered global
    // default visibility the same way (only user toggles commit -- see
    // SetOnCollapseCommitted's own comment for why C-c p's transient
    // expand never lands here).
    projectSidebar->SetOnCollapseCommitted(
        [](bool collapsed) { ned::editor::SetVariable("sidebar-visible", collapsed ? "false" : "true"); });

    // sidebar-width-memory follow-up: the width the user last dragged the
    // divider to, remembered globally (ProjectSidebar::EndResize writes it
    // through variables.json). Applied before the session block below on
    // purpose -- a project session's own per-project width is the more
    // specific fact and wins by overwriting this one.
    if (const auto rememberedWidth = ned::editor::Variable("sidebar-width")) {
        try {
            projectSidebar->SetWidth(std::stoi(*rememberedWidth));
        }
        catch (const std::exception&) {
            // Malformed state (hand-edited variables.json) -- keep the default.
        }
    }
    if (const auto rememberedVisible = ned::editor::Variable("sidebar-visible")) {
        projectSidebar->SetCollapsed(*rememberedVisible == "false");
    }

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
    // Container::Paint() call, so it always reflects whatever
    // ProjectSidebar::Width() currently is -- including the 1-column strip
    // Width() reports while collapsed
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

    // Widget::TakeFocus (Widget.h) is a flat, direct write to a
    // process-wide registry, indifferent to whatever tree shape does or
    // doesn't exist around the target widget, so this call would work
    // identically from inside WindowManager's own constructor too. Left at
    // this exact call site anyway: moving it would be a pure refactor with
    // no behavior change.
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

    // session-persistence slice 3 (+ project-plugin-autoload follow-up): the
    // untrusted-file prompt queue deferred from the load loop above, now
    // that a focused pane exists to drive it (same deferral
    // RequestOpenBinaryFile gets, and the same "must run after TakeFocus()"
    // reason). Loading this late instead of beside the global init.janet is
    // the accepted cost of prompting at all: mode/plugin overrides a
    // first-time project file registers won't affect the already-selected
    // initial Mode until the next buffer switch. Each entry gets its own
    // y/n/a prompt, one at a time, chained through the decision callback --
    // recursing through the same shared_ptr<function> so a project with
    // several new/changed files gets asked about every one of them, not just
    // the first. The hash is recomputed at decision time so what gets
    // recorded as trusted is exactly what got loaded, not what was on disk
    // at startup.
    if (!deferredTrustPrompts.empty()) {
        auto promptNext = std::make_shared<std::function<void()>>();
        *promptNext     = [wm = windowManager.get(), &janetEnv, &statusMessage, deferredTrustPrompts,
                           promptNext]() mutable -> void {
            if (deferredTrustPrompts.empty()) {
                return;
            }
            const std::filesystem::path path = deferredTrustPrompts.front();
            deferredTrustPrompts.pop_front();
            wm->RequestTrustProjectInit(
                path, [&janetEnv, &statusMessage, promptNext](const std::filesystem::path& initPath, ned::editor::ProjectInitDecision decision) -> void {
                    if (decision == ned::editor::ProjectInitDecision::Decline) {
                        statusMessage = initPath.string() + " not loaded.";
                    }
                    else {
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
                            statusMessage = initPath.string() + " error: " + e.what();
                        }
                    }
                    (*promptNext)();
                });
        };
        (*promptNext)();
    }

    // EventLoop's constructor (notcurses_core_init) is what enters the
    // alternate screen buffer, places the cursor, and starts reading stdin
    // (see the --detect-theme branch's own comment above for why
    // RunDetectTheme must finish strictly before this point).
    EventLoop eventLoop;

    // background-mode-prewarm follow-up: builds every already-open buffer's
    // Mode (tree-sitter parse included) on a background thread right now,
    // uniformly -- not just the buffer about to be shown first -- so
    // switching to any of them later, even for the very first time, never
    // pays for that build synchronously; see ModePrewarm.h's own doc
    // comment. Buffers opened before this point (session restore, CLI
    // args) couldn't be prewarmed any earlier than this: a background
    // thread's only way to hand its result back to the main thread is
    // eventLoop.Post, and eventLoop doesn't exist until the line above.
    // modePrewarmer outlives eventLoop.Run() below, the same "declared
    // before, destroyed after" scoping every other process-lifetime
    // manager here (lspManager, dapManager, taskRunner, ...) already uses.
    ned::editor::ModePrewarmer modePrewarmer(bufferList, eventLoop);
    for (const auto& openBuffer : bufferList.Buffers()) {
        modePrewarmer.Prewarm(*openBuffer);
    }
    bufferList.SetOnFileOpened([&modePrewarmer](ned::text::Buffer& opened) -> void {
        ned::editor::RestoreFilePlace(opened, static_cast<std::size_t>(ned::editor::TabWidth()));
        ned::editor::TryRestoreUndoHistory(opened);
        modePrewarmer.Prewarm(opened);
    });

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
    windowManager->SetThemeApplier([&theme, limitedTerminal](const ned::ui::Theme& next) -> void {
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

    // Self-hosting-completion follow-up: same "connect after construction,
    // unset is a safe no-op" wiring as SetLspManager just above -- janetEnv
    // outlives windowManager (declared well before it, so destroyed after
    // it in reverse order), so this raw pointer is safe for the whole run,
    // including every "ned/*" completion request Janet-mode ghost-text
    // completion issues against it later.
    windowManager->SetJanetEnvironment(&janetEnv);

    // task-runner follow-up: same "constructed here, needs a real EventLoop&"
    // shape as lspManager just above, and the same "wired into windowManager,
    // connect after construction" convention.
    ned::editor::tasks::TaskRunner taskRunner(bufferList, eventLoop);
    windowManager->SetTaskRunner(&taskRunner);

    // project-undo follow-up: no EventLoop/subprocess dependency (unlike
    // every manager around it) -- just a process-lifetime, in-memory
    // undo/redo transaction log. Same "wired into windowManager, connect
    // after construction" convention regardless.
    ned::editor::ProjectUndoManager projectUndo;
    windowManager->SetProjectUndo(&projectUndo);

    // test-runner integration: same shape as taskRunner just above. The
    // outcome hook keeps an already-open "*test results*" buffer live --
    // rebuilt in place on every parse, never created unprompted (the user
    // opens it via show-test-results, C-c T r).
    ned::editor::testrun::TestRunner testRunner(bufferList, eventLoop);
    windowManager->SetTestRunner(&testRunner);
    testRunner.SetOnOutcomeChanged([&bufferList, &testRunner] {
        if (bufferList.Find(ned::editor::testrun::TestResultsBufferName()) && testRunner.LatestOutcome()) {
            ned::editor::testrun::RebuildTestResultsBuffer(bufferList, *testRunner.LatestOutcome());
        }
    });

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

    // ACP client slice 2: same "constructed here, needs a real EventLoop&"
    // shape as dapManager just above, and the same "wired into
    // windowManager, connect after construction" convention.
    ned::editor::acp::AcpManager acpManager(bufferList, eventLoop);
    windowManager->SetAcpManager(&acpManager);

    // BufferView's completion-debounce/status-message-idle-timeout
    // DeadlineTimers and ScrollArrowButton's press-and-hold repeat both need
    // a real EventLoop& too (see their own SetEventLoop doc comments) --
    // forwarded to every pane, present and future, the same "connect after
    // construction" shape SetProjectSidebar/SetLspManager already establish.
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

    // file-watcher follow-up: same "only the real, running editor opts in,
    // needs the owning EventLoop" reasoning as StartAutoSaveTimer just
    // above -- an inotify trigger so external file changes start the
    // revert/merge sweep near-instantly; the timer tick above keeps
    // running as the safety net (see WindowManager::StartFileWatcher).
    windowManager->StartFileWatcher(eventLoop);

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
    // progressive-huge-file-load follow-up: same wiring, same CLI-arg gap
    // (a file over HugeFileThreshold() passed directly on the command line
    // still opens synchronously via the top-of-main OpenOrCreateFile call
    // below, before this hook exists) -- every interactive open of a huge
    // file gets the progressive, editable-while-loading path from here on.
    windowManager->EnableAsyncHugeFileLoading(eventLoop);

    // huge-file-session-restore follow-up: the restored-session files
    // deferred above (see deferredSessionOpenPaths' own comment), opened now
    // that the async/huge opener hooks exist -- each becomes an ordinary
    // background buffer (streamed in the same way an interactive open of the
    // same file would be), same "no per-file interactive confirmation"
    // contract the rest of session restore already has. RestoreWindowLayout
    // itself also has to wait until after this loop: it resolves every leaf
    // by path via a single BuildNodeFromLayout pass with no open of its
    // own -- called any earlier, a leaf naming one of these still-unopened
    // deferred paths would fail to resolve, and BuildNodeFromLayout discards
    // the *entire* restored tree on any single unresolvable leaf, not just
    // that one pane.
    for (const auto& file : deferredSessionOpenPaths) {
        try {
            bufferList.OpenOrCreateFile(file, forceBinary);
        }
        catch (const std::exception&) {
            // Best-effort, same as the synchronous half of this loop above.
        }
    }
    if (restoredSession) {
        windowManager->RestoreWindowLayout(*restoredSession);
    }

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
                startupScratch = nullptr;
            }
        }
        catch (const std::exception& e) {
            statusMessage = e.what();
        }
    }

    // huge-file-session-restore follow-up: a fallback for the rare case
    // RestoreWindowLayout above couldn't rebuild the pane tree at all (an
    // old session predating windowLayout, or a referenced file that still
    // failed to open even after the retry above) -- redone independently of
    // that method's own focusedPanePath so a plain single-default-pane
    // startup still lands on the intended activeFile/first-openFiles buffer
    // rather than whatever was resolvable earliest. A no-op when
    // RestoreWindowLayout already succeeded: FocusedActiveBuffer() already
    // names the same buffer this recomputes (both trace back to the same
    // saved focus), so the .Set() below is a harmless no-op re-application
    // in that case. pathArg == nullptr guards this the same way as
    // RestoreWindowLayout implicitly is guarded by deferredLargeOpenPath's
    // own unconditional focus override just above -- a CLI file, sync or
    // deferred, always wins outright.
    if (pathArg == nullptr && restoredSession && !deferredSessionOpenPaths.empty()) {
        std::optional<std::filesystem::path> focusPath;
        if (restoredSession->activeFile && bufferList.FindByPath(*restoredSession->activeFile) != nullptr) {
            focusPath = restoredSession->activeFile;
        }
        if (!focusPath) {
            for (const auto& file : restoredSession->openFiles) {
                if (bufferList.FindByPath(file) != nullptr) {
                    focusPath = file;
                    break;
                }
            }
        }
        if (focusPath) {
            ned::text::Buffer* resolved = bufferList.FindByPath(*focusPath);
            windowManager->FocusedActiveBuffer().Set(*resolved);
            projectSidebar->RevealPath(*focusPath);
            if (startupScratch != nullptr && !startupScratch->Modified()) {
                windowManager->NotifyBufferClosing(*startupScratch);
                bufferList.Close(startupScratch->Name());
                startupScratch = nullptr;
            }
        }
    }

    // Mouse events are enabled by EventLoop's constructor too, via
    // notcurses_mice_enable(NCMICE_ALL_EVENTS) -- nothing left to set
    // explicitly here for that.
    // A single Screen (Widget.h) reused across every frame, resized to
    // match the terminal on every onResize callback.
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
        return Box{.x_min = 0,
                   .x_max = std::max(0, size.width - 1 - MinimapOverlayReserve()),
                   .y_min = std::max(1, yMax - height + 1),
                   .y_max = yMax};
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
    auto toggleTerminal = [&overlays, panel = terminalPanel.get(), wm = windowManager.get()] {
        if (!overlays.IsVisible(*panel)) {
            overlays.Show(*panel);
            panel->EnsureStarted();
            panel->TakeFocus();
        }
        else {
            overlays.Hide(*panel);
            // vcs-diff-gutter-staleness follow-up: closing the embedded
            // terminal is the single most likely moment a `git commit`/
            // `git checkout` just ran from inside ned itself -- refresh
            // every pane's diff gutter right away rather than waiting out
            // the periodic autosave-tick sweep (WindowManager::
            // StartAutoSaveTimer) that otherwise catches this too.
            wm->RefreshVcsDiffGutters();
        }
    };
    windowManager->SetOnTerminalToggle(toggleTerminal);
    terminalPanel->SetOnToggleRequest(toggleTerminal);

    // ACP chat panel follow-up: same OverlayHost-overlay shape as
    // terminalPanel just above, dockable at the bottom (default, mirroring
    // the terminal drawer's own geometry) or the right edge, per the
    // Janet-configurable ned/set-acp-panel-dock -- re-read fresh on every
    // Reflow/Show, the same pull-fresh convention every other percent-style
    // setting in this file already follows.
    ned::ui::AcpPanel acpPanel(theme);
    acpPanel.SetAcpManager(&acpManager);
    // ACP round-1-live-validation follow-up: lets a pending permission
    // request resolve inside this panel instead of the focused pane's echo
    // area whenever the panel itself has focus -- see
    // WindowManager::SetAcpPanelFocusChecker's own doc comment.
    windowManager->SetAcpPanelFocusChecker([&acpPanel] { return acpPanel.Focused(); });
    overlays.Add(acpPanel, [](Size size) {
        // acp-panel-minimap-overlap follow-up: see MinimapOverlayReserve's
        // own comment for why this is needed at all.
        const int minimapReserve = MinimapOverlayReserve();
        const int yMax           = std::max(1, size.height - 2); // above the echo area row
        if (ned::editor::acp::GetAcpPanelDock() == ned::editor::acp::AcpPanelDock::Right) {
            const int width = std::clamp(size.width * ned::editor::acp::AcpPanelSizePercent() / 100, 20, size.width - 1);
            const int xMin  = size.width - width;
            return Box{.x_min = xMin, .x_max = std::max(xMin, size.width - 1 - minimapReserve), .y_min = 1, .y_max = yMax};
        }
        const int height = std::max(4, size.height * ned::editor::acp::AcpPanelSizePercent() / 100);
        return Box{
            .x_min = 0, .x_max = std::max(0, size.width - 1 - minimapReserve), .y_min = std::max(1, yMax - height + 1), .y_max = yMax};
    });
    overlays.SetFocusReturn(acpPanel, [wm = windowManager.get()] { wm->TakeFocus(); });
    // Auto-opens the panel the instant a session actually produces
    // content -- regardless of whether the session was started via the
    // panel's own future entry points or the existing echo-area
    // acp-start-session prompt.
    acpManager.SetOnTranscriptChanged([&overlays, panel = &acpPanel] {
        if (!overlays.IsVisible(*panel)) {
            overlays.Show(*panel);
        }
    });
    auto toggleAcpPanel = [&overlays, panel = &acpPanel] {
        if (!overlays.IsVisible(*panel)) {
            overlays.Show(*panel);
            panel->TakeFocus();
        }
        else {
            overlays.Hide(*panel);
        }
    };
    windowManager->SetOnAcpPanelToggle(toggleAcpPanel);
    acpPanel.SetOnToggleRequest(toggleAcpPanel);

    // DAP round 2: the debug console (REPL) panel -- same OverlayHost-overlay
    // shape as terminalPanel/acpPanel above, hardcoded bottom-dock like
    // terminalPanel (a REPL is naturally bottom-docked; no dock-side config
    // was asked for, unlike the ACP chat panel's left/right choice).
    ned::ui::DebugConsolePanel dapConsolePanel(theme);
    dapConsolePanel.SetDapManager(&dapManager);
    overlays.Add(dapConsolePanel, [](Size size) {
        const int yMax   = std::max(1, size.height - 2); // above the echo area row
        const int height = std::max(4, size.height * 30 / 100);
        return Box{.x_min = 0,
                   .x_max = std::max(0, size.width - 1 - MinimapOverlayReserve()),
                   .y_min = std::max(1, yMax - height + 1),
                   .y_max = yMax};
    });
    overlays.SetFocusReturn(dapConsolePanel, [wm = windowManager.get()] { wm->TakeFocus(); });
    auto toggleDapConsole = [&overlays, panel = &dapConsolePanel] {
        if (!overlays.IsVisible(*panel)) {
            overlays.Show(*panel);
            panel->TakeFocus();
        }
        else {
            overlays.Hide(*panel);
        }
    };
    windowManager->SetOnDapConsoleToggle(toggleDapConsole);
    dapConsolePanel.SetOnToggleRequest(toggleDapConsole);

    // generic-popup follow-up: list-buffers (C-x C-b), the focus-mode
    // ListPopup's first real consumer -- same OverlayHost-overlay shape as
    // the panels above, but roughly centered and most-of-screen (a real
    // working list, not a small hint/drawer) rather than docked to an edge.
    ned::ui::BufferListPanel bufferListPanel(theme, bufferList);
    overlays.Add(bufferListPanel.Popup(), [](Size size) {
        const int width  = std::clamp(size.width - 8, 20, 80);
        const int height = std::clamp(size.height - 6, 6, 24);
        const int xMin   = std::max(0, (size.width - width) / 2);
        const int yMin   = std::max(0, (size.height - height) / 2);
        return Box{.x_min = xMin, .x_max = xMin + width - 1, .y_min = yMin, .y_max = yMin + height - 1};
    });
    overlays.SetFocusReturn(bufferListPanel.Popup(), [wm = windowManager.get()] { wm->TakeFocus(); });
    bufferListPanel.SetOnRequestSwitchToBuffer(
        [&overlays, panel = &bufferListPanel, wm = windowManager.get()](ned::text::Buffer& buffer) {
            wm->FocusedActiveBuffer().Set(buffer);
            overlays.Hide(panel->Popup());
        });
    bufferListPanel.SetOnCancel(
        [&overlays, panel = &bufferListPanel] { overlays.Hide(panel->Popup()); });
    bufferListPanel.SetOnBufferClosing(
        [wm = windowManager.get()](ned::text::Buffer& buffer) { wm->NotifyBufferClosing(buffer); });
    windowManager->SetOnBufferListToggle([&overlays, panel = &bufferListPanel] {
        if (!overlays.IsVisible(panel->Popup())) {
            panel->Show();
            overlays.Show(panel->Popup());
            panel->Popup().TakeFocus();
        }
        else {
            overlays.Hide(panel->Popup());
        }
    });

    // which-key follow-up (generic-popup follow-up: now a ListPopup in its
    // non-focusable mode): a small popup shown the instant a prefix chord
    // (C-x, C-c, ...) becomes pending -- unlike the panels above, never
    // taken focus (SetFocusable() is left at its default false), so no
    // SetFocusReturn/toggle wiring is needed; BufferView keeps receiving
    // every keystroke throughout. Bottom-anchored, just above the echo area
    // row, sized to the current hint's own row count (ContentRowCount())
    // each time it's shown/reflowed.
    ned::ui::ListPopup whichKeyPopup(theme);
    overlays.Add(whichKeyPopup, [panel = &whichKeyPopup](Size size) {
        const int yMax   = std::max(1, size.height - 2); // above the echo area row
        const int height = std::clamp(panel->ContentRowCount(), 3, std::min(12, size.height));
        const int width  = std::min(50, size.width);
        return Box{.x_min = 0, .x_max = std::max(0, width - 1), .y_min = std::max(0, yMax - height + 1), .y_max = yMax};
    });
    windowManager->SetOnPrefixHintChanged([&overlays, panel = &whichKeyPopup](std::optional<ned::ui::WhichKeyHint> hint) {
        if (hint) {
            ned::ui::ListPopupModel model;
            model.title = hint->prefixLabel;
            model.rows.reserve(hint->bindings.size());
            for (const auto& [chord, label] : hint->bindings) {
                model.rows.push_back({.left = chord, .main = label, .accented = true});
            }
            panel->SetModel(std::move(model));
            overlays.Show(*panel);
        }
        else {
            overlays.Hide(*panel);
        }
    });

    // generic-popup follow-up (Phase 3): the shared candidate popup behind
    // M-x/project-find-file/find-recent-file/bookmark-jump/select-theme/LSP
    // code-action-select -- same non-focusable, BufferView-keeps-focus
    // shape as whichKeyPopup just above (only one of these sessions, or
    // which-key's own hint, is ever live at a time, so one shared instance
    // is enough), just taller/wider since a candidate list routinely has
    // more rows and longer entries (file paths) than a key-binding hint.
    ned::ui::ListPopup candidatePopup(theme);
    overlays.Add(candidatePopup, [panel = &candidatePopup](Size size) {
        const int yMax   = std::max(1, size.height - 2); // above the echo area row
        const int height = std::clamp(panel->ContentRowCount(), 3, std::min(14, size.height));
        const int width  = std::min(90, size.width);
        return Box{.x_min = 0, .x_max = std::max(0, width - 1), .y_min = std::max(0, yMax - height + 1), .y_max = yMax};
    });
    windowManager->SetOnCandidatesChanged(
        [&overlays, panel = &candidatePopup](std::optional<ned::ui::ListPopupModel> model) {
            if (model) {
                panel->SetModel(std::move(*model));
                overlays.Show(*panel);
            }
            else {
                overlays.Hide(*panel);
            }
        });

    // completion-popup follow-up: replaces LSP/dabbrev/Janet-binding ghost
    // text entirely -- a dedicated ListPopup+overlay, not a reuse of
    // candidatePopup above (see BufferView::SetOnCompletionChanged's own
    // doc comment for why: this is non-modal, live-typing UI, not a
    // mutually-exclusive modal prompt session). Placement is anchor-aware
    // (BufferView::CompletionAnchorNow, carried through ListPopupModel::
    // anchor/ListPopup::Anchor()) rather than docked -- opens directly under
    // point, flipping to open above when it wouldn't fit before the
    // terminal's bottom edge, and shifted horizontally to stay on screen.
    ned::ui::ListPopup completionPopup(theme);
    overlays.Add(completionPopup, [panel = &completionPopup](Size size) {
        const ned::ui::Point origin = panel->Anchor().value_or(ned::ui::Point{});
        const int            width  = std::min(64, size.width);
        const int            height = std::clamp(panel->ContentRowCount(), 3, std::min(10, size.height));

        const int xMin = std::clamp(origin.x, 0, std::max(0, size.width - width));
        const int xMax = std::min(size.width - 1, xMin + width - 1);

        int yMin, yMax;
        if (origin.y + height - 1 <= size.height - 1) {
            // Fits below the anchor (one row under point) -- the common case.
            yMin = origin.y;
            yMax = yMin + height - 1;
        }
        else {
            // Flip upward, ending just above point's own line (origin.y -
            // 1) so the popup never covers the line being typed on.
            yMax = std::max(0, origin.y - 2);
            yMin = std::max(0, yMax - height + 1);
        }
        return Box{.x_min = xMin, .x_max = xMax, .y_min = yMin, .y_max = yMax};
    });
    windowManager->SetOnCompletionChanged(
        [&overlays, panel = &completionPopup](std::optional<ned::ui::ListPopupModel> model) {
            if (model) {
                panel->SetModel(std::move(*model));
                overlays.Show(*panel);
            }
            else {
                overlays.Hide(*panel);
            }
        });

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

    // Pixel-blitter-minimap follow-up: must run before eventLoop itself is
    // destroyed (which happens at this function's own closing brace,
    // *before* windowManager -- windowManager was declared earlier in this
    // function, so it's destroyed later, after ~EventLoop already called
    // notcurses_stop -- see WindowManager::ReleaseMinimapPixelPlanes()'s own
    // doc comment for why that ordering makes a plane torn down by
    // ~Minimap() itself a real, confirmed SIGABRT).
    windowManager->ReleaseMinimapPixelPlanes();

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
    logShutdown("post-run: saving recent files and bookmarks");
    ned::editor::SaveRecentFiles(/*force=*/true);
    ned::editor::SaveBookmarks(/*force=*/true);
    logShutdown("post-run: saving undo history");
    ned::editor::SaveUndoHistoryForOpenBuffers(bufferList);
    logShutdown("post-run: saving project session");
    windowManager->SaveProjectSessionNow();
    // graceful-lsp-shutdown follow-up: sends "shutdown"+"exit" to every
    // directly-spawned (non-broker) running LSP client before the local
    // teardown below destroys lspManager -- see LspManager::Shutdown's own
    // doc comment for why this doesn't (and can't) wait for the shutdown
    // response, and why that's fine: ChildProcess::~ChildProcess()'s
    // existing bounded close-stdin/poll/SIGKILL-escalation sequence is what
    // actually bounds the wait, unchanged by this call.
    logShutdown("post-run: sending shutdown/exit to direct-spawn LSP clients");
    lspManager.Shutdown();
    logShutdown("post-run: explicit steps done; entering local destruction "
                "(terminal pty, DAP, VCS, task runner, LSP clients, window tree, Janet, EventLoop/terminal restore)");

    return 0;
}
