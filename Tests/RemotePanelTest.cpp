//
// The Remote panel against ned_fake_ssh and the real agent: roots opened
// and listed as they're expanded, connected hosts' homes, and the keys
// that act on the selected row.
//

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "Editor/PanelMode.h"
#include "Editor/PostQueue.h"
#include "Editor/Remote/Hosts.h"
#include "EnvOverride.h"
#include "FakeRemoteHost.h"
#include "FileIconsOff.h"
#include "TestEvents.h"
#include "UI/RemotePanel.h"
#include "UI/Theme.h"
#include "UI/Widget.h"

using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::StartsWith;
using ned::tests::PumpUntil;
namespace test = ned::ui::test;

namespace {

constexpr int kWidth  = 60;
constexpr int kHeight = 16;

struct Fixture {
    ned::tests::FakeRemoteHost    fake;
    ned::tests::ScopedEnvOverride home{"HOME", (fake.dir / "home").c_str()};
    ned::editor::PostQueue        queue;
    ned::editor::remote::Hosts    hosts{queue, [this](const std::string& destination) { return fake.Options(destination); },
                                        ned::tests::FakeResolver(fake.dir)};
    ned::ui::Theme                theme = ned::ui::DarkTheme();
    ned::ui::RemotePanel          panel{theme, hosts};
    ned::ui::Screen               screen{kWidth, kHeight};

    using FileAction = ned::ui::RemotePanel::FileAction;
    std::vector<ned::text::RemoteFile>                         opened;
    std::vector<std::pair<FileAction, ned::text::RemoteFile>> actions;
    int                                openRootRequests = 0;
    int                                rootsChanged     = 0;
    int                                                        listedChanges    = 0;
    std::string                        message;

    Fixture() {
        panel.Tree().SetBox_(ned::ui::Box{.x_min = 0, .x_max = kWidth - 1, .y_min = 0, .y_max = kHeight - 1});
        panel.Tree().SetDrawBorder(false); // as the dock hosts it
        panel.Tree().TakeFocus();
        hosts.SetOnChanged([this](const std::string&) { panel.Refresh(); });
        panel.SetOnOpenFile([this](const ned::text::RemoteFile& file) { opened.push_back(file); });
        panel.SetOnRequestOpenRoot([this] { ++openRootRequests; });
        panel.SetOnRootsChanged([this] { ++rootsChanged; });
        panel.SetOnMessage([this](std::string text) { message = std::move(text); });
        panel.SetOnFileAction([this](FileAction action, const ned::text::RemoteFile& target) { actions.emplace_back(action, target); });
        panel.SetOnListedChanged([this] { ++listedChanges; });
    }

    [[nodiscard]] std::string RowText(int y) {
        std::string text;
        for (int x = 0; x < kWidth; ++x) {
            text += screen.PixelAt(x, y).character;
        }
        while (!text.empty() && text.back() == ' ') {
            text.pop_back();
        }
        return text;
    }
    // The painted rows, model row 0 first.
    [[nodiscard]] std::vector<std::string> Rows() {
        panel.Tree().Paint(ned::ui::Canvas(screen, panel.Tree().Box_()));
        std::vector<std::string> rows;
        for (int y = 0; y < kHeight; ++y) {
            if (std::string text = RowText(y); !text.empty()) {
                rows.push_back(std::move(text));
            }
        }
        return rows;
    }
    [[nodiscard]] std::string AllRows() {
        std::string all;
        for (const std::string& row : Rows()) {
            all += row + "\n";
        }
        return all;
    }
    bool RowsSettle(std::size_t count) {
        return PumpUntil(queue, [&] { return Rows().size() == count && AllRows().find("…") == std::string::npos; });
    }
    void Select(const std::string& text) {
        const std::vector<std::string> rows   = Rows();
        const auto                     target = std::ranges::find_if(rows, [&](const std::string& row) { return row.find(text) != std::string::npos; });
        INFO(AllRows());
        REQUIRE(target != rows.end());
        const auto index   = static_cast<std::size_t>(target - rows.begin());
        auto       current = panel.Tree().SelectedRow().value_or(0);
        for (; current < index; ++current) {
            REQUIRE(panel.Tree().OnEvent(test::ArrowDown()));
        }
        for (; current > index; --current) {
            REQUIRE(panel.Tree().OnEvent(test::ArrowUp()));
        }
    }
};

} // namespace

