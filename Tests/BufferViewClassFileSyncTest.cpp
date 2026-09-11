//
// class-file-sync end to end: the M-x commands and the unprompted offers,
// through BufferView's own y/n and out into a real file rename on disk. The
// rules themselves -- containment, the first-dot filename split, when an
// offer is certain enough to make -- are unit-tested purely in
// ClassFileSyncTest.cpp, and each language's tags query in ModeTest.cpp; what
// this file pins is the wiring between them, and that "no" really does
// nothing.
//

#include <catch2/catch_test_macros.hpp>

#include <poll.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#include "Editor/ClassFileSyncSettings.h"
#include "Editor/Commands.h"
#include "Editor/Dispatcher.h"
#include "Editor/ImportFixupSettings.h"
#include "Editor/Keymap.h"
#include "Editor/Lsp/Client.h"
#include "Editor/Lsp/Manager.h"
#include "Editor/Mode.h"
#include "Editor/ModeOverrides.h"
#include "Editor/Multibuffer.h"
#include "Editor/Project/Root.h"
#include "Editor/PromptHistory.h"
#include "Editor/Register.h"
#include "Editor/RenameReviewSettings.h"
#include "TestEvents.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"
#include "UI/ActiveBuffer.h"
#include "UI/BufferView.h"
#include "UI/EventLoop.h"
#include "UI/Theme.h"

using ned::text::Buffer;
using ned::ui::BufferView;
namespace test = ned::ui::test;

namespace {

struct RegistryResetGuard {
    RegistryResetGuard() {
        ned::editor::multibuffer::ClearRegistryForTesting();
    }
    ~RegistryResetGuard() {
        ned::editor::multibuffer::ClearRegistryForTesting();
    }
};

struct ProjectRootResetGuard {
    std::filesystem::path saved = ned::editor::ProjectRoot();
    ~ProjectRootResetGuard() {
        ned::editor::SetProjectRoot(saved);
    }
};

// The import scan is a project-wide search with nothing to find in these
// one-file projects; turned off so a rename here measures only this feature.
struct ImportFixupGuard {
    explicit ImportFixupGuard(bool enabled) : previous_(ned::editor::ImportFixupEnabled()) {
        ned::editor::SetImportFixupEnabled(enabled);
    }
    ~ImportFixupGuard() {
        ned::editor::SetImportFixupEnabled(previous_);
    }
    bool previous_;
};

std::size_t NextFixtureId() {
    static std::size_t next = 0;
    return next++;
}

// One real file in a real directory, opened through BufferList so the buffer
// carries a Path() the rename can act on.
struct Fixture {
    RegistryResetGuard    registryResetGuard;
    ProjectRootResetGuard rootGuard;
    ImportFixupGuard      importGuard{false};
    std::filesystem::path dir = std::filesystem::temp_directory_path() /
                                ("ned_class_file_sync_test_" + std::to_string(::getpid()) + "_" +
                                 std::to_string(NextFixtureId()));

    ned::text::KillRing          killRing;
    ned::editor::RegisterTable   registers;
    ned::editor::PromptHistory   promptHistory;
    ned::text::BufferList        bufferList;
    ned::editor::CommandRegistry registry{[] {
        ned::editor::CommandRegistry r;
        ned::editor::RegisterBuiltinCommands(r);
        return r;
    }()};
    ned::editor::Keymap          keymap = ned::editor::BuildDefaultGlobalKeymap();
    ned::editor::Dispatcher      dispatcher{registry, ned::editor::KeymapStack({&keymap})};
    ned::editor::Mode            mode  = ned::editor::FundamentalMode();
    ned::ui::Theme               theme = ned::ui::DarkTheme();

    std::string                          statusMessage;
    Buffer*                              opened = nullptr;
    std::optional<ned::ui::ActiveBuffer> activeBuffer;
    std::filesystem::path                file;

    Fixture(const std::string& filename, const std::string& contents) {
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        file = dir / filename;
        std::ofstream(file) << contents;

        ned::editor::SetProjectRoot(dir);
        mode   = ned::editor::ModeForPath(file);
        opened = &bufferList.OpenOrCreateFile(file);
        activeBuffer.emplace(*opened);
    }

