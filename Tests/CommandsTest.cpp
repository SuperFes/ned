#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string_view>

#include "Editor/Commands.h"
#include "Editor/Dispatcher.h"
#include "Editor/FormatOnSave.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"

using namespace ned::editor;

namespace {

// FormatCommand is process-wide state (see FormatOnSave.h); every test that
// sets one must leave it unset for the next test, guaranteed via RAII.
struct FormatCommandGuard {
    ~FormatCommandGuard() {
        SetFormatCommand(std::nullopt);
    }
};

struct Fixture {
    ned::text::Buffer     buffer{"scratch"};
    ned::text::KillRing   killRing;
    ned::text::BufferList bufferList;

    CommandContext Context() {
        return CommandContext{buffer, killRing, bufferList};
    }
};

void Type(Dispatcher& dispatcher, CommandContext& context, std::string_view text) {
    for (const char c : text) {
        KeyChord chord;
        chord.Codepoint = static_cast<char32_t>(static_cast<unsigned char>(c));
        REQUIRE(dispatcher.Feed(chord, context) == Dispatcher::Outcome::Invoked);
    }
}

} // namespace

TEST_CASE("Default keymap + builtin commands support basic editing end to end", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);
    Keymap     keymap = BuildDefaultGlobalKeymap();
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();

    Type(dispatcher, context, "hello");
    REQUIRE(fixture.buffer.Text() == "hello");
    REQUIRE(fixture.buffer.Point() == 5);

    REQUIRE(dispatcher.Feed(ParseKeyChord("C-a"), context) == Dispatcher::Outcome::Invoked); // beginning-of-line
    REQUIRE(fixture.buffer.Point() == 0);

    dispatcher.Feed(ParseKeyChord("C-k"), context); // kill-line: kills "hello"
    REQUIRE(fixture.buffer.Text().empty());

    dispatcher.Feed(ParseKeyChord("C-y"), context); // yank it back
    REQUIRE(fixture.buffer.Text() == "hello");

    dispatcher.Feed(ParseKeyChord("C-/"), context); // undo the yank
    REQUIRE(fixture.buffer.Text().empty());

    dispatcher.Feed(ParseKeyChord("C-/"), context); // undo the kill
    REQUIRE(fixture.buffer.Text() == "hello");
}

TEST_CASE("RET inserts a newline via the newline command", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);
    Keymap     keymap = BuildDefaultGlobalKeymap();
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();

    Type(dispatcher, context, "ab");
    dispatcher.Feed(ParseKeyChord("RET"), context);
    Type(dispatcher, context, "cd");

    REQUIRE(fixture.buffer.Text() == "ab\ncd");
}

TEST_CASE("Arrow-key bindings move point without inserting", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);
    Keymap     keymap = BuildDefaultGlobalKeymap();
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();

    Type(dispatcher, context, "abc");
    dispatcher.Feed(ParseKeyChord("LEFT"), context);
    dispatcher.Feed(ParseKeyChord("LEFT"), context);
    REQUIRE(fixture.buffer.Point() == 1);

    dispatcher.Feed(ParseKeyChord("RIGHT"), context);
    REQUIRE(fixture.buffer.Point() == 2);
    REQUIRE(fixture.buffer.Text() == "abc");
}

TEST_CASE("backward-delete-char via DEL removes the previous grapheme cluster", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);
    Keymap     keymap = BuildDefaultGlobalKeymap();
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();

    Type(dispatcher, context, "abc");
    dispatcher.Feed(ParseKeyChord("DEL"), context);

    REQUIRE(fixture.buffer.Text() == "ab");
}

TEST_CASE("kill-line at end of line kills the newline, joining with the next line", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    ned::text::Buffer     buffer("scratch", ned::text::Rope("hi\nbye"));
    ned::text::KillRing   killRing;
    ned::text::BufferList bufferList;
    CommandContext        context{buffer, killRing, bufferList};

    buffer.SetPoint(2); // end of "hi", right before the newline
    registry.Invoke("kill-line", context);

    REQUIRE(buffer.Text() == "hibye");
}

TEST_CASE("save-buffer writes the file and reports a confirmation message", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_commands_test_save.txt";
    std::filesystem::remove(path);

    ned::text::Buffer buffer("scratch", ned::text::Rope("hello"));
    buffer.SaveToFile(path); // establishes the associated path without writing via the command

    ned::text::KillRing   killRing;
    ned::text::BufferList bufferList;
    std::string           message;
    CommandContext        context{buffer, killRing, bufferList, KeyChord{}, &message};

    registry.Invoke("save-buffer", context);

    REQUIRE(std::filesystem::exists(path));
    REQUIRE(message.find(buffer.Name()) != std::string::npos);

    std::filesystem::remove(path);
}

TEST_CASE("save-buffer reports an error message instead of throwing when there's no associated file", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    ned::text::Buffer     buffer("scratch", ned::text::Rope("hello"));
    ned::text::KillRing   killRing;
    ned::text::BufferList bufferList;
    std::string           message;
    CommandContext        context{buffer, killRing, bufferList, KeyChord{}, &message};

    registry.Invoke("save-buffer", context); // must not throw

    REQUIRE_FALSE(message.empty());
}