TEST_CASE("The Remote panel lists an opened root as it's expanded, and opens its files", "[RemotePanel][Remote][HostAgent]") {
    Fixture                     fixture;
    const std::filesystem::path dir = fixture.fake.dir / "proj";
    std::filesystem::create_directories(dir / "sub" / "deep");
    std::ofstream(dir / "b.txt") << "b\n";
    std::ofstream(dir / "a.txt") << "a\n";
    std::ofstream(dir / "sub" / "c.txt") << "c\n";

    // Empty, it offers to open one.
    REQUIRE(fixture.Rows().size() == 1);
    CHECK_THAT(fixture.Rows()[0], ContainsSubstring("o opens host:/path"));
    fixture.panel.Tree().OnEvent(test::Return());
    CHECK(fixture.openRootRequests == 1);

    const std::string root = "host:" + dir.string();
    fixture.panel.OpenRoot(root);
    CHECK(fixture.rootsChanged == 1);
    REQUIRE(fixture.panel.Roots() == std::vector<ned::text::RemoteFile>{{.destination = "host", .path = dir}});
    REQUIRE(fixture.RowsSettle(4));
    std::vector<std::string> rows = fixture.Rows();
    CHECK_THAT(rows[0], ContainsSubstring("▾ ● " + root));
    // Directories first, then files, each sorted.
    CHECK_THAT(rows[1], ContainsSubstring("▸ \uf07b sub"));
    CHECK_THAT(rows[2], ContainsSubstring("\uf016 a.txt"));
    CHECK_THAT(rows[3], ContainsSubstring("b.txt"));

    const auto at = [](const std::filesystem::path& path) { return ned::text::RemoteFile{.destination = "host", .path = path}; };
    CHECK(fixture.panel.ListedDirectories() == std::vector{at(dir)});
    const int listedChanges = fixture.listedChanges;

    fixture.Select("sub");
    fixture.panel.Tree().OnEvent(test::ArrowRight());
    REQUIRE(fixture.RowsSettle(6));
    CHECK(fixture.panel.ListedDirectories() == std::vector{at(dir), at(dir / "sub")});
    CHECK(fixture.listedChanges == listedChanges + 1);
    rows = fixture.Rows();
    CHECK_THAT(rows[1], ContainsSubstring("▾ \uf07c sub"));
    CHECK_THAT(rows[2], ContainsSubstring("▸ \uf07b deep"));
    CHECK_THAT(rows[3], ContainsSubstring("c.txt"));

    fixture.Select("c.txt");
    fixture.panel.Tree().OnEvent(test::Return());
    CHECK(fixture.opened == std::vector<ned::text::RemoteFile>{{.destination = "host", .path = dir / "sub" / "c.txt"}});

    // An empty directory has nothing to expand once listed.
    fixture.Select("deep");
    fixture.panel.Tree().OnEvent(test::Return());
    REQUIRE(PumpUntil(fixture.queue, [&] { return fixture.AllRows().find("…") == std::string::npos && fixture.Rows()[2].find("▸") == std::string::npos; }));

    // g lists the selected directory again.
    std::ofstream(dir / "sub" / "new.txt") << "n\n";
    fixture.Select("sub");
    fixture.panel.Tree().OnEvent(test::Character("g"));
    REQUIRE(fixture.RowsSettle(7));
    CHECK_THAT(fixture.AllRows(), ContainsSubstring("new.txt"));

    // Left collapses, and the selection keeps its row across a refresh.
    // What's collapsed, and everything under it, isn't watched.
    fixture.panel.Tree().OnEvent(test::ArrowLeft());
    REQUIRE(fixture.Rows().size() == 4);
    CHECK(fixture.panel.ListedDirectories() == std::vector{at(dir)});
    fixture.panel.Refresh();
    CHECK(fixture.panel.Tree().SelectedRow() == 1u);

    // d disconnects the selected row's host; the root stays.
    fixture.panel.Tree().OnEvent(test::Character("d"));
    CHECK(fixture.message == "Disconnected from host");
    REQUIRE(PumpUntil(fixture.queue, [&] { return fixture.Rows()[0].find("○") != std::string::npos; }));

    // x removes the root.
    fixture.Select(root);
    fixture.panel.Tree().OnEvent(test::Character("x"));
    CHECK(fixture.panel.Roots().empty());
    CHECK(fixture.rootsChanged == 2);
    CHECK_THAT(fixture.Rows()[0], ContainsSubstring("o opens host:/path"));
}