    ~Fixture() {
        std::filesystem::remove_all(dir);
    }

    // Returned as a prvalue: BufferView is neither copyable nor movable, so
    // a named local here would not compile.
    BufferView View() {
        return BufferView(*activeBuffer, killRing, registers, promptHistory, bufferList, dispatcher, statusMessage,
                          mode, theme);
    }
};

std::string Content(const Buffer& buffer) {
    return buffer.Content().Substring(0, buffer.Content().ByteLength());
}

void Type(BufferView& view, std::string_view text) {
    for (const char c : text) {
        view.OnEvent(test::Character(std::string(1, c)));
    }
}

// Mirrors BufferViewTest.cpp's and BufferViewDocumentLinkTest.cpp's own
// FakeLspServer/frame helpers, kept file-local there too -- same precedent.
// The automatic offer rides on a SERVER rename landing (a top-level type is
// always a cross-file symbol, so rename-symbol's scope-aware tier declines it
// by construction), so pinning that path needs a server to answer.
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

    static FakeLspServer Create(ned::editor::lsp::Manager& manager, const std::string& language,
                                ned::ui::EventLoop& eventLoop, ned::editor::lsp::Client*& outClient) {
        int clientWritesHere[2];
        int clientReadsHere[2];
        REQUIRE(::pipe(clientWritesHere) == 0);
        REQUIRE(::pipe(clientReadsHere) == 0);
        auto client = std::make_unique<ned::editor::lsp::Client>(
            ned::editor::lsp::Transport(clientReadsHere[0], clientWritesHere[1]), eventLoop);
        outClient = &manager.SetClientForTesting(language, std::move(client));
        return FakeLspServer(clientWritesHere[0], clientReadsHere[1]);
    }
};

bool NoFrameArrives(int fd) {
    pollfd pfd{.fd = fd, .events = POLLIN, .revents = 0};
    return ::poll(&pfd, 1, 200) == 0;
}

// Unlike the copies in BufferViewTest.cpp/BufferViewDocumentLinkTest.cpp,
// this polls before every read. Those are driven by a fixture whose language
// key is known to match the injected client; here it is derived from the
// fixture's own file extension, and getting that wrong parked the whole
// suite on a blocking ::read rather than failing. A test that hangs is worse
// than one that fails, so this reports instead.
std::string ReadRawLspFrame(int fd) {
    std::string all;
    char        buffer[512];
    for (int i = 0; i < 4; ++i) {
        if (NoFrameArrives(fd)) {
            break;
        }
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

void DrainAllPendingFrames(int fd) {
    char buffer[512];
    while (!NoFrameArrives(fd)) {
        if (::read(fd, buffer, sizeof(buffer)) <= 0) {
            break;
        }
    }
}

struct RenameReviewGuard {
    explicit RenameReviewGuard(bool enabled) : previous_(ned::editor::RenameThroughReview()) {
        ned::editor::SetRenameThroughReview(enabled);
    }
    ~RenameReviewGuard() {
        ned::editor::SetRenameThroughReview(previous_);
    }
    bool previous_;
};

struct ClassFileSyncGuard {
    explicit ClassFileSyncGuard(bool enabled) : previous_(ned::editor::ClassFileSyncEnabled()) {
        ned::editor::SetClassFileSync(enabled);
    }
    ~ClassFileSyncGuard() {
        ned::editor::SetClassFileSync(previous_);
    }
    bool previous_;
};

void InvokeCommand(BufferView& view, std::string_view name) {
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 20});
    view.OnEvent(test::Alt('x'));
    Type(view, name);
    view.OnEvent(test::Return());
}

} // namespace

