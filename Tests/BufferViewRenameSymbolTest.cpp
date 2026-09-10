//
// rename-symbol end to end: the tiered command (Editor/Commands.cpp) through
// BufferView's own prompt and the in-buffer rewrite. The resolver's logic is
// covered in LocalScopesTest.cpp and each language's query in
// ModeLocalsTest.cpp; what this file pins is the wiring between them -- that
// C-c C-M-r resolves without any language server wired in at all, that Enter
// rewrites exactly the resolved occurrences as ONE undo step, and that the
// cases the scope-aware tier must refuse really do refuse.
//

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>

#include "Editor/Commands.h"
#include "Editor/Dispatcher.h"
#include "Editor/Keymap.h"
#include "Editor/Mode.h"
#include "Editor/ModeOverrides.h"
#include "Editor/PromptHistory.h"
#include "Editor/Register.h"
#include "TestEvents.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"
#include "UI/ActiveBuffer.h"
#include "UI/BufferView.h"
#include "UI/Theme.h"

using ned::ui::BufferView;
namespace test = ned::ui::test;

namespace {

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

    explicit Fixture(const std::string& modeName, const std::string& text) {
        if (modeName != "fundamental-mode") {
            const std::optional<ned::editor::Mode> resolved = ned::editor::ModeByName(modeName);
            REQUIRE(resolved.has_value());
            mode = *resolved;
        }
        buffer.InsertAtPoint(text);
    }

    BufferView View() {
        return BufferView(activeBuffer, killRing, registers, promptHistory, bufferList, dispatcher, statusMessage,
                          mode, theme);
    }
};

// C-c C-M-r, the rename-symbol binding.
void PressRename(BufferView& view) {
    view.OnEvent(test::Ctrl('c'));
    view.OnEvent(test::CtrlAlt('r'));
}

void Type(BufferView& view, const std::string& text) {
    for (const char ch : text) {
        view.OnEvent(test::Character(ch));
    }
}

std::string Content(const ned::text::Buffer& buffer) {
    return buffer.Content().Substring(0, buffer.Content().ByteLength());
}

} // namespace

TEST_CASE("rename-symbol renames a local with no language server wired in", "[BufferView][RenameSymbol]") {
    const std::string source = "int size;\n"
                               "void f(int size) {\n"
                               "    int doubled = size + size;\n"
                               "}\n";
    Fixture           fixture("c-mode", source);
    BufferView        view = fixture.View();
    fixture.buffer.SetPoint(source.find("int size)") + 4); // on the parameter

    PressRename(view);
    REQUIRE(fixture.statusMessage.find("New name:") != std::string::npos);
    // The prompt is prefilled with the current name, so a rename is an edit
    // of what is there rather than retyping it.
    REQUIRE(fixture.statusMessage.find("size") != std::string::npos);

    for (int i = 0; i < 4; ++i) {
        view.OnEvent(test::Backspace());
    }
    Type(view, "width");
    view.OnEvent(test::Return());

    REQUIRE(Content(fixture.buffer) == "int size;\n"
                                       "void f(int width) {\n"
                                       "    int doubled = width + width;\n"
                                       "}\n");
    REQUIRE(fixture.statusMessage.find("3 occurrences") != std::string::npos);
    REQUIRE(fixture.statusMessage.find("parameter") != std::string::npos);
}

TEST_CASE("rename-symbol's rewrite is a single undo step", "[BufferView][RenameSymbol]") {
    const std::string source = "void f(int size) { return size + size; }\n";
    Fixture           fixture("c-mode", source);
    BufferView        view = fixture.View();
    fixture.buffer.SetPoint(source.find("int size") + 4);

    PressRename(view);
    for (int i = 0; i < 4; ++i) {
        view.OnEvent(test::Backspace());
    }
    Type(view, "n");
    view.OnEvent(test::Return());
    REQUIRE(Content(fixture.buffer) == "void f(int n) { return n + n; }\n");

    fixture.buffer.Undo();
    REQUIRE(Content(fixture.buffer) == source); // one step, not three
}

TEST_CASE("rename-symbol leaves a shadowing binding alone", "[BufferView][RenameSymbol]") {
    const std::string source = "void f(void) {\n"
                               "    int value = 1;\n"
                               "    {\n"
                               "        int value = 2;\n"
                               "        use(value);\n"
                               "    }\n"
                               "    use(value);\n"
                               "}\n";
    Fixture           fixture("c-mode", source);
    BufferView        view = fixture.View();
    fixture.buffer.SetPoint(source.find("int value = 2") + 4); // the inner one

    PressRename(view);
    for (int i = 0; i < 5; ++i) {
        view.OnEvent(test::Backspace());
    }
    Type(view, "inner");
    view.OnEvent(test::Return());

    REQUIRE(Content(fixture.buffer) == "void f(void) {\n"
                                       "    int value = 1;\n"
                                       "    {\n"
                                       "        int inner = 2;\n"
                                       "        use(inner);\n"
                                       "    }\n"
                                       "    use(value);\n"
                                       "}\n");
}