TEST_CASE("The Remote panel's file keys name the selected row, and a change relists its directory", "[RemotePanel][Remote][HostAgent]") {
    const FileIconsOff noIcons;
    using FileAction            = Fixture::FileAction;
    Fixture                     fixture;
    const std::filesystem::path dir = fixture.fake.dir / "proj";
    std::filesystem::create_directories(dir / "sub" / "deep");
    std::ofstream(dir / "a.txt") << "a\n";
    std::ofstream(dir / "sub" / "c.txt") << "c\n";
    const auto at = [](const std::filesystem::path& path) { return ned::text::RemoteFile{.destination = "host", .path = path}; };

    fixture.panel.OpenRoot("host:" + dir.string());
    REQUIRE(fixture.RowsSettle(3));
    fixture.Select("sub");
    fixture.panel.Tree().OnEvent(test::Return());
    REQUIRE(fixture.RowsSettle(5));
    fixture.Select("deep");
    fixture.panel.Tree().OnEvent(test::Return());
    REQUIRE(fixture.RowsSettle(5));

    // A new file or directory goes in a selected directory, or beside a file.
    fixture.Select("c.txt");
    for (const char* key : {"n", "+", "R", "D"}) {
        fixture.panel.Tree().OnEvent(test::Character(key));
    }
    fixture.Select("deep");
    fixture.panel.Tree().OnEvent(test::Character("+"));
    CHECK(fixture.actions == std::vector<std::pair<FileAction, ned::text::RemoteFile>>{{FileAction::NewFile, at(dir / "sub")},
                                                                                       {FileAction::NewDirectory, at(dir / "sub")},
                                                                                       {FileAction::Rename, at(dir / "sub" / "c.txt")},
                                                                                       {FileAction::Delete, at(dir / "sub" / "c.txt")},
                                                                                       {FileAction::NewDirectory, at(dir / "sub" / "deep")}});

    // A root is removed with x, never renamed or deleted.
    fixture.actions.clear();
    fixture.Select("host:" + dir.string());
    fixture.panel.Tree().OnEvent(test::Character("D"));
    CHECK(fixture.actions.empty());
    CHECK(fixture.message == "A root is removed from the panel, not renamed or deleted from here");

    // The keys are remote-panel-mode's, so init.janet's rebinding reaches them.
    {
        struct Guard {
            ~Guard() {
                ned::editor::ResetPanelKeysForTesting();
            }
        } guard;
        ned::editor::DefinePanelKey("remote-panel-mode", "r", "remote-panel-rename");
        ned::editor::DefinePanelKey("remote-panel-mode", "R", "");
        fixture.Select("c.txt");
        fixture.panel.Tree().OnEvent(test::Character("R"));
        CHECK(fixture.actions.empty());
        CHECK(fixture.message == "R is undefined in Remote panel");
        fixture.panel.Tree().OnEvent(test::Character("r"));
        CHECK(fixture.actions == std::vector<std::pair<FileAction, ned::text::RemoteFile>>{{FileAction::Rename, at(dir / "sub" / "c.txt")}});
    }
    fixture.actions.clear();

    // A new file under sub relists it; deep stays expanded and the
    // selection stays on c.txt.
    fixture.Select("c.txt");
    std::ofstream(dir / "sub" / "b.txt") << "b\n";
    fixture.panel.Changed(at(dir / "sub" / "b.txt"));
    REQUIRE(fixture.RowsSettle(6));
    std::vector<std::string> rows = fixture.Rows();
    CHECK_THAT(rows[2], ContainsSubstring("deep"));
    CHECK(rows[2].find("▸") == std::string::npos);
    CHECK_THAT(rows[3], ContainsSubstring("b.txt"));
    CHECK(fixture.panel.Tree().SelectedRow() == 4u);

    // A directory made several levels down shows in its nearest listed ancestor.
    std::filesystem::create_directories(dir / "x" / "y");
    fixture.panel.Changed(at(dir / "x" / "y"));
    REQUIRE(fixture.RowsSettle(7));
    CHECK_THAT(fixture.AllRows(), ContainsSubstring("▸ x"));
}