TEST_CASE("rename-file-to-match-type proposes the file's own type and renames on y",
          "[BufferView][ClassFileSync]") {
    Fixture    fixture("Widget.php", "<?php\nnamespace App;\n\nclass Gadget {\n}\n");
    BufferView view = fixture.View();

    InvokeCommand(view, "rename-file-to-match-type");
    INFO("status: " << fixture.statusMessage);
    REQUIRE(fixture.statusMessage.find("Rename Widget.php to Gadget.php") != std::string::npos);
    REQUIRE(fixture.statusMessage.find("(y/n)") != std::string::npos);
    // Nothing has happened yet: the question is a question.
    REQUIRE(std::filesystem::exists(fixture.dir / "Widget.php"));

    view.OnEvent(test::Character("y"));
    CHECK(std::filesystem::exists(fixture.dir / "Gadget.php"));
    CHECK_FALSE(std::filesystem::exists(fixture.dir / "Widget.php"));
    // The open buffer follows the file, exactly as a hand-typed rename-file
    // makes it -- this routes through PerformProjectRename rather than
    // renaming anything itself.
    CHECK(fixture.opened->Path()->filename() == "Gadget.php");
    CHECK(fixture.opened->Name() == "Gadget.php");
}

TEST_CASE("rename-file-to-match-type does nothing at all on n", "[BufferView][ClassFileSync]") {
    Fixture    fixture("Widget.php", "<?php\nclass Gadget {\n}\n");
    BufferView view = fixture.View();

    InvokeCommand(view, "rename-file-to-match-type");
    view.OnEvent(test::Character("n"));

    CHECK(std::filesystem::exists(fixture.dir / "Widget.php"));
    CHECK_FALSE(std::filesystem::exists(fixture.dir / "Gadget.php"));
    CHECK(fixture.statusMessage.find("cancelled") != std::string::npos);
}

TEST_CASE("rename-file-to-match-type keeps a compound suffix through the stem swap",
          "[BufferView][ClassFileSync]") {
    // The PEAR-era spelling: std::filesystem would call the stem
    // "Thing.class" and produce "Widget.php", dropping ".class" silently.
    Fixture    fixture("Thing.class.php", "<?php\nclass Widget {\n}\n");
    BufferView view = fixture.View();

    InvokeCommand(view, "rename-file-to-match-type");
    REQUIRE(fixture.statusMessage.find("Widget.class.php") != std::string::npos);
    view.OnEvent(test::Character("y"));
    CHECK(std::filesystem::exists(fixture.dir / "Widget.class.php"));
}

TEST_CASE("rename-file-to-match-type works for a PHP 8.1 enum and a Java record",
          "[BufferView][ClassFileSync]") {
    {
        Fixture    fixture("Old.php", "<?php\nenum Status: string {\n    case Active = 'active';\n}\n");
        BufferView view = fixture.View();
        InvokeCommand(view, "rename-file-to-match-type");
        INFO("status: " << fixture.statusMessage);
        REQUIRE(fixture.statusMessage.find("Rename Old.php to Status.php") != std::string::npos);
    }
    {
        Fixture    fixture("Old.java", "public record Point(int x, int y) {}\n");
        BufferView view = fixture.View();
        InvokeCommand(view, "rename-file-to-match-type");
        INFO("status: " << fixture.statusMessage);
        REQUIRE(fixture.statusMessage.find("Rename Old.java to Point.java") != std::string::npos);
    }
}

TEST_CASE("rename-file-to-match-type takes the outermost type and says so when a file holds several",
          "[BufferView][ClassFileSync]") {
    // The permissive tier on purpose: the user asked, so it answers rather
    // than refusing -- but it names which one it picked and that there were
    // others, so an unwanted rename is visible before "y", not after.
    Fixture    fixture("Old.ts", "class First {}\n\nclass Second {}\n");
    BufferView view = fixture.View();

    InvokeCommand(view, "rename-file-to-match-type");
    INFO("status: " << fixture.statusMessage);
    CHECK(fixture.statusMessage.find("Rename Old.ts to First.ts") != std::string::npos);
    CHECK(fixture.statusMessage.find("outermost of 2 types") != std::string::npos);
}