TEST_CASE("save-buffer formats the buffer through the configured command before writing", "[Commands]") {
    const FormatCommandGuard guard;
    SetFormatCommand(std::string("tr 'a-z' 'A-Z'"));

    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_commands_test_format.txt";
    std::filesystem::remove(path);

    ned::text::Buffer buffer("scratch", ned::text::Rope("hello"));
    buffer.SaveToFile(path);

    ned::text::KillRing   killRing;
    ned::text::BufferList bufferList;
    std::string           message;
    CommandContext        context{buffer, killRing, bufferList, KeyChord{}, &message};

    registry.Invoke("save-buffer", context);

    REQUIRE(buffer.Text() == "HELLO");
    REQUIRE(message.find("(format command failed)") == std::string::npos);

    std::ifstream file(path);
    std::string   written((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    REQUIRE(written == "HELLO");

    std::filesystem::remove(path);
}

TEST_CASE("save-buffer still saves the original content and reports failure when the formatter fails",
          "[Commands]") {
    const FormatCommandGuard guard;
    SetFormatCommand(std::string("false")); // always exits non-zero

    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "ned_commands_test_format_fail.txt";
    std::filesystem::remove(path);

    ned::text::Buffer buffer("scratch", ned::text::Rope("hello"));
    buffer.SaveToFile(path);

    ned::text::KillRing   killRing;
    ned::text::BufferList bufferList;
    std::string           message;
    CommandContext        context{buffer, killRing, bufferList, KeyChord{}, &message};

    registry.Invoke("save-buffer", context);

    REQUIRE(buffer.Text() == "hello"); // unchanged -- the failed formatter's output is never applied
    REQUIRE(message.find("(format command failed)") != std::string::npos);

    std::ifstream file(path);
    std::string   written((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    REQUIRE(written == "hello");

    std::filesystem::remove(path);
}

TEST_CASE("save-buffer does not touch the buffer when no format command is configured", "[Commands]") {
    const FormatCommandGuard guard; // ensures it's unset, even if a prior test in this run left one behind

    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_commands_test_no_format.txt";
    std::filesystem::remove(path);

    ned::text::Buffer buffer("scratch", ned::text::Rope("hello"));
    buffer.SaveToFile(path);

    ned::text::KillRing   killRing;
    ned::text::BufferList bufferList;
    std::string           message;
    CommandContext        context{buffer, killRing, bufferList, KeyChord{}, &message};

    registry.Invoke("save-buffer", context);

    REQUIRE(buffer.Text() == "hello");
    REQUIRE(message.find("(format command failed)") == std::string::npos);

    std::filesystem::remove(path);
}

TEST_CASE("C-x C-s is bound to save-buffer in the default keymap", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);
    Keymap     keymap = BuildDefaultGlobalKeymap();
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();

    REQUIRE(dispatcher.Feed(ParseKeyChord("C-x"), context) == Dispatcher::Outcome::Pending);
    REQUIRE(dispatcher.Feed(ParseKeyChord("C-s"), context) == Dispatcher::Outcome::Invoked);
}

TEST_CASE("isearch/query-replace commands set interactiveRequest, not the buffer", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);
    Keymap     keymap = BuildDefaultGlobalKeymap();
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();

    REQUIRE(dispatcher.Feed(ParseKeyChord("C-s"), context) == Dispatcher::Outcome::Invoked);
    REQUIRE(context.interactiveRequest == InteractiveRequest::IsearchForward);
    REQUIRE(fixture.buffer.Text().empty()); // the command itself doesn't touch the buffer

    context.interactiveRequest = InteractiveRequest::None;
    REQUIRE(dispatcher.Feed(ParseKeyChord("C-r"), context) == Dispatcher::Outcome::Invoked);
    REQUIRE(context.interactiveRequest == InteractiveRequest::IsearchBackward);

    context.interactiveRequest = InteractiveRequest::None;
    REQUIRE(dispatcher.Feed(ParseKeyChord("ESC"), context) == Dispatcher::Outcome::Pending);
    REQUIRE(dispatcher.Feed(ParseKeyChord("%"), context) == Dispatcher::Outcome::Invoked);
    REQUIRE(context.interactiveRequest == InteractiveRequest::QueryReplace);
}

TEST_CASE("quit sets CommandContext::quit, and C-x C-c is bound to it", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);
    Keymap     keymap = BuildDefaultGlobalKeymap();
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();
    REQUIRE_FALSE(context.quit);

    REQUIRE(dispatcher.Feed(ParseKeyChord("C-x"), context) == Dispatcher::Outcome::Pending);
    REQUIRE(dispatcher.Feed(ParseKeyChord("C-c"), context) == Dispatcher::Outcome::Invoked);
    REQUIRE(context.quit);
}

TEST_CASE("quit requests confirmation instead of quitting when a buffer has unsaved changes", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    // fixture.buffer is a standalone Buffer, not registered in fixture.bufferList
    // -- quit checks context.bufferList, so the modified buffer needs to actually
    // be a member of it.
    Fixture            fixture;
    ned::text::Buffer& buffer = fixture.bufferList.CreateBuffer("scratch");
    buffer.InsertAtPoint("unsaved edit");
    REQUIRE(buffer.Modified());

    CommandContext context{buffer, fixture.killRing, fixture.bufferList};
    registry.Invoke("quit", context);

    REQUIRE_FALSE(context.quit);
    REQUIRE(context.interactiveRequest == InteractiveRequest::ConfirmQuit);
}

TEST_CASE("self-insert-command is a no-op when triggered by a special key", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    ned::text::Buffer     buffer("scratch");
    ned::text::KillRing   killRing;
    ned::text::BufferList bufferList;
    CommandContext        context{buffer, killRing, bufferList};
    context.triggeringKey.Special = SpecialKey::Enter;

    registry.Invoke("self-insert-command", context);
    REQUIRE(buffer.Text().empty());
}

TEST_CASE("Up/Down and C-n/C-p bindings move point by line", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);
    Keymap     keymap = BuildDefaultGlobalKeymap();
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();

    fixture.buffer.InsertAtPoint("one\ntwo\nthree");
    fixture.buffer.SetPoint(1); // line 0, column 1

    dispatcher.Feed(ParseKeyChord("DOWN"), context);
    REQUIRE(fixture.buffer.Point() == 5); // line 1 ("two"), column 1

    dispatcher.Feed(ParseKeyChord("C-n"), context);
    REQUIRE(fixture.buffer.Point() == 9); // line 2 ("three"), column 1

    dispatcher.Feed(ParseKeyChord("UP"), context);
    REQUIRE(fixture.buffer.Point() == 5);

    dispatcher.Feed(ParseKeyChord("C-p"), context);
    REQUIRE(fixture.buffer.Point() == 1);
}