TEST_CASE("rename-symbol declines a file-level binding rather than renaming it locally",
          "[BufferView][RenameSymbol]") {
    // A file-level name can be referenced from another translation unit, so
    // the scope-aware tier must hand off. With no LSP manager wired in there
    // is nothing to hand off TO, so the LSP tier's own unprefilled prompt is
    // what comes up -- and the buffer stays untouched.
    const std::string source = "int shared;\nvoid f(void) { shared = 1; }\n";
    Fixture           fixture("c-mode", source);
    BufferView        view = fixture.View();
    fixture.buffer.SetPoint(source.find("int shared") + 4);

    PressRename(view);
    REQUIRE(fixture.statusMessage.find("New name:") != std::string::npos);
    REQUIRE(fixture.statusMessage.find("shared") == std::string::npos); // not prefilled -- this is the LSP prompt

    Type(view, "renamed");
    view.OnEvent(test::Return());
    REQUIRE(Content(fixture.buffer) == source);
}

TEST_CASE("rename-symbol falls back for a mode with no locals query", "[BufferView][RenameSymbol]") {
    const std::string source = "{ \"key\": 1 }\n";
    Fixture           fixture("json-mode", source);
    BufferView        view = fixture.View();
    REQUIRE_FALSE(static_cast<bool>(fixture.mode.localScopes));
    fixture.buffer.SetPoint(source.find("key"));

    PressRename(view);
    REQUIRE(fixture.statusMessage.find("New name:") != std::string::npos);

    Type(view, "other");
    view.OnEvent(test::Return());
    REQUIRE(Content(fixture.buffer) == source);
}

TEST_CASE("rename-symbol to the same name or an empty one changes nothing", "[BufferView][RenameSymbol]") {
    const std::string source = "void f(int size) { return size; }\n";

    SECTION("unchanged name") {
        Fixture    fixture("c-mode", source);
        BufferView view = fixture.View();
        fixture.buffer.SetPoint(source.find("int size") + 4);
        PressRename(view);
        view.OnEvent(test::Return());
        REQUIRE(Content(fixture.buffer) == source);
    }
    SECTION("emptied name") {
        Fixture    fixture("c-mode", source);
        BufferView view = fixture.View();
        fixture.buffer.SetPoint(source.find("int size") + 4);
        PressRename(view);
        for (int i = 0; i < 4; ++i) {
            view.OnEvent(test::Backspace());
        }
        view.OnEvent(test::Return());
        REQUIRE(Content(fixture.buffer) == source);
    }
}

TEST_CASE("rename-symbol abandoned with Escape leaves the buffer alone", "[BufferView][RenameSymbol]") {
    const std::string source = "void f(int size) { return size; }\n";
    Fixture           fixture("c-mode", source);
    BufferView        view = fixture.View();
    fixture.buffer.SetPoint(source.find("int size") + 4);

    PressRename(view);
    for (int i = 0; i < 4; ++i) {
        view.OnEvent(test::Backspace());
    }
    Type(view, "width");
    view.OnEvent(test::Escape());
    REQUIRE(Content(fixture.buffer) == source);
}

TEST_CASE("rename-symbol renames a Python local across block boundaries", "[BufferView][RenameSymbol]") {
    const std::string source = "def f(count):\n"
                               "    if count:\n"
                               "        local = count\n"
                               "    else:\n"
                               "        local = 0\n"
                               "    return local\n";
    Fixture           fixture("python-mode", source);
    BufferView        view = fixture.View();
    fixture.buffer.SetPoint(source.find("local"));

    PressRename(view);
    for (int i = 0; i < 5; ++i) {
        view.OnEvent(test::Backspace());
    }
    Type(view, "value");
    view.OnEvent(test::Return());

    REQUIRE(Content(fixture.buffer) == "def f(count):\n"
                                       "    if count:\n"
                                       "        value = count\n"
                                       "    else:\n"
                                       "        value = 0\n"
                                       "    return value\n");
}

TEST_CASE("rename-symbol refuses on a read-only buffer", "[BufferView][RenameSymbol]") {
    const std::string source = "void f(int size) { return size; }\n";
    Fixture           fixture("c-mode", source);
    BufferView        view = fixture.View();
    fixture.buffer.SetPoint(source.find("int size") + 4);

    PressRename(view);
    for (int i = 0; i < 4; ++i) {
        view.OnEvent(test::Backspace());
    }
    Type(view, "width");
    fixture.buffer.SetReadOnly(true);
    view.OnEvent(test::Return());

    REQUIRE(Content(fixture.buffer) == source);
    REQUIRE(fixture.statusMessage.find("read-only") != std::string::npos);
}