TEST_CASE("rename-file-to-match-type declines a file with nothing to be named after",
          "[BufferView][ClassFileSync]") {
    // "if we find no match, we find no match" -- it says so and stops,
    // rather than inventing a name from something that isn't a type.
    Fixture    fixture("helpers.php", "<?php\nfunction add($a, $b) { return $a + $b; }\n");
    BufferView view = fixture.View();

    InvokeCommand(view, "rename-file-to-match-type");
    CHECK(fixture.statusMessage.find("No class, interface, enum, struct or record") != std::string::npos);
    CHECK(std::filesystem::exists(fixture.dir / "helpers.php"));
}

TEST_CASE("rename-file-to-match-type is a no-op when the name already matches", "[BufferView][ClassFileSync]") {
    Fixture    fixture("Widget.php", "<?php\nclass Widget {\n}\n");
    BufferView view = fixture.View();

    InvokeCommand(view, "rename-file-to-match-type");
    CHECK(fixture.statusMessage.find("already named after Widget") != std::string::npos);
}

TEST_CASE("rename-file-to-match-type refuses to overwrite an existing file", "[BufferView][ClassFileSync]") {
    Fixture fixture("Widget.php", "<?php\nclass Gadget {\n}\n");
    std::ofstream(fixture.dir / "Gadget.php") << "<?php\n// someone else's file\n";
    BufferView view = fixture.View();

    InvokeCommand(view, "rename-file-to-match-type");
    CHECK(fixture.statusMessage.find("already exists") != std::string::npos);
    CHECK(std::filesystem::exists(fixture.dir / "Widget.php"));
}

TEST_CASE("rename-file-to-match-type works with the automatic offers turned off",
          "[BufferView][ClassFileSync]") {
    // ned/set-class-file-sync gates only what ned volunteers. Asking
    // explicitly still works -- "stop volunteering" is not "refuse when I
    // ask", and that asymmetry is the setting's whole point.
    const bool previous = ned::editor::ClassFileSyncEnabled();
    ned::editor::SetClassFileSync(false);
    Fixture    fixture("Widget.php", "<?php\nclass Gadget {\n}\n");
    BufferView view = fixture.View();

    InvokeCommand(view, "rename-file-to-match-type");
    CHECK(fixture.statusMessage.find("Rename Widget.php to Gadget.php") != std::string::npos);
    view.OnEvent(test::Character("y"));
    CHECK(std::filesystem::exists(fixture.dir / "Gadget.php"));

    ned::editor::SetClassFileSync(previous);
}

// ---------------------------------------------------------------------------
// The unprompted offer. A top-level type is always a cross-file symbol, so
// rename-symbol's scope-aware tier declines it (LocalBinding::scopeIsFile) and
// the rename reaches the server -- which is why these drive one, and why the
// explicit commands above (which need no server at all) are the whole feature
// for a language with none configured.
// ---------------------------------------------------------------------------

namespace {

// Drives lsp-rename through to a server response renaming `oldName` to
// `newName` across the fixture's own file, and returns with the edit applied.
struct RenameDriver {
    ned::ui::EventLoop        eventLoop;
    ned::editor::lsp::Manager manager;
    ned::editor::lsp::Client* client = nullptr;
    FakeLspServer             server;

    // Keyed by the fixture's OWN language, not a fixed string: these
    // fixtures open .php/.java/.ts files, so a hardcoded key would inject a
    // client the manager never consults and no request would ever be sent.
    explicit RenameDriver(Fixture& fixture) : manager(fixture.bufferList, eventLoop),
                                              server(FakeLspServer::Create(manager, ned::editor::LanguageKeyForMode(fixture.mode), eventLoop, client)) {
    }

    void Run(Fixture& fixture, BufferView& view, std::size_t startByte, const std::string& oldName,
             const std::string& newName) {
        view.SetLspManager(&manager);
        view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 20});