TEST_CASE("Home/End bindings move point to the start/end of the line", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);
    Keymap     keymap = BuildDefaultGlobalKeymap();
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();

    fixture.buffer.InsertAtPoint("hello world");
    fixture.buffer.SetPoint(5);

    dispatcher.Feed(ParseKeyChord("HOME"), context);
    REQUIRE(fixture.buffer.Point() == 0);

    dispatcher.Feed(ParseKeyChord("END"), context);
    REQUIRE(fixture.buffer.Point() == 11);
}

TEST_CASE("ESC f / ESC b bindings move point by word", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);
    Keymap     keymap = BuildDefaultGlobalKeymap();
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();

    fixture.buffer.InsertAtPoint("hello world");
    fixture.buffer.SetPoint(0);

    REQUIRE(dispatcher.Feed(ParseKeyChord("ESC"), context) == Dispatcher::Outcome::Pending);
    REQUIRE(dispatcher.Feed(ParseKeyChord("f"), context) == Dispatcher::Outcome::Invoked);
    REQUIRE(fixture.buffer.Point() == 5); // right after "hello"

    REQUIRE(dispatcher.Feed(ParseKeyChord("ESC"), context) == Dispatcher::Outcome::Pending);
    REQUIRE(dispatcher.Feed(ParseKeyChord("b"), context) == Dispatcher::Outcome::Invoked);
    REQUIRE(fixture.buffer.Point() == 0); // back to the start of "hello"
}

TEST_CASE("scroll-page-down/scroll-page-up move by a fraction of CommandContext::viewportHeight", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);
    Keymap     keymap = BuildDefaultGlobalKeymap();
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();

    std::string content;
    for (int i = 0; i < 20; ++i) {
        content += "line\n";
    }
    fixture.buffer.InsertAtPoint(content);
    fixture.buffer.SetPoint(0);

    context.viewportHeight = 10; // floor(10 * 0.65) -> 6 lines
    dispatcher.Feed(ParseKeyChord("PAGEDOWN"), context);
    REQUIRE(fixture.buffer.Content().ByteOffsetToLine(fixture.buffer.Point()) == 6);
    REQUIRE(fixture.buffer.Point() == 30); // column 0 on line 6 ("line\n" x 6 = 30 bytes)

    dispatcher.Feed(ParseKeyChord("PAGEUP"), context);
    REQUIRE(fixture.buffer.Point() == 0);
}

TEST_CASE("scroll-page-down is a small, sane fallback when CommandContext::viewportHeight is unset", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);
    Keymap     keymap = BuildDefaultGlobalKeymap();
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();

    fixture.buffer.InsertAtPoint("a\nb\nc\nd\ne\n");
    fixture.buffer.SetPoint(0);

    REQUIRE(context.viewportHeight == 0); // never set by this test's fixture
    dispatcher.Feed(ParseKeyChord("PAGEDOWN"), context);

    // PageLineCount clamps to at least 1 line rather than moving by 0.
    REQUIRE(fixture.buffer.Content().ByteOffsetToLine(fixture.buffer.Point()) == 1);
}