TEST_CASE("The Remote panel hides and shows dotfiles with '.'", "[RemotePanel][Remote][HostAgent]") {
    Fixture                     fixture;
    const std::filesystem::path dir = fixture.fake.dir / "proj";
    std::filesystem::create_directories(dir / ".cache");
    std::filesystem::create_directories(dir / "only");
    std::ofstream(dir / "only" / ".x") << "x\n";
    std::ofstream(dir / ".rc") << "r\n";
    std::ofstream(dir / "a.txt") << "a\n";

    fixture.panel.OpenRoot("host:" + dir.string());
    REQUIRE(fixture.RowsSettle(5));
    fixture.Select("only");
    fixture.panel.Tree().OnEvent(test::Return());
    REQUIRE(fixture.RowsSettle(6));
    CHECK_THAT(fixture.AllRows(), ContainsSubstring(".cache") && ContainsSubstring(".rc") && ContainsSubstring(".x"));

    // Hiding them leaves the selection on its nearest shown ancestor, and a
    // directory holding only dotfiles has nothing to expand.
    fixture.Select(".x");
    fixture.panel.Tree().OnEvent(test::Character("."));
    CHECK(fixture.message == "Hiding dotfiles");
    const std::vector<std::string> rows = fixture.Rows();
    REQUIRE(rows.size() == 3);
    CHECK_THAT(rows[1], ContainsSubstring("only"));
    CHECK(rows[1].find("▾") == std::string::npos);
    CHECK_THAT(rows[2], ContainsSubstring("a.txt"));
    CHECK(fixture.panel.Tree().SelectedRow() == 1u);

    fixture.panel.Tree().OnEvent(test::Character("."));
    CHECK(fixture.message == "Showing dotfiles");
    CHECK(fixture.Rows().size() == 6);
}

TEST_CASE("The Remote panel shows a connected host's home and says why a directory won't list", "[RemotePanel][Remote][HostAgent]") {
    Fixture fixture;
    bool    connected = false;
    fixture.hosts.Connect("host", [&](const auto& connection) { connected = connection.has_value(); });
    REQUIRE(PumpUntil(fixture.queue, [&] { return connected && fixture.Rows().size() == 1 && fixture.Rows()[0].find("host:~") != std::string::npos; }));
    CHECK_THAT(fixture.Rows()[0], ContainsSubstring("●"));
    fixture.hosts.Disconnect("host");
    REQUIRE(PumpUntil(fixture.queue, [&] { return fixture.Rows()[0].find("o opens host:/path") != std::string::npos; }));

    fixture.panel.OpenRoot("host:" + (fixture.fake.dir / "missing").string());
    REQUIRE(PumpUntil(fixture.queue, [&] { return fixture.AllRows().find("no such directory") != std::string::npos; }));
    CHECK_THAT(fixture.Rows()[0], ContainsSubstring("▸"));
    CHECK(fixture.message == "host: no such directory: " + (fixture.fake.dir / "missing").string());

    // A bare host opens its login's home as a root.
    fixture.panel.OpenRoot("host");
    REQUIRE(PumpUntil(fixture.queue, [&] { return fixture.panel.Roots().size() == 2; }));
    CHECK(fixture.panel.Roots()[1] == ned::text::RemoteFile{.destination = "host", .path = std::filesystem::current_path()});
}

TEST_CASE("The Remote panel says why a root's host won't connect", "[RemotePanel][Remote][HostAgent]") {
    Fixture fixture;
    fixture.panel.OpenRoot("denied:/srv");
    REQUIRE(PumpUntil(fixture.queue, [&] { return !fixture.message.empty(); }));
    CHECK_THAT(fixture.message, StartsWith("Couldn't connect to denied: "));
    CHECK(fixture.message.size() > std::string("Couldn't connect to denied: ").size());
    REQUIRE(fixture.Rows().size() == 1);
    CHECK_THAT(fixture.Rows()[0], ContainsSubstring("denied:/srv"));
    CHECK_THAT(fixture.Rows()[0], ContainsSubstring("failed"));
}