        ned::ui::Screen screenBuf(80, 21);
        ned::ui::Canvas canvas(screenBuf, ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 20});
        view.Paint(canvas);
        DrainAllPendingFrames(server.serverStdinRead);

        fixture.opened->SetPoint(startByte);
        view.OnEvent(test::Ctrl('c'));
        view.OnEvent(test::CtrlAlt('r'));

        const std::string prepareRaw = ReadRawLspFrame(server.serverStdinRead);
        INFO("prepareRename frame: " << prepareRaw);
        REQUIRE(prepareRaw.find("textDocument/prepareRename") != std::string::npos);
        client->DispatchFrame(ned::editor::lsp::Json{{"jsonrpc", "2.0"},
                                                     {"id", LspRequestIdFromFrame(prepareRaw)},
                                                     {"result", ned::editor::lsp::Json{{"defaultBehavior", true}}}}
                                  .dump());

        Type(view, newName);
        view.OnEvent(test::Return());

        const std::string raw = ReadRawLspFrame(server.serverStdinRead);
        INFO("rename frame: " << raw);
        REQUIRE(raw.find("textDocument/rename") != std::string::npos);
        const auto        request = ned::editor::lsp::Json::parse(raw.substr(raw.find("\r\n\r\n") + 4));
        const std::string uri     = request["params"]["textDocument"]["uri"].get<std::string>();

        // One edit per occurrence of oldName in the fixture's file, so the
        // response is a real rename of the type rather than a synthetic one.
        const std::string      text      = Content(*fixture.opened);
        ned::editor::lsp::Json edits     = ned::editor::lsp::Json::array();
        std::size_t            line      = 0;
        std::size_t            lineStart = 0;
        for (std::size_t i = 0; i < text.size(); ++i) {
            if (text[i] == '\n') {
                ++line;
                lineStart = i + 1;
                continue;
            }
            if (text.compare(i, oldName.size(), oldName) != 0) {
                continue;
            }
            const std::size_t column = i - lineStart;
            edits.push_back({{"range",
                              {{"start", {{"line", line}, {"character", column}}},
                               {"end", {{"line", line}, {"character", column + oldName.size()}}}}},
                             {"newText", newName}});
            i += oldName.size() - 1;
        }
        client->DispatchFrame(ned::editor::lsp::Json{{"jsonrpc", "2.0"},
                                                     {"id", LspRequestIdFromFrame(raw)},
                                                     {"result", {{"changes", {{uri, edits}}}}}}
                                  .dump());
    }
};

} // namespace

TEST_CASE("A server rename of the file's own type offers to rename the file too",
          "[BufferView][ClassFileSync]") {
    const ClassFileSyncGuard sync(true);
    const RenameReviewGuard  direct(false); // the review's own commit path is covered separately
    Fixture                  fixture("Widget.php", "<?php\nclass Widget {\n}\n");
    BufferView               view = fixture.View();
    RenameDriver             driver(fixture);

    driver.Run(fixture, view, Content(*fixture.opened).find("Widget"), "Widget", "Gadget");

    INFO("status: " << fixture.statusMessage);
    REQUIRE(fixture.statusMessage.find("Rename Widget.php to Gadget.php as well?") != std::string::npos);
    // Still just a question until it is answered.
    REQUIRE(std::filesystem::exists(fixture.dir / "Widget.php"));

    view.OnEvent(test::Character("y"));
    CHECK(std::filesystem::exists(fixture.dir / "Gadget.php"));
    CHECK_FALSE(std::filesystem::exists(fixture.dir / "Widget.php"));
}

TEST_CASE("The offer is silent when the file was not named after the renamed type",
          "[BufferView][ClassFileSync]") {
    // The evidence-not-convention rule, end to end: Helpers.php deliberately
    // holds class Widget, so renaming it is not a reason to touch the file.
    const ClassFileSyncGuard sync(true);
    const RenameReviewGuard  direct(false);
    Fixture                  fixture("Helpers.php", "<?php\nclass Widget {\n}\n");
    BufferView               view = fixture.View();
    RenameDriver             driver(fixture);

    driver.Run(fixture, view, Content(*fixture.opened).find("Widget"), "Widget", "Gadget");

    CHECK(fixture.statusMessage.find("as well?") == std::string::npos);
    CHECK(std::filesystem::exists(fixture.dir / "Helpers.php"));
}

