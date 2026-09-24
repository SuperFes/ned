#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>
#include <vector>

#include "Editor/Commands.h"
#include "Editor/Dispatcher.h"
#include "Editor/Keymap.h"
#include "Editor/Mode.h"
#include "Editor/Project/Root.h"
#include "Editor/PromptHistory.h"
#include "Editor/Register.h"
#include "Editor/Vim/Settings.h"
#include "TestEvents.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"
#include "UI/ActiveBuffer.h"
#include "UI/BufferView.h"
#include "UI/Theme.h"

using ned::ui::BufferView;

namespace {

// Mirrors BufferViewDiffGutterTest.cpp's own Fixture shape.
struct Fixture {
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

// ModeEnabled is process-wide state (Editor/Vim/Settings.h) -- same guard shape as
// BufferViewTest.cpp's own VimModeGuard, restoring whatever was configured before the
// test ran (default false) rather than unconditionally disabling it.
class VimModeGuard {
  public:
    VimModeGuard() : previous_(ned::editor::vim::ModeEnabled()) {
        ned::editor::vim::SetModeEnabled(true);
    }
    ~VimModeGuard() {
        ned::editor::vim::SetModeEnabled(previous_);
    }
    VimModeGuard(const VimModeGuard&)            = delete;
    VimModeGuard& operator=(const VimModeGuard&) = delete;

  private:
    bool previous_;
};


std::filesystem::path TempFile(const std::string& name) {
    return std::filesystem::temp_directory_path() / ("ned_vim_save_" + name + "_" + std::to_string(::getpid()) + ".txt");
}

std::string ReadBytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void Type(BufferView& view, const std::string& keys) {
    for (const char c : keys) {
        view.OnEvent(c == '\n' ? ned::ui::test::Return() : ned::ui::test::Character(c));
    }
}

// Records window requests; reports "not the last window" so a close is a
// DeleteWindow request rather than quitting the app.
struct WindowSpy {
    std::vector<ned::editor::InteractiveRequest> requests;

    void Attach(BufferView& view) {
        view.SetIsOnlyWindowQuery([] { return false; });
        view.SetOnWindowRequest([this](ned::editor::InteractiveRequest request) { requests.push_back(request); });
    }
};

} // namespace

TEST_CASE(":w saves through save-buffer, with the buffer's conventions", "[BufferView][Vim]") {
    VimModeGuard                vimGuard;
    Fixture                     fixture;
    const std::filesystem::path path = TempFile("w");
    fixture.buffer.SetPath(path);
    fixture.buffer.InsertAtPoint("one\ntwo");
    fixture.buffer.SetConventions({.ensureFinalNewline = false, .lineEnding = ned::text::LineEnding::CRLF});
    BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 4});

    Type(view, ":w\n");
    CHECK(ReadBytes(path) == "one\r\ntwo");
    CHECK_FALSE(fixture.buffer.Modified());
    CHECK(fixture.statusMessage.starts_with("Wrote"));
    std::filesystem::remove(path);
}

TEST_CASE(":wq closes the window once the save lands", "[BufferView][Vim]") {
    VimModeGuard                vimGuard;
    Fixture                     fixture;
    const std::filesystem::path path = TempFile("wq");
    fixture.buffer.SetPath(path);
    fixture.buffer.InsertAtPoint("x\n");
    BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 4});
    WindowSpy spy;
    spy.Attach(view);

    Type(view, ":wq\n");
    CHECK(ReadBytes(path) == "x\n");
    REQUIRE(spy.requests.size() == 1);
    CHECK(spy.requests[0] == ned::editor::InteractiveRequest::DeleteWindow);
    std::filesystem::remove(path);
}

TEST_CASE(":wq and ZZ keep the window open when the save fails", "[BufferView][Vim]") {
    VimModeGuard vimGuard;
    Fixture      fixture;
    fixture.buffer.SetPath(std::filesystem::temp_directory_path() / "ned_vim_save_no_such_dir" / "x.txt");
    fixture.buffer.InsertAtPoint("x\n");
    BufferView view = fixture.View();
    view.SetBox_(ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 4});
    WindowSpy spy;
    spy.Attach(view);

    Type(view, ":wq\n");
    CHECK(spy.requests.empty());
    CHECK(fixture.buffer.Modified());

    Type(view, "ZZ");
    CHECK(spy.requests.empty());
}