TEST_CASE("Renaming something other than the file's type never drags the filename along",
          "[BufferView][ClassFileSync]") {
    // The case the "before" half of the evidence exists to exclude: after the
    // fact this file is named "size" and holds a type named "width", which is
    // indistinguishable from a real type rename unless the offer remembers
    // that the file did NOT match its type beforehand.
    const ClassFileSyncGuard sync(true);
    const RenameReviewGuard  direct(false);
    Fixture                  fixture("size.php", "<?php\nclass width {\n    public $size = 1;\n}\n");
    BufferView               view = fixture.View();
    RenameDriver             driver(fixture);

    driver.Run(fixture, view, Content(*fixture.opened).find("$size") + 1, "size", "width");

    INFO("status: " << fixture.statusMessage);
    CHECK(fixture.statusMessage.find("as well?") == std::string::npos);
    CHECK(std::filesystem::exists(fixture.dir / "size.php"));
}

TEST_CASE("ned/set-class-file-sync false stops the offer without touching the rename",
          "[BufferView][ClassFileSync]") {
    const ClassFileSyncGuard sync(false);
    const RenameReviewGuard  direct(false);
    Fixture                  fixture("Widget.php", "<?php\nclass Widget {\n}\n");
    BufferView               view = fixture.View();
    RenameDriver             driver(fixture);

    driver.Run(fixture, view, Content(*fixture.opened).find("Widget"), "Widget", "Gadget");

    CHECK(fixture.statusMessage.find("as well?") == std::string::npos);
    CHECK(Content(*fixture.opened).find("class Gadget") != std::string::npos); // the rename itself still happened
    CHECK(std::filesystem::exists(fixture.dir / "Widget.php"));
}

TEST_CASE("Committing a rename review offers the file rename, and excluding it does not",
          "[BufferView][ClassFileSync]") {
    const ClassFileSyncGuard sync(true);
    const RenameReviewGuard  review(true);

    SECTION("committed") {
        Fixture      fixture("Widget.php", "<?php\nclass Widget {\n}\n");
        BufferView   view = fixture.View();
        RenameDriver driver(fixture);
        driver.Run(fixture, view, Content(*fixture.opened).find("Widget"), "Widget", "Gadget");

        // The review is what came up; nothing has landed yet, so no offer.
        REQUIRE(fixture.bufferList.Find("*rename*") != nullptr);
        REQUIRE(fixture.statusMessage.find("as well?") == std::string::npos);

        view.OnEvent(test::Ctrl('c'));
        view.OnEvent(test::Ctrl('c'));
        view.OnEvent(test::Character("b")); // into the open buffers

        INFO("status: " << fixture.statusMessage);
        CHECK(fixture.statusMessage.find("Rename Widget.php to Gadget.php as well?") != std::string::npos);
    }

    SECTION("excluded") {
        Fixture      fixture("Widget.php", "<?php\nclass Widget {\n}\n");
        BufferView   view = fixture.View();
        RenameDriver driver(fixture);
        driver.Run(fixture, view, Content(*fixture.opened).find("Widget"), "Widget", "Gadget");

        Buffer* const reviewBuffer = fixture.bufferList.Find("*rename*");
        REQUIRE(reviewBuffer != nullptr);
        // M-n first: a fresh review leaves point at offset 0, which is an
        // excerpt HEADER line, and M-r only acts on a body.
        view.OnEvent(test::Alt('n'));
        view.OnEvent(test::Alt('r')); // revert this excerpt: the rename is excluded
        REQUIRE(Content(*reviewBuffer).find("class Widget") != std::string::npos);

        view.OnEvent(test::Ctrl('c'));
        view.OnEvent(test::Ctrl('c'));
        view.OnEvent(test::Character("b"));

        // Nothing was renamed, so there is nothing to follow up on -- the
        // "after" half of the evidence is re-read rather than assumed.
        INFO("status: " << fixture.statusMessage);
        CHECK(fixture.statusMessage.find("as well?") == std::string::npos);
        CHECK(Content(*fixture.opened).find("class Widget") != std::string::npos);
    }
}

// ---------------------------------------------------------------------------
// Direction B: the file's name drives the type's. Two tiers at accept time --
// a running server renames across the dependency tree, and with none, a
// best-guess rename of this file's own top-level type and its whole-word
// occurrences here, which says plainly that it went no further.
// ---------------------------------------------------------------------------

TEST_CASE("rename-type-to-match-file proposes the file's stem and renames in-file with no server",
          "[BufferView][ClassFileSync]") {
    const RenameReviewGuard direct(false);
    Fixture                 fixture("Gadget.php", "<?php\nclass Widget {\n    public function make(): Widget {\n"
                                                  "        return new Widget();\n    }\n}\n");
    BufferView              view = fixture.View();

    InvokeCommand(view, "rename-type-to-match-file");
    INFO("status: " << fixture.statusMessage);
    REQUIRE(fixture.statusMessage.find("Rename Widget to Gadget, after Gadget.php") != std::string::npos);

    view.OnEvent(test::Character("y"));
    INFO("after: " << fixture.statusMessage);
    const std::string after = Content(*fixture.opened);
    CHECK(after.find("class Gadget {") != std::string::npos);
    CHECK(after.find("): Gadget {") != std::string::npos);
    CHECK(after.find("new Gadget()") != std::string::npos);
    CHECK(after.find("Widget") == std::string::npos);
    // The limit is named rather than left to be discovered later.
    CHECK(fixture.statusMessage.find("Other files are not updated") != std::string::npos);
}

TEST_CASE("The no-server tier's in-file rename is one undo step", "[BufferView][ClassFileSync]") {
    const RenameReviewGuard direct(false);
    const std::string       source = "<?php\nclass Widget {\n    public function make(): Widget {}\n}\n";
    Fixture                 fixture("Gadget.php", source);
    BufferView              view = fixture.View();

    InvokeCommand(view, "rename-type-to-match-file");
    view.OnEvent(test::Character("y"));
    REQUIRE(Content(*fixture.opened).find("class Gadget") != std::string::npos);

    fixture.opened->Undo();
    CHECK(Content(*fixture.opened) == source); // one step, not three
}

TEST_CASE("The no-server tier leaves a comment occurrence alone", "[BufferView][ClassFileSync]") {
    const RenameReviewGuard direct(false);
    Fixture                 fixture("Gadget.php", "<?php\n// Widget does the thing\nclass Widget {\n}\n");
    BufferView              view = fixture.View();

    InvokeCommand(view, "rename-type-to-match-file");
    view.OnEvent(test::Character("y"));

    const std::string after = Content(*fixture.opened);
    CHECK(after.find("class Gadget {") != std::string::npos);
    // Classified as a comment hit and never rewritten unasked -- the same
    // rule a symbol rename's own review applies.
    CHECK(after.find("// Widget does the thing") != std::string::npos);
}

TEST_CASE("rename-type-to-match-file opens a review when reviews are on", "[BufferView][ClassFileSync]") {
    const RenameReviewGuard review(true);
    const std::string       source = "<?php\nclass Widget {\n}\n";
    Fixture                 fixture("Gadget.php", source);
    BufferView              view = fixture.View();

    InvokeCommand(view, "rename-type-to-match-file");
    view.OnEvent(test::Character("y"));

    Buffer* const reviewBuffer = fixture.bufferList.Find("*rename*");
    REQUIRE(reviewBuffer != nullptr);
    CHECK(Content(*reviewBuffer).find("class Gadget {") != std::string::npos);
    CHECK(Content(*fixture.opened) == source); // nothing lands until it is committed
    CHECK(fixture.statusMessage.find("Other files are not updated") != std::string::npos);
}

TEST_CASE("rename-type-to-match-file declines a filename that is not a type name",
          "[BufferView][ClassFileSync]") {
    // "if we find no match, we find no match" -- ordinary filenames that
    // simply are not type names decline instead of proposing something odd.
    Fixture    fixture("02_migration.php", "<?php\nclass Widget {\n}\n");
    BufferView view = fixture.View();

    InvokeCommand(view, "rename-type-to-match-file");
    CHECK(fixture.statusMessage.find("would not be a valid type name") != std::string::npos);
    CHECK(Content(*fixture.opened).find("class Widget") != std::string::npos);
}

TEST_CASE("rename-type-to-match-file is a no-op when the type already matches",
          "[BufferView][ClassFileSync]") {
    Fixture    fixture("Widget.php", "<?php\nclass Widget {\n}\n");
    BufferView view = fixture.View();

    InvokeCommand(view, "rename-type-to-match-file");
    CHECK(fixture.statusMessage.find("already named after Widget.php") != std::string::npos);
}

TEST_CASE("rename-type-to-match-file reads through a compound suffix", "[BufferView][ClassFileSync]") {
    const RenameReviewGuard direct(false);
    Fixture                 fixture("Gadget.class.php", "<?php\nclass Widget {\n}\n");
    BufferView              view = fixture.View();

    InvokeCommand(view, "rename-type-to-match-file");
    // "Gadget", not "Gadget.class" -- the same first-dot split the other
    // direction uses to keep the suffix.
    CHECK(fixture.statusMessage.find("Rename Widget to Gadget,") != std::string::npos);
}

TEST_CASE("Renaming a file offers to rename the type it was named after", "[BufferView][ClassFileSync]") {
    const ClassFileSyncGuard sync(true);
    const RenameReviewGuard  direct(false);
    Fixture                  fixture("Widget.php", "<?php\nclass Widget {\n}\n");
    BufferView               view = fixture.View();

    // C-c C-n: rename-file, source prompt then destination prompt.
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 20});
    view.OnEvent(test::Ctrl('c'));
    view.OnEvent(test::Ctrl('n'));
    Type(view, (fixture.dir / "Widget.php").string());
    view.OnEvent(test::Return());
    Type(view, (fixture.dir / "Gadget.php").string());
    view.OnEvent(test::Return());

    INFO("status: " << fixture.statusMessage);
    REQUIRE(std::filesystem::exists(fixture.dir / "Gadget.php"));
    REQUIRE(fixture.statusMessage.find("Rename Widget to Gadget as well?") != std::string::npos);

    view.OnEvent(test::Character("y"));
    CHECK(Content(*fixture.opened).find("class Gadget") != std::string::npos);
}

TEST_CASE("Renaming a file whose type it was never named after offers nothing",
          "[BufferView][ClassFileSync]") {
    const ClassFileSyncGuard sync(true);
    const RenameReviewGuard  direct(false);
    Fixture                  fixture("Helpers.php", "<?php\nclass Widget {\n}\n");
    BufferView               view = fixture.View();

    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 20});
    view.OnEvent(test::Ctrl('c'));
    view.OnEvent(test::Ctrl('n'));
    Type(view, (fixture.dir / "Helpers.php").string());
    view.OnEvent(test::Return());
    Type(view, (fixture.dir / "Utilities.php").string());
    view.OnEvent(test::Return());

    REQUIRE(std::filesystem::exists(fixture.dir / "Utilities.php"));
    CHECK(fixture.statusMessage.find("as well?") == std::string::npos);
    CHECK(Content(*fixture.opened).find("class Widget") != std::string::npos);
}

TEST_CASE("A pure directory move offers no type rename", "[BufferView][ClassFileSync]") {
    const ClassFileSyncGuard sync(true);
    Fixture                  fixture("Widget.php", "<?php\nclass Widget {\n}\n");
    std::filesystem::create_directories(fixture.dir / "sub");
    BufferView view = fixture.View();

    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 79, .y_min = 0, .y_max = 20});
    view.OnEvent(test::Ctrl('c'));
    view.OnEvent(test::Ctrl('n'));
    Type(view, (fixture.dir / "Widget.php").string());
    view.OnEvent(test::Return());
    Type(view, (fixture.dir / "sub" / "Widget.php").string());
    view.OnEvent(test::Return());

    REQUIRE(std::filesystem::exists(fixture.dir / "sub" / "Widget.php"));
    // The stem did not change, so there is nothing to rename.
    CHECK(fixture.statusMessage.find("as well?") == std::string::npos);
}
