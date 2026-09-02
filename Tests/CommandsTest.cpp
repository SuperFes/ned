#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string_view>
#include <vector>

#include "Editor/AutoPair.h"
#include "Editor/Backup.h"
#include "Editor/Commands.h"
#include "Editor/Dispatcher.h"
#include "Editor/FormatOnSave.h"
#include "Editor/LineEndingPolicy.h"
#include "Editor/Mode.h"
#include "Editor/ProjectRoot.h"
#include "Editor/ProjectSession.h"
#include "Editor/SnippetRegistry.h"
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

// LineEndingPolicy is process-wide state (see Editor/LineEndingPolicy.h);
// every test that sets one must restore the default for the next test.
struct LineEndingPolicyGuard {
    ~LineEndingPolicyGuard() {
        SetLineEndingPolicy({LineEndingPolicyMode::Preserve, ned::text::LineEnding::LF});
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

    dispatcher.Feed(ParseKeyChord("C-_"), context); // undo the yank
    REQUIRE(fixture.buffer.Text().empty());

    dispatcher.Feed(ParseKeyChord("C-_"), context); // undo the kill
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

TEST_CASE("delete-char via DELETE at end of line joins it with the next line", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);
    Keymap     keymap = BuildDefaultGlobalKeymap();
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    ned::text::Buffer     buffer("scratch", ned::text::Rope("hi\nbye"));
    ned::text::KillRing   killRing;
    ned::text::BufferList bufferList;
    CommandContext        context{buffer, killRing, bufferList};

    buffer.SetPoint(2); // end of "hi", right before the newline
    dispatcher.Feed(ParseKeyChord("DELETE"), context);

    REQUIRE(buffer.Text() == "hibye");
    REQUIRE(buffer.Point() == 2);
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

// Emacs-keymap-round-2 follow-up (kill-append).

TEST_CASE("Consecutive kill-line calls append into one kill-ring entry", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);
    Keymap     keymap = BuildDefaultGlobalKeymap();
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();

    fixture.buffer.InsertAtPoint("hello\nworld\n");
    fixture.buffer.SetPoint(0);

    dispatcher.Feed(ParseKeyChord("C-k"), context); // kills "hello", buffer now "\nworld\n"
    dispatcher.Feed(ParseKeyChord("C-k"), context); // kills the newline -- second consecutive kill

    REQUIRE(fixture.killRing.Current() == "hello\n");
    REQUIRE(fixture.buffer.Text() == "world\n");
}

TEST_CASE("An intervening command breaks the kill-append chain", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);
    Keymap     keymap = BuildDefaultGlobalKeymap();
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();

    fixture.buffer.InsertAtPoint("hello\nworld\n");
    fixture.buffer.SetPoint(0);

    dispatcher.Feed(ParseKeyChord("C-k"), context); // kills "hello"
    dispatcher.Feed(ParseKeyChord("C-f"), context); // an ordinary motion command in between
    dispatcher.Feed(ParseKeyChord("C-k"), context);

    REQUIRE(fixture.killRing.Current() != "hello\n");
}

TEST_CASE("backward-kill-word prepends onto the current kill-ring entry", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);
    Keymap     keymap = BuildDefaultGlobalKeymap();
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();

    fixture.buffer.InsertAtPoint("foo bar");
    fixture.buffer.SetPoint(7); // buffer end

    dispatcher.Feed(ParseKeyChord("M-DEL"), context); // kills "bar"
    dispatcher.Feed(ParseKeyChord("M-DEL"), context); // kills "foo " -- prepends, since it's a backward kill

    REQUIRE(fixture.killRing.Current() == "foo bar");
}

TEST_CASE("kill-region prepends when the killed region sat before point", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    Fixture        fixture;
    CommandContext context = fixture.Context();

    fixture.killRing.Kill("X");
    context.lastCommand = "kill-line"; // simulate an immediately preceding kill

    fixture.buffer.InsertAtPoint("ab");
    fixture.buffer.SetMark(0);
    fixture.buffer.SetPoint(2); // point at region end -- region [0,2) sat *before* point

    registry.Invoke("kill-region", context);
    REQUIRE(fixture.killRing.Current() == "abX");
}

TEST_CASE("kill-region appends when the killed region sat after point", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    Fixture        fixture;
    CommandContext context = fixture.Context();

    fixture.killRing.Kill("X");
    context.lastCommand = "kill-line";

    fixture.buffer.InsertAtPoint("ab");
    fixture.buffer.SetPoint(0);
    fixture.buffer.SetMark(2); // point at region start -- region [0,2) sat *after* point

    registry.Invoke("kill-region", context);
    REQUIRE(fixture.killRing.Current() == "Xab");
}

TEST_CASE("set-mark-command sets the mark at point", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    Fixture        fixture;
    CommandContext context = fixture.Context();

    fixture.buffer.InsertAtPoint("hello");
    fixture.buffer.SetPoint(2);
    registry.Invoke("set-mark-command", context);

    REQUIRE(fixture.buffer.HasMark());
    REQUIRE(fixture.buffer.Mark() == 2);
}

TEST_CASE("Plain motion commands no longer clear an existing mark", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);
    Keymap     keymap = BuildDefaultGlobalKeymap();
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();

    Type(dispatcher, context, "hello world");
    fixture.buffer.SetPoint(0);
    dispatcher.Feed(ParseKeyChord("C-SPC"), context);
    REQUIRE(fixture.buffer.HasMark());

    dispatcher.Feed(ParseKeyChord("RIGHT"), context);
    dispatcher.Feed(ParseKeyChord("RIGHT"), context);
    dispatcher.Feed(ParseKeyChord("C-f"), context);
    REQUIRE(fixture.buffer.HasMark());
    REQUIRE(fixture.buffer.Mark() == 0);
    REQUIRE(fixture.buffer.Point() == 3);
}

// Regression: a mark set via C-SPC used to linger indefinitely through any
// unrelated edit once plain motion commands stopped clearing it (see the
// test above) -- since nothing else cleared it either, editing commands
// (typing, deleting, undo/redo, yank, tab) left a stale, ever-growing
// region highlighted in BufferView's gutter/selection rendering long after
// the edit that should have deactivated it. Each editing command below
// must ClearMark() even though it doesn't touch the region itself.
TEST_CASE("Editing commands clear a leftover mark, unlike plain motion", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    auto freshMarkedContext = [](Fixture& fixture) {
        fixture.buffer.InsertAtPoint("hello");
        fixture.buffer.SetMark(0);
    };

    {
        Fixture        fixture;
        CommandContext context = fixture.Context();
        freshMarkedContext(fixture);
        registry.Invoke("self-insert-command", context);
        REQUIRE_FALSE(fixture.buffer.HasMark());
    }
    {
        Fixture        fixture;
        CommandContext context = fixture.Context();
        freshMarkedContext(fixture);
        registry.Invoke("delete-char", context);
        REQUIRE_FALSE(fixture.buffer.HasMark());
    }
    {
        Fixture        fixture;
        CommandContext context = fixture.Context();
        freshMarkedContext(fixture);
        registry.Invoke("backward-delete-char", context);
        REQUIRE_FALSE(fixture.buffer.HasMark());
    }
    {
        Fixture        fixture;
        CommandContext context = fixture.Context();
        freshMarkedContext(fixture);
        registry.Invoke("kill-line", context);
        REQUIRE_FALSE(fixture.buffer.HasMark());
    }
    {
        Fixture        fixture;
        CommandContext context = fixture.Context();
        freshMarkedContext(fixture);
        registry.Invoke("yank", context);
        REQUIRE_FALSE(fixture.buffer.HasMark());
    }
    {
        Fixture        fixture;
        CommandContext context = fixture.Context();
        freshMarkedContext(fixture);
        registry.Invoke("undo", context);
        REQUIRE_FALSE(fixture.buffer.HasMark());
    }
    {
        Fixture        fixture;
        CommandContext context = fixture.Context();
        freshMarkedContext(fixture);
        registry.Invoke("undo", context);
        registry.Invoke("redo", context);
        REQUIRE_FALSE(fixture.buffer.HasMark());
    }
    {
        Fixture        fixture;
        CommandContext context = fixture.Context();
        freshMarkedContext(fixture);
        registry.Invoke("newline", context);
        REQUIRE_FALSE(fixture.buffer.HasMark());
    }
    {
        Fixture        fixture;
        CommandContext context = fixture.Context();
        freshMarkedContext(fixture);
        registry.Invoke("indent-for-tab-command", context);
        REQUIRE_FALSE(fixture.buffer.HasMark());
    }
}

TEST_CASE("kill-region without a mark is a no-op", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    Fixture        fixture;
    CommandContext context = fixture.Context();

    fixture.buffer.InsertAtPoint("hello");
    registry.Invoke("kill-region", context);

    REQUIRE(fixture.buffer.Text() == "hello");
    REQUIRE(fixture.killRing.Empty());
}

TEST_CASE("kill-region deletes the region, pushes it to the kill ring, and clears the mark", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    Fixture        fixture;
    CommandContext context = fixture.Context();

    fixture.buffer.InsertAtPoint("hello world");
    fixture.buffer.SetPoint(6);
    fixture.buffer.SetMark(0);
    registry.Invoke("kill-region", context);

    REQUIRE(fixture.buffer.Text() == "world");
    REQUIRE(fixture.buffer.Point() == 0);
    REQUIRE_FALSE(fixture.buffer.HasMark());
    REQUIRE(fixture.killRing.Current() == "hello ");
}

TEST_CASE("kill-region works with mark after point, same as mark before point", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    Fixture        fixture;
    CommandContext context = fixture.Context();

    fixture.buffer.InsertAtPoint("hello world");
    fixture.buffer.SetPoint(0);
    fixture.buffer.SetMark(6);
    registry.Invoke("kill-region", context);

    REQUIRE(fixture.buffer.Text() == "world");
    REQUIRE(fixture.killRing.Current() == "hello ");
}

TEST_CASE("kill-ring-save copies the region without deleting it, and clears the mark", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    Fixture        fixture;
    CommandContext context = fixture.Context();

    fixture.buffer.InsertAtPoint("hello world");
    fixture.buffer.SetPoint(6);
    fixture.buffer.SetMark(0);
    registry.Invoke("kill-ring-save", context);

    REQUIRE(fixture.buffer.Text() == "hello world");
    REQUIRE_FALSE(fixture.buffer.HasMark());
    REQUIRE(fixture.killRing.Current() == "hello ");
}

TEST_CASE("kill-ring-save without a mark is a no-op", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    Fixture        fixture;
    CommandContext context = fixture.Context();

    fixture.buffer.InsertAtPoint("hello");
    registry.Invoke("kill-ring-save", context);

    REQUIRE(fixture.killRing.Empty());
}

TEST_CASE("C-SPC, move, C-w round-trips through yank", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);
    Keymap     keymap = BuildDefaultGlobalKeymap();
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();

    Type(dispatcher, context, "hello world");
    fixture.buffer.SetPoint(0);
    dispatcher.Feed(ParseKeyChord("C-SPC"), context);
    for (int i = 0; i < 5; ++i) {
        dispatcher.Feed(ParseKeyChord("C-f"), context);
    }
    dispatcher.Feed(ParseKeyChord("C-w"), context);
    REQUIRE(fixture.buffer.Text() == " world");

    dispatcher.Feed(ParseKeyChord("C-y"), context);
    REQUIRE(fixture.buffer.Text() == "hello world");
}

TEST_CASE("M-w copies the region via kill-ring-save", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);
    Keymap     keymap = BuildDefaultGlobalKeymap();
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();

    Type(dispatcher, context, "hello world");
    fixture.buffer.SetPoint(0);
    dispatcher.Feed(ParseKeyChord("C-SPC"), context);
    for (int i = 0; i < 5; ++i) {
        dispatcher.Feed(ParseKeyChord("C-f"), context);
    }
    dispatcher.Feed(ParseKeyChord("M-w"), context);

    REQUIRE(fixture.buffer.Text() == "hello world");
    REQUIRE(fixture.killRing.Current() == "hello");
}

TEST_CASE("M-/ and ESC / both invoke redo", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);
    Keymap     keymap = BuildDefaultGlobalKeymap();
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();

    Type(dispatcher, context, "hi");
    dispatcher.Feed(ParseKeyChord("C-_"), context); // undo the typing
    REQUIRE(fixture.buffer.Text().empty());

    REQUIRE(dispatcher.Feed(ParseKeyChord("M-/"), context) == Dispatcher::Outcome::Invoked);
    REQUIRE(fixture.buffer.Text() == "hi");

    dispatcher.Feed(ParseKeyChord("C-_"), context);
    REQUIRE(fixture.buffer.Text().empty());
    REQUIRE(dispatcher.Feed(ParseKeyChord("ESC"), context) == Dispatcher::Outcome::Pending);
    REQUIRE(dispatcher.Feed(ParseKeyChord("/"), context) == Dispatcher::Outcome::Invoked);
    REQUIRE(fixture.buffer.Text() == "hi");
}

TEST_CASE("beginning-of-buffer and end-of-buffer move point to the extremes", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    ned::text::Buffer     buffer("scratch", ned::text::Rope("hello world"));
    ned::text::KillRing   killRing;
    ned::text::BufferList bufferList;
    CommandContext        context{buffer, killRing, bufferList};

    buffer.SetPoint(5);
    registry.Invoke("beginning-of-buffer", context);
    REQUIRE(buffer.Point() == 0);

    registry.Invoke("end-of-buffer", context);
    REQUIRE(buffer.Point() == buffer.Content().ByteLength());
}

TEST_CASE("M-< and M-> are bound to beginning/end-of-buffer", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);
    Keymap     keymap = BuildDefaultGlobalKeymap();
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();

    Type(dispatcher, context, "hello world");
    dispatcher.Feed(ParseKeyChord("M-<"), context);
    REQUIRE(fixture.buffer.Point() == 0);

    dispatcher.Feed(ParseKeyChord("M->"), context);
    REQUIRE(fixture.buffer.Point() == fixture.buffer.Content().ByteLength());
}

TEST_CASE("keyboard-quit deactivates an active mark", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    Fixture        fixture;
    CommandContext context = fixture.Context();

    fixture.buffer.InsertAtPoint("hello");
    fixture.buffer.SetMark(0);
    REQUIRE(fixture.buffer.HasMark());

    registry.Invoke("keyboard-quit", context);
    REQUIRE_FALSE(fixture.buffer.HasMark());
}

TEST_CASE("keyboard-quit without a mark is a no-op", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    Fixture        fixture;
    CommandContext context = fixture.Context();

    fixture.buffer.InsertAtPoint("hello");
    fixture.buffer.SetPoint(2);
    registry.Invoke("keyboard-quit", context);

    REQUIRE(fixture.buffer.Point() == 2);
    REQUIRE_FALSE(fixture.buffer.HasMark());
}

TEST_CASE("C-g is bound to keyboard-quit, stopping a shift-select in progress", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);
    Keymap     keymap = BuildDefaultGlobalKeymap();
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();

    Type(dispatcher, context, "hello world");
    fixture.buffer.SetPoint(0);

    KeyChord shiftRight;
    shiftRight.Shift   = true;
    shiftRight.Special = SpecialKey::Right;
    dispatcher.Feed(shiftRight, context);
    dispatcher.Feed(shiftRight, context);
    REQUIRE(fixture.buffer.HasMark());

    REQUIRE(dispatcher.Feed(ParseKeyChord("C-g"), context) == Dispatcher::Outcome::Invoked);
    REQUIRE_FALSE(fixture.buffer.HasMark());

    // The next Shift+Right starts a brand new selection from here, not a
    // resumption of the cancelled one.
    dispatcher.Feed(shiftRight, context);
    REQUIRE(fixture.buffer.Mark() == 2);
}

TEST_CASE("exchange-point-and-mark without a mark is a no-op", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    Fixture        fixture;
    CommandContext context = fixture.Context();

    fixture.buffer.InsertAtPoint("hello");
    fixture.buffer.SetPoint(2);
    registry.Invoke("exchange-point-and-mark", context);

    REQUIRE(fixture.buffer.Point() == 2);
    REQUIRE_FALSE(fixture.buffer.HasMark());
}

TEST_CASE("exchange-point-and-mark swaps point and mark", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    Fixture        fixture;
    CommandContext context = fixture.Context();

    fixture.buffer.InsertAtPoint("hello world");
    fixture.buffer.SetPoint(3);
    fixture.buffer.SetMark(8);
    registry.Invoke("exchange-point-and-mark", context);

    REQUIRE(fixture.buffer.Point() == 8);
    REQUIRE(fixture.buffer.HasMark());
    REQUIRE(fixture.buffer.Mark() == 3);
}

TEST_CASE("C-x C-x is bound to exchange-point-and-mark", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);
    Keymap     keymap = BuildDefaultGlobalKeymap();
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();

    Type(dispatcher, context, "hello world");
    fixture.buffer.SetPoint(3);
    fixture.buffer.SetMark(8);
    REQUIRE(dispatcher.Feed(ParseKeyChord("C-x"), context) == Dispatcher::Outcome::Pending);
    REQUIRE(dispatcher.Feed(ParseKeyChord("C-x"), context) == Dispatcher::Outcome::Invoked);

    REQUIRE(fixture.buffer.Point() == 8);
    REQUIRE(fixture.buffer.Mark() == 3);
}

TEST_CASE("indent-for-tab-command inserts a literal tab", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    Fixture        fixture;
    CommandContext context = fixture.Context();

    registry.Invoke("indent-for-tab-command", context);
    REQUIRE(fixture.buffer.Text() == "\t");
}

TEST_CASE("TAB in Normal mode inserts a tab via the global keymap", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);
    Keymap     keymap = BuildDefaultGlobalKeymap();
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();

    REQUIRE(dispatcher.Feed(ParseKeyChord("TAB"), context) == Dispatcher::Outcome::Invoked);
    REQUIRE(fixture.buffer.Text() == "\t");
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

TEST_CASE("save-buffer preserves a CRLF-detected buffer's ending by default", "[Commands][LineEnding]") {
    const LineEndingPolicyGuard guard;

    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_commands_test_line_ending_preserve.txt";
    {
        std::ofstream file(path, std::ios::binary);
        file << "one\r\ntwo\r\n";
    }

    ned::text::Buffer buffer = ned::text::Buffer::FromFile(path);
    buffer.InsertAtPoint("zero\n");

    ned::text::KillRing   killRing;
    ned::text::BufferList bufferList;
    CommandContext        context{buffer, killRing, bufferList};

    registry.Invoke("save-buffer", context);

    std::ifstream     written(path, std::ios::binary);
    const std::string writtenContent((std::istreambuf_iterator<char>(written)), std::istreambuf_iterator<char>());
    REQUIRE(writtenContent == "zero\r\none\r\ntwo\r\n");

    std::filesystem::remove(path);
}

TEST_CASE("save-buffer forces LF when the process-wide policy is set to Force(LF)", "[Commands][LineEnding]") {
    const LineEndingPolicyGuard guard;
    SetLineEndingPolicy({LineEndingPolicyMode::Force, ned::text::LineEnding::LF});

    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_commands_test_line_ending_force.txt";
    {
        std::ofstream file(path, std::ios::binary);
        file << "one\r\ntwo\r\n";
    }

    ned::text::Buffer buffer = ned::text::Buffer::FromFile(path);
    REQUIRE(buffer.LineEndingKind() == ned::text::LineEnding::CRLF);

    ned::text::KillRing   killRing;
    ned::text::BufferList bufferList;
    CommandContext        context{buffer, killRing, bufferList};

    registry.Invoke("save-buffer", context);

    std::ifstream     written(path, std::ios::binary);
    const std::string writtenContent((std::istreambuf_iterator<char>(written)), std::istreambuf_iterator<char>());
    REQUIRE(writtenContent == "one\ntwo\n");

    std::filesystem::remove(path);
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
    REQUIRE(written == "HELLO\n"); // save-buffer ensures a trailing newline on disk by default (EnsureFinalNewline())

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
    REQUIRE(written == "hello\n"); // save-buffer ensures a trailing newline on disk by default (EnsureFinalNewline())

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

// binary-safety-guardrails follow-up: save-buffer/format-buffer/
// convert-line-endings-to-* must all skip/refuse their byte-level,
// content-changing behavior for a buffer whose Buffer::LikelyBinary() is
// set, unless toggle-binary-safeguards has overridden it.

TEST_CASE("save-buffer skips format-on-save, ensure-final-newline, and forced line-ending conversion for a "
          "LikelyBinary buffer",
          "[Commands][BinarySafety]") {
    const FormatCommandGuard     guard;
    SetFormatCommand(std::string("tr 'a-z' 'A-Z'"));
    const LineEndingPolicyGuard policyGuard;
    SetLineEndingPolicy({LineEndingPolicyMode::Force, ned::text::LineEnding::CRLF});

    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_commands_test_binary_save.bin";
    std::filesystem::remove(path);

    ned::text::Buffer buffer("scratch", ned::text::Rope("hello")); // no trailing newline -- would normally gain one
    buffer.SetLikelyBinary(true);
    buffer.SaveToFile(path);

    ned::text::KillRing   killRing;
    ned::text::BufferList bufferList;
    std::string           message;
    CommandContext        context{buffer, killRing, bufferList, KeyChord{}, &message};

    registry.Invoke("save-buffer", context);

    REQUIRE(buffer.Text() == "hello"); // format-on-save never ran -- would have upper-cased it
    REQUIRE(message.find("(format command failed)") == std::string::npos);

    std::ifstream file(path, std::ios::binary);
    std::string   written((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    REQUIRE(written == "hello"); // no forced final newline, no CRLF conversion -- exact bytes preserved

    std::filesystem::remove(path);
}

TEST_CASE("toggle-binary-safeguards lets save-buffer apply its normal behavior again", "[Commands][BinarySafety]") {
    const FormatCommandGuard guard;
    SetFormatCommand(std::string("tr 'a-z' 'A-Z'"));

    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_commands_test_binary_override.bin";
    std::filesystem::remove(path);

    ned::text::Buffer buffer("scratch", ned::text::Rope("hello"));
    buffer.SetLikelyBinary(true);
    buffer.SaveToFile(path);

    ned::text::KillRing   killRing;
    ned::text::BufferList bufferList;
    std::string           message;
    CommandContext        context{buffer, killRing, bufferList, KeyChord{}, &message};

    registry.Invoke("toggle-binary-safeguards", context);
    REQUIRE(buffer.BinarySafetyOverride());
    REQUIRE(message.find("overridden") != std::string::npos);

    registry.Invoke("save-buffer", context);
    REQUIRE(buffer.Text() == "HELLO"); // format-on-save ran normally now

    std::filesystem::remove(path);
}

TEST_CASE("toggle-binary-safeguards is a no-op message for a buffer that was never detected as binary",
          "[Commands][BinarySafety]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    ned::text::Buffer     buffer("scratch", ned::text::Rope("hello"));
    ned::text::KillRing   killRing;
    ned::text::BufferList bufferList;
    std::string           message;
    CommandContext        context{buffer, killRing, bufferList, KeyChord{}, &message};

    registry.Invoke("toggle-binary-safeguards", context);

    REQUIRE_FALSE(buffer.BinarySafetyOverride());
    REQUIRE(message.find("never detected") != std::string::npos);
}

TEST_CASE("format-buffer refuses a LikelyBinary buffer until overridden", "[Commands][BinarySafety]") {
    const FormatCommandGuard guard;
    SetFormatCommand(std::string("tr 'a-z' 'A-Z'"));

    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    ned::text::Buffer buffer("scratch", ned::text::Rope("hello"));
    buffer.SetLikelyBinary(true);

    ned::text::KillRing   killRing;
    ned::text::BufferList bufferList;
    std::string           message;
    CommandContext        context{buffer, killRing, bufferList, KeyChord{}, &message};

    registry.Invoke("format-buffer", context);
    REQUIRE(buffer.Text() == "hello"); // refused, never ran
    REQUIRE(message.find("refusing to format") != std::string::npos);

    buffer.SetBinarySafetyOverride(true);
    registry.Invoke("format-buffer", context);
    REQUIRE(buffer.Text() == "HELLO");
}

TEST_CASE("convert-line-endings-to-crlf refuses a LikelyBinary buffer until overridden", "[Commands][BinarySafety]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    ned::text::Buffer buffer("scratch", ned::text::Rope("hello\n"));
    buffer.SetLikelyBinary(true);

    ned::text::KillRing   killRing;
    ned::text::BufferList bufferList;
    std::string           message;
    CommandContext        context{buffer, killRing, bufferList, KeyChord{}, &message};

    registry.Invoke("convert-line-endings-to-crlf", context);
    REQUIRE(buffer.LineEndingKind() == ned::text::LineEnding::LF); // refused -- unchanged from its constructed default
    REQUIRE(message.find("refusing to convert") != std::string::npos);

    buffer.SetBinarySafetyOverride(true);
    registry.Invoke("convert-line-endings-to-crlf", context);
    REQUIRE(buffer.LineEndingKind() == ned::text::LineEnding::CRLF);
}

TEST_CASE("format-buffer formats without saving", "[Commands]") {
    const FormatCommandGuard guard;
    SetFormatCommand(std::string("tr 'a-z' 'A-Z'"));

    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_commands_test_format_buffer.txt";
    std::filesystem::remove(path);

    ned::text::Buffer buffer("scratch", ned::text::Rope("hello"));
    buffer.SaveToFile(path);

    ned::text::KillRing   killRing;
    ned::text::BufferList bufferList;
    std::string           message;
    CommandContext        context{buffer, killRing, bufferList, KeyChord{}, &message};

    registry.Invoke("format-buffer", context);

    REQUIRE(buffer.Text() == "HELLO");
    REQUIRE(message.find("Formatted") != std::string::npos);

    std::ifstream file(path);
    std::string   written((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    REQUIRE(written == "hello\n"); // never saved -- disk content is untouched (SaveToFile's own default trailing newline)

    std::filesystem::remove(path);
}

TEST_CASE("format-buffer reports failure and leaves the buffer untouched when the formatter fails", "[Commands]") {
    const FormatCommandGuard guard;
    SetFormatCommand(std::string("false"));

    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    Fixture        fixture;
    CommandContext context = fixture.Context();
    std::string    message;
    context.message = &message;

    fixture.buffer.InsertAtPoint("hello");
    registry.Invoke("format-buffer", context);

    REQUIRE(fixture.buffer.Text() == "hello");
    REQUIRE(message.find("failed") != std::string::npos);
}

TEST_CASE("format-buffer reports explicitly when no format command is configured", "[Commands]") {
    const FormatCommandGuard guard;

    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    Fixture        fixture;
    CommandContext context = fixture.Context();
    std::string    message;
    context.message = &message;

    fixture.buffer.InsertAtPoint("hello");
    registry.Invoke("format-buffer", context);

    REQUIRE(fixture.buffer.Text() == "hello");
    REQUIRE(message.find("No format command configured") != std::string::npos);
}

TEST_CASE("convert-line-endings-to-crlf overrides the buffer's tracked ending without touching live content",
          "[Commands][LineEnding]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    Fixture        fixture;
    CommandContext context = fixture.Context();
    std::string    message;
    context.message = &message;

    fixture.buffer.InsertAtPoint("one\ntwo\n");
    REQUIRE(fixture.buffer.LineEndingKind() == ned::text::LineEnding::LF);

    registry.Invoke("convert-line-endings-to-crlf", context);

    REQUIRE(fixture.buffer.LineEndingKind() == ned::text::LineEnding::CRLF);
    REQUIRE(fixture.buffer.Text() == "one\ntwo\n"); // live content is never touched -- disk-only until saved
    REQUIRE(message.find("CRLF") != std::string::npos);
}

TEST_CASE("convert-line-endings-to-lf/-cr set the expected ending", "[Commands][LineEnding]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    Fixture        fixture;
    CommandContext context = fixture.Context();

    registry.Invoke("convert-line-endings-to-lf", context);
    REQUIRE(fixture.buffer.LineEndingKind() == ned::text::LineEnding::LF);

    registry.Invoke("convert-line-endings-to-cr", context);
    REQUIRE(fixture.buffer.LineEndingKind() == ned::text::LineEnding::CR);
}

TEST_CASE("kill-buffer sets InteractiveRequest::KillBuffer, bound to C-x k", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);
    Keymap     keymap = BuildDefaultGlobalKeymap();
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();

    REQUIRE(dispatcher.Feed(ParseKeyChord("C-x"), context) == Dispatcher::Outcome::Pending);
    REQUIRE(dispatcher.Feed(ParseKeyChord("k"), context) == Dispatcher::Outcome::Invoked);
    REQUIRE(context.interactiveRequest == InteractiveRequest::KillBuffer);
}

TEST_CASE("C-v/M-v are bound to scroll-page-down/up", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);
    Keymap     keymap = BuildDefaultGlobalKeymap();
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();

    REQUIRE(dispatcher.Feed(ParseKeyChord("C-v"), context) == Dispatcher::Outcome::Invoked);
    REQUIRE(dispatcher.Feed(ParseKeyChord("M-v"), context) == Dispatcher::Outcome::Invoked);
}

TEST_CASE("C-LEFT/C-RIGHT are bound to word motion", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);
    Keymap     keymap = BuildDefaultGlobalKeymap();
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();

    Type(dispatcher, context, "hello world");
    fixture.buffer.SetPoint(0);

    REQUIRE(dispatcher.Feed(ParseKeyChord("C-RIGHT"), context) == Dispatcher::Outcome::Invoked);
    REQUIRE(fixture.buffer.Point() == 5);

    REQUIRE(dispatcher.Feed(ParseKeyChord("C-LEFT"), context) == Dispatcher::Outcome::Invoked);
    REQUIRE(fixture.buffer.Point() == 0);
}

TEST_CASE("shift-select-forward-char sets a mark on first use, then extends past it", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    Fixture        fixture;
    CommandContext context = fixture.Context();

    fixture.buffer.InsertAtPoint("hello world");
    fixture.buffer.SetPoint(2);
    REQUIRE_FALSE(fixture.buffer.HasMark());

    registry.Invoke("shift-select-forward-char", context);
    REQUIRE(fixture.buffer.HasMark());
    REQUIRE(fixture.buffer.Mark() == 2);
    REQUIRE(fixture.buffer.Point() == 3);

    registry.Invoke("shift-select-forward-char", context);
    REQUIRE(fixture.buffer.Mark() == 2); // anchor unchanged -- the same selection keeps extending
    REQUIRE(fixture.buffer.Point() == 4);
}

TEST_CASE("shift-select commands extend an existing mark rather than resetting its anchor", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    Fixture        fixture;
    CommandContext context = fixture.Context();

    fixture.buffer.InsertAtPoint("hello world");
    fixture.buffer.SetPoint(5);
    fixture.buffer.SetMark(0); // an existing mark, e.g. from C-SPC

    registry.Invoke("shift-select-forward-char", context);
    REQUIRE(fixture.buffer.Mark() == 0); // untouched, not reset to point
    REQUIRE(fixture.buffer.Point() == 6);
}

TEST_CASE("move-line-up swaps the current line with the line above, preserving column", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    Fixture        fixture;
    CommandContext context = fixture.Context();

    fixture.buffer.InsertAtPoint("a\nb\nc"); // last line, "c", has no trailing newline
    fixture.buffer.SetPoint(4);              // start of "c"

    registry.Invoke("move-line-up", context);

    REQUIRE(fixture.buffer.Text() == "a\nc\nb");
    REQUIRE(fixture.buffer.Point() == 2); // start of "c", now the second line
}

TEST_CASE("move-line-up on the first line is a no-op", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    Fixture        fixture;
    CommandContext context = fixture.Context();

    fixture.buffer.InsertAtPoint("a\nb\nc");
    fixture.buffer.SetPoint(0);

    registry.Invoke("move-line-up", context);

    REQUIRE(fixture.buffer.Text() == "a\nb\nc");
    REQUIRE(fixture.buffer.Point() == 0);
}

TEST_CASE("move-line-down swaps the current line with the line below, preserving column", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    Fixture        fixture;
    CommandContext context = fixture.Context();

    fixture.buffer.InsertAtPoint("a\nb\nc"); // "c" (below "b") has no trailing newline
    fixture.buffer.SetPoint(2);              // start of "b"

    registry.Invoke("move-line-down", context);

    REQUIRE(fixture.buffer.Text() == "a\nc\nb");
    REQUIRE(fixture.buffer.Point() == 4); // start of "b", now the last line
}

TEST_CASE("move-line-down on the last line is a no-op", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    Fixture        fixture;
    CommandContext context = fixture.Context();

    fixture.buffer.InsertAtPoint("a\nb\nc");
    fixture.buffer.SetPoint(4);

    registry.Invoke("move-line-down", context);

    REQUIRE(fixture.buffer.Text() == "a\nb\nc");
    REQUIRE(fixture.buffer.Point() == 4);
}

TEST_CASE("move-line-up/down clear an existing mark", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    Fixture        fixture;
    CommandContext context = fixture.Context();

    fixture.buffer.InsertAtPoint("a\nb\nc");
    fixture.buffer.SetPoint(2);
    fixture.buffer.SetMark(0);

    registry.Invoke("move-line-down", context);
    REQUIRE_FALSE(fixture.buffer.HasMark());
}

TEST_CASE("duplicate-line copies the current line below it, moving point into the copy", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    Fixture        fixture;
    CommandContext context = fixture.Context();

    fixture.buffer.InsertAtPoint("a\nb\nc");
    fixture.buffer.SetPoint(0); // start of "a"

    registry.Invoke("duplicate-line", context);

    REQUIRE(fixture.buffer.Text() == "a\na\nb\nc");
    REQUIRE(fixture.buffer.Point() == 2); // start of the duplicate "a"
}

TEST_CASE("duplicate-line on the last (trailing-newline-less) line still terminates correctly", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    Fixture        fixture;
    CommandContext context = fixture.Context();

    fixture.buffer.InsertAtPoint("a\nb\nc"); // "c" has no trailing newline
    fixture.buffer.SetPoint(4);              // start of "c"

    registry.Invoke("duplicate-line", context);

    REQUIRE(fixture.buffer.Text() == "a\nb\nc\nc");
    REQUIRE(fixture.buffer.Point() == 6); // start of the duplicate "c"
}

TEST_CASE("M-UP/M-DOWN are bound to move-line-up/down, C-c d to duplicate-line", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);
    Keymap     keymap = BuildDefaultGlobalKeymap();
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();

    fixture.buffer.InsertAtPoint("a\nb");
    fixture.buffer.SetPoint(2);

    REQUIRE(dispatcher.Feed(ParseKeyChord("M-UP"), context) == Dispatcher::Outcome::Invoked);
    REQUIRE(fixture.buffer.Text() == "b\na");

    REQUIRE(dispatcher.Feed(ParseKeyChord("M-DOWN"), context) == Dispatcher::Outcome::Invoked);
    REQUIRE(fixture.buffer.Text() == "a\nb");

    REQUIRE(dispatcher.Feed(ParseKeyChord("C-c"), context) == Dispatcher::Outcome::Pending);
    REQUIRE(dispatcher.Feed(ParseKeyChord("d"), context) == Dispatcher::Outcome::Invoked);
    REQUIRE(fixture.buffer.Text() == "a\nb\nb");
}

TEST_CASE("S-LEFT/S-RIGHT/S-UP/S-DOWN are bound to the shift-select commands", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);
    Keymap     keymap = BuildDefaultGlobalKeymap();
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();

    Type(dispatcher, context, "hello world");
    fixture.buffer.SetPoint(5);

    KeyChord shiftRight;
    shiftRight.Shift   = true;
    shiftRight.Special = SpecialKey::Right;
    REQUIRE(dispatcher.Feed(shiftRight, context) == Dispatcher::Outcome::Invoked);
    REQUIRE(fixture.buffer.HasMark());
    REQUIRE(fixture.buffer.Mark() == 5);
    REQUIRE(fixture.buffer.Point() == 6);

    KeyChord shiftLeft;
    shiftLeft.Shift   = true;
    shiftLeft.Special = SpecialKey::Left;
    REQUIRE(dispatcher.Feed(shiftLeft, context) == Dispatcher::Outcome::Invoked);
    REQUIRE(fixture.buffer.Point() == 5);

    KeyChord shiftDown;
    shiftDown.Shift   = true;
    shiftDown.Special = SpecialKey::Down;
    REQUIRE(dispatcher.Feed(shiftDown, context) == Dispatcher::Outcome::Invoked);

    KeyChord shiftUp;
    shiftUp.Shift   = true;
    shiftUp.Special = SpecialKey::Up;
    REQUIRE(dispatcher.Feed(shiftUp, context) == Dispatcher::Outcome::Invoked);
}

TEST_CASE("M-f/M-b/M-% are bound as real Meta chords, not just ESC-prefix", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);
    Keymap     keymap = BuildDefaultGlobalKeymap();
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();

    Type(dispatcher, context, "hello world");
    fixture.buffer.SetPoint(0);

    REQUIRE(dispatcher.Feed(ParseKeyChord("M-f"), context) == Dispatcher::Outcome::Invoked);
    REQUIRE(fixture.buffer.Point() == 5);

    REQUIRE(dispatcher.Feed(ParseKeyChord("M-b"), context) == Dispatcher::Outcome::Invoked);
    REQUIRE(fixture.buffer.Point() == 0);

    REQUIRE(dispatcher.Feed(ParseKeyChord("M-%"), context) == Dispatcher::Outcome::Invoked);
    REQUIRE(context.interactiveRequest == InteractiveRequest::QueryReplace);
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

TEST_CASE("org-agenda sets interactiveRequest and is bound to C-c a", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);
    Keymap     keymap = BuildDefaultGlobalKeymap();
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();

    REQUIRE(dispatcher.Feed(ParseKeyChord("C-c"), context) == Dispatcher::Outcome::Pending);
    REQUIRE(dispatcher.Feed(ParseKeyChord("a"), context) == Dispatcher::Outcome::Invoked);
    REQUIRE(context.interactiveRequest == InteractiveRequest::ProjectAgenda);
    REQUIRE(fixture.buffer.Text().empty()); // the command itself doesn't touch the buffer
}

TEST_CASE("show-messages sets interactiveRequest and is reachable via M-x (no dedicated keychord)", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    Fixture        fixture;
    CommandContext context = fixture.Context();
    registry.Invoke("show-messages", context);
    REQUIRE(context.interactiveRequest == InteractiveRequest::ShowMessages);
    REQUIRE(fixture.buffer.Text().empty()); // the command itself doesn't touch the buffer
}

TEST_CASE("org-clock-report sets interactiveRequest and is bound to C-c C-x r in Org's own keymap", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    Fixture        fixture;
    CommandContext context = fixture.Context();
    registry.Invoke("org-clock-report", context);
    REQUIRE(context.interactiveRequest == InteractiveRequest::OrgClockReport);
    REQUIRE(fixture.buffer.Text().empty()); // the command itself doesn't touch the buffer

    // org-clock-display follow-up: mode-local (unlike org-agenda above),
    // same "C-c C-x" prefix org-clock-in/-out/org-set-property/-delete-property
    // already share -- resolved directly against Org's own keymap rather
    // than through a Dispatcher, since Org's keymap isn't part of the
    // global default one.
    const Keymap org = OrgMode().keymap;
    REQUIRE(org.Resolve(ParseKeySequence("C-c C-x r")).result == Keymap::LookupResult::Match);
    REQUIRE(org.Resolve(ParseKeySequence("C-c C-x r")).commandName == "org-clock-report");
}

// keymap-collision follow-up: guards against reintroducing the class of bug
// that made "C-c a s"/"C-c a p"/"C-c a k" unreachable by typing once "C-c a"
// was bound to org-agenda (see Keymap::Resolve's own comment and
// ROADMAP.md). AmbiguousBindings() flags any sequence that is both a
// complete command binding and a prefix of a longer one -- Resolve fires the
// shorter Match first and never consults the longer children, so such a
// binding can never be typed. Every shipped keymap with real nesting
// (the global default plus Org's and Markdown's mode-local overlays) is
// checked here; a future collision like this should fail ctest, not need
// another live tmux session to find.
TEST_CASE("shipped keymaps have no unreachable-by-typing bindings", "[Commands][Keymap]") {
    const Keymap global = BuildDefaultGlobalKeymap();
    CHECK(global.AmbiguousBindings().empty());

    const Keymap org = OrgMode().keymap;
    CHECK(org.AmbiguousBindings().empty());

    const Keymap markdown = MarkdownMode().keymap;
    CHECK(markdown.AmbiguousBindings().empty());
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

TEST_CASE("self-insert-command pairs a quote typed inside a just-paired bracket (if (\" scenario)", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    Fixture        fixture;
    CommandContext context = fixture.Context();

    auto type = [&](char c) {
        context.triggeringKey.Special   = SpecialKey::None;
        context.triggeringKey.Codepoint = static_cast<char32_t>(c);
        registry.Invoke("self-insert-command", context);
    };

    for (char c : std::string_view("if (")) {
        type(c);
    }
    REQUIRE(fixture.buffer.Text() == "if ()");
    REQUIRE(fixture.buffer.Point() == 4); // "if (|)"

    type('"');
    REQUIRE(fixture.buffer.Text() == "if (\"\")");
    REQUIRE(fixture.buffer.Point() == 5); // "if (\"|\")"
}

TEST_CASE("self-insert-command respects the active Mode's autoPairs: Janet mode skips single quotes, still pairs "
          "double quotes and brackets",
          "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    Fixture        fixture;
    CommandContext context = fixture.Context();
    const Mode     janetMode = JanetMode();
    context.mode             = &janetMode;

    auto type = [&](char c) {
        context.triggeringKey.Special   = SpecialKey::None;
        context.triggeringKey.Codepoint = static_cast<char32_t>(c);
        registry.Invoke("self-insert-command", context);
    };

    type('\''); // real Janet quote-macro syntax, e.g. '(a b c) -- must not pair
    REQUIRE(fixture.buffer.Text() == "'");

    type('"'); // Janet strings are still double-quoted -- must pair
    REQUIRE(fixture.buffer.Text() == "'\"\"");
    REQUIRE(fixture.buffer.Point() == 2); // "'\"|\""

    fixture.buffer.SetPoint(fixture.buffer.Content().ByteLength()); // past both quotes
    type('(');                                                      // and brackets pair as usual, regardless of mode
    REQUIRE(fixture.buffer.Text() == "'\"\"()");
}

TEST_CASE("self-insert-command/backward-delete-char pair nothing when SetAutoPairEnabled(false)", "[Commands]") {
    struct AutoPairEnabledGuard {
        ~AutoPairEnabledGuard() {
            SetAutoPairEnabled(true);
        }
    } guard;
    SetAutoPairEnabled(false);

    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    Fixture        fixture;
    CommandContext context = fixture.Context();

    context.triggeringKey.Special   = SpecialKey::None;
    context.triggeringKey.Codepoint = '(';
    registry.Invoke("self-insert-command", context);
    REQUIRE(fixture.buffer.Text() == "("); // no auto-inserted closer

    fixture.buffer.InsertAtPoint(")"); // "()" with point between, as if typed plainly
    fixture.buffer.SetPoint(1);
    registry.Invoke("backward-delete-char", context); // must delete only the "(" -- not the adjacent-pair collapse
    REQUIRE(fixture.buffer.Text() == ")");
}

TEST_CASE("self-insert-command suppresses quote pairing when point is already inside a comment", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    Fixture        fixture;
    CommandContext context = fixture.Context();
    const Mode     cMode   = CMode();
    context.mode           = &cMode;

    auto type = [&](std::string_view text) {
        for (const char c : text) {
            context.triggeringKey.Special   = SpecialKey::None;
            context.triggeringKey.Codepoint = static_cast<char32_t>(c);
            registry.Invoke("self-insert-command", context);
        }
    };

    type("// hello ");
    REQUIRE(fixture.buffer.Text() == "// hello ");

    type("\""); // inside a line comment -- must NOT pair
    REQUIRE(fixture.buffer.Text() == "// hello \"");
}

TEST_CASE("self-insert-command still pairs a quote typed in ordinary code, not inside a string/comment",
          "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    Fixture        fixture;
    CommandContext context = fixture.Context();
    const Mode     cMode   = CMode();
    context.mode           = &cMode;

    auto type = [&](std::string_view text) {
        for (const char c : text) {
            context.triggeringKey.Special   = SpecialKey::None;
            context.triggeringKey.Codepoint = static_cast<char32_t>(c);
            registry.Invoke("self-insert-command", context);
        }
    };

    type("int x = ");
    type("\"");
    REQUIRE(fixture.buffer.Text() == "int x = \"\"");
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

TEST_CASE("M-e / M-a bindings move point by sentence", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);
    Keymap     keymap = BuildDefaultGlobalKeymap();
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();

    fixture.buffer.InsertAtPoint("Foo. Bar.");
    fixture.buffer.SetPoint(0);

    REQUIRE(dispatcher.Feed(ParseKeyChord("M-e"), context) == Dispatcher::Outcome::Invoked);
    REQUIRE(fixture.buffer.Point() == 5); // start of "Bar."

    REQUIRE(dispatcher.Feed(ParseKeyChord("M-a"), context) == Dispatcher::Outcome::Invoked);
    REQUIRE(fixture.buffer.Point() == 0); // back to the start of "Foo."
}

TEST_CASE("C-M-f / C-M-b bindings move point by sexp, using the active mode's syntax tree", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);
    Keymap     keymap = BuildDefaultGlobalKeymap();
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    const Mode mode = CMode();

    Fixture        fixture;
    CommandContext context = fixture.Context();
    context.mode           = &mode;

    fixture.buffer.InsertAtPoint("foo(a, b);");
    fixture.buffer.SetPoint(0);

    // Point 0 sits exactly at the "foo" identifier's own start -- the
    // smallest named node there -- so the first move is just over "foo",
    // not the whole call (a C grammar's call_expression and its function
    // identifier share the same start byte; the *smallest* node wins).
    REQUIRE(dispatcher.Feed(ParseKeyChord("C-M-f"), context) == Dispatcher::Outcome::Invoked);
    REQUIRE(fixture.buffer.Point() == 3); // over "foo"

    // Point 3 now sits exactly at the argument list's own start ("(") --
    // the next move goes over the whole "(a, b)".
    REQUIRE(dispatcher.Feed(ParseKeyChord("C-M-f"), context) == Dispatcher::Outcome::Invoked);
    REQUIRE(fixture.buffer.Point() == 9); // over "(a, b)"

    REQUIRE(dispatcher.Feed(ParseKeyChord("C-M-b"), context) == Dispatcher::Outcome::Invoked);
    REQUIRE(fixture.buffer.Point() == 3); // back to the start of "(a, b)"

    REQUIRE(dispatcher.Feed(ParseKeyChord("C-M-b"), context) == Dispatcher::Outcome::Invoked);
    REQUIRE(fixture.buffer.Point() == 0); // back to the start of "foo"
}

TEST_CASE("forward-sexp reports no mode configured, mirroring code-fold-toggle", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    Fixture        fixture;
    CommandContext context = fixture.Context();
    std::string    message;
    context.message = &message;

    fixture.buffer.InsertAtPoint("foo(a, b);");
    fixture.buffer.SetPoint(0);

    registry.Invoke("forward-sexp", context);
    REQUIRE(message == "No sexp motion available in this mode.");
    REQUIRE(fixture.buffer.Point() == 0);
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

TEST_CASE("lsp-show-diagnostic reports the message of a diagnostic covering point", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    Fixture        fixture;
    CommandContext context = fixture.Context();
    std::string    message;
    context.message = &message;

    fixture.buffer.InsertAtPoint("int x = 1;\nint y = 2;\n");
    fixture.buffer.SetDiagnostics({
        ned::text::Buffer::Diagnostic{
            .startByte = 4, .endByte = 5, .severity = ned::text::Buffer::Diagnostic::Severity::Warning, .message = "unused variable x"},
    });

    fixture.buffer.SetPoint(4); // right at the diagnostic's own start
    registry.Invoke("lsp-show-diagnostic", context);
    REQUIRE(message == "unused variable x");

    // diagnostics-UX follow-up: outside the span but on its line still
    // reports it -- the gutter icon is a per-line signal, so this must be
    // too.
    message.clear();
    fixture.buffer.SetPoint(0);
    registry.Invoke("lsp-show-diagnostic", context);
    REQUIRE(message == "unused variable x");

    message.clear();
    fixture.buffer.SetPoint(fixture.buffer.Content().LineToByteOffset(1)); // a different line entirely
    registry.Invoke("lsp-show-diagnostic", context);
    REQUIRE(message == "No diagnostic at point.");
}

TEST_CASE("lsp-show-diagnostic counts additional diagnostics sharing the line", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    Fixture        fixture;
    CommandContext context = fixture.Context();
    std::string    message;
    context.message = &message;

    fixture.buffer.InsertAtPoint("int x = 1;\n");
    fixture.buffer.SetDiagnostics({
        ned::text::Buffer::Diagnostic{
            .startByte = 4, .endByte = 5, .severity = ned::text::Buffer::Diagnostic::Severity::Warning, .message = "unused variable x"},
        ned::text::Buffer::Diagnostic{
            .startByte = 8, .endByte = 9, .severity = ned::text::Buffer::Diagnostic::Severity::Hint, .message = "magic number"},
    });

    fixture.buffer.SetPoint(0); // on the line, inside neither span
    registry.Invoke("lsp-show-diagnostic", context);
    REQUIRE(message == "unused variable x (+1 more on this line)");
}

TEST_CASE("lsp-show-diagnostic matches a zero-width diagnostic at its offset, and via its line elsewhere", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    Fixture        fixture;
    CommandContext context = fixture.Context();
    std::string    message;
    context.message = &message;

    fixture.buffer.InsertAtPoint("abc\ndef\n");
    fixture.buffer.SetDiagnostics({
        ned::text::Buffer::Diagnostic{
            .startByte = 2, .endByte = 2, .severity = ned::text::Buffer::Diagnostic::Severity::Hint, .message = "zero-width hint"},
    });

    fixture.buffer.SetPoint(2);
    registry.Invoke("lsp-show-diagnostic", context);
    REQUIRE(message == "zero-width hint");

    // diagnostics-UX follow-up: elsewhere on the same line now falls back to
    // the line's diagnostic rather than reporting nothing.
    message.clear();
    fixture.buffer.SetPoint(3);
    registry.Invoke("lsp-show-diagnostic", context);
    REQUIRE(message == "zero-width hint");

    message.clear();
    fixture.buffer.SetPoint(fixture.buffer.Content().LineToByteOffset(1)); // a different line entirely
    registry.Invoke("lsp-show-diagnostic", context);
    REQUIRE(message == "No diagnostic at point.");
}

// org-cycle-todo/org-cycle-priority/org-toggle-checkbox aren't in the
// global keymap -- they're bound in OrgMode's own keymap (Mode.cpp), only
// layered in for a real .org buffer -- so these invoke the registry
// directly by name rather than feeding key chords through a Dispatcher,
// the same way this file already tests registry-level behavior that isn't
// keymap-specific.

TEST_CASE("org-cycle-todo cycles the headline at point and reports failure off a headline", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    Fixture        fixture;
    CommandContext context = fixture.Context();
    std::string    message;
    context.message = &message;

    fixture.buffer.InsertAtPoint("* Buy milk\n");
    fixture.buffer.SetPoint(2);

    registry.Invoke("org-cycle-todo", context);
    REQUIRE(fixture.buffer.Text() == "* TODO Buy milk\n");
    REQUIRE(message.empty());

    message.clear();
    fixture.buffer.SetPoint(fixture.buffer.Size());
    fixture.buffer.InsertAtPoint("plain text\n");
    fixture.buffer.SetPoint(fixture.buffer.Size() - 5); // inside "plain text", not the headline above

    registry.Invoke("org-cycle-todo", context);
    REQUIRE(message == "Not on a headline.");
}

TEST_CASE("org-cycle-priority cycles the headline at point", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    Fixture        fixture;
    CommandContext context = fixture.Context();

    fixture.buffer.InsertAtPoint("* Buy milk\n");
    fixture.buffer.SetPoint(2);

    registry.Invoke("org-cycle-priority", context);
    REQUIRE(fixture.buffer.Text() == "* [#A] Buy milk\n");
}

TEST_CASE("org-toggle-checkbox toggles the checkbox at point and reports failure off a checkbox", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    Fixture        fixture;
    CommandContext context = fixture.Context();
    std::string    message;
    context.message = &message;

    fixture.buffer.InsertAtPoint("- [ ] Buy milk\n");
    fixture.buffer.SetPoint(0);

    registry.Invoke("org-toggle-checkbox", context);
    REQUIRE(fixture.buffer.Text() == "- [X] Buy milk\n");
    REQUIRE(message.empty());

    fixture.buffer.SetPoint(fixture.buffer.Size());
    fixture.buffer.InsertAtPoint("plain text");
    fixture.buffer.SetPoint(fixture.buffer.Size() - 3);
    registry.Invoke("org-toggle-checkbox", context);
    REQUIRE(message == "Not on a checkbox.");
}

TEST_CASE("org-cycle advances the headline at point through the 3-state fold cycle", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    Fixture        fixture;
    CommandContext context = fixture.Context();
    std::string    message;
    context.message = &message;

    fixture.buffer.InsertAtPoint("* Parent\nbody\n* Sibling\n");
    fixture.buffer.SetPoint(2); // inside "* Parent"

    registry.Invoke("org-cycle", context);
    REQUIRE(fixture.buffer.FoldMarkerAt(0) == ned::text::Buffer::FoldMarker::Collapsed);
    REQUIRE(message.empty());

    fixture.buffer.SetPoint(fixture.buffer.Size());
    fixture.buffer.InsertAtPoint("plain text");
    fixture.buffer.SetPoint(fixture.buffer.Size() - 3); // inside "plain text", not a headline

    registry.Invoke("org-cycle", context);
    REQUIRE(message == "Not on a headline.");
}

TEST_CASE("code-fold-toggle reports no folding available when context.mode is unset", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    Fixture        fixture;
    CommandContext context = fixture.Context();
    std::string    message;
    context.message = &message;
    // context.mode left at its default (nullptr) -- the headless/no-UI case.

    fixture.buffer.InsertAtPoint("int main(void) {\n    return 0;\n}\n");
    fixture.buffer.SetPoint(0);

    registry.Invoke("code-fold-toggle", context);
    REQUIRE(message == "No folding available in this mode.");
    REQUIRE(fixture.buffer.FoldMarkers().empty());
}

TEST_CASE("code-fold-toggle folds the block starting at point when context.mode has a fold query", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    const Mode mode = CMode();

    Fixture        fixture;
    CommandContext context = fixture.Context();
    std::string    message;
    context.message = &message;
    context.mode    = &mode;

    fixture.buffer.InsertAtPoint("int main(void) {\n    return 0;\n}\n");
    fixture.buffer.SetPoint(0); // on the function's own opening line

    registry.Invoke("code-fold-toggle", context);
    REQUIRE(message.empty());
    REQUIRE(fixture.buffer.FoldMarkers().size() == 1);

    fixture.buffer.SetPoint(fixture.buffer.Text().find("return")); // not a block's own opening line
    registry.Invoke("code-fold-toggle", context);
    REQUIRE(message == "No foldable block starts here.");
}

TEST_CASE("toggle-line-comment reports explicitly when no mode/comment syntax is configured", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    Fixture        fixture;
    CommandContext context = fixture.Context();
    std::string    message;
    context.message = &message;
    // context.mode left at its default (nullptr).

    fixture.buffer.InsertAtPoint("hello");
    registry.Invoke("toggle-line-comment", context);
    REQUIRE(message == "No comment syntax configured for this mode.");
    REQUIRE(fixture.buffer.Text() == "hello");

    const Mode json = JsonMode(); // has no lineCommentPrefix -- JSON has no comment syntax
    context.mode    = &json;
    registry.Invoke("toggle-line-comment", context);
    REQUIRE(message == "No comment syntax configured for this mode.");
    REQUIRE(fixture.buffer.Text() == "hello");
}

TEST_CASE("toggle-line-comment comments then uncomments a single line", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    const Mode cMode = CMode();

    Fixture        fixture;
    CommandContext context = fixture.Context();
    context.mode           = &cMode;

    fixture.buffer.InsertAtPoint("int x = 1;");
    fixture.buffer.SetPoint(0);

    registry.Invoke("toggle-line-comment", context);
    REQUIRE(fixture.buffer.Text() == "// int x = 1;");

    registry.Invoke("toggle-line-comment", context);
    REQUIRE(fixture.buffer.Text() == "int x = 1;");
}

TEST_CASE("toggle-line-comment preserves indentation", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    const Mode pyMode = PythonMode();

    Fixture        fixture;
    CommandContext context = fixture.Context();
    context.mode           = &pyMode;

    fixture.buffer.InsertAtPoint("    x = 1");
    fixture.buffer.SetPoint(4);

    registry.Invoke("toggle-line-comment", context);
    REQUIRE(fixture.buffer.Text() == "    # x = 1");

    registry.Invoke("toggle-line-comment", context);
    REQUIRE(fixture.buffer.Text() == "    x = 1");
}

TEST_CASE("toggle-line-comment leaves a blank line untouched", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    const Mode cMode = CMode();

    Fixture        fixture;
    CommandContext context = fixture.Context();
    context.mode           = &cMode;

    fixture.buffer.InsertAtPoint("   ");
    fixture.buffer.SetPoint(0);

    registry.Invoke("toggle-line-comment", context);
    REQUIRE(fixture.buffer.Text() == "   ");
}

TEST_CASE("toggle-line-comment over a region comments every uncommented line, skipping blanks", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    const Mode cMode = CMode();

    Fixture        fixture;
    CommandContext context = fixture.Context();
    context.mode           = &cMode;

    fixture.buffer.InsertAtPoint("a\n\nb\n// c");
    fixture.buffer.SetPoint(0);
    fixture.buffer.SetMark(fixture.buffer.Content().ByteLength()); // whole buffer

    registry.Invoke("toggle-line-comment", context);
    REQUIRE(fixture.buffer.Text() == "// a\n\n// b\n// c"); // blank line 2 untouched, already-commented line 4 untouched
    REQUIRE_FALSE(fixture.buffer.HasMark());
}

TEST_CASE("toggle-line-comment uncomments every line only once all non-blank lines are commented", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    const Mode cMode = CMode();

    Fixture        fixture;
    CommandContext context = fixture.Context();
    context.mode           = &cMode;

    fixture.buffer.InsertAtPoint("// a\n// b");
    fixture.buffer.SetPoint(0);
    fixture.buffer.SetMark(fixture.buffer.Content().ByteLength());

    registry.Invoke("toggle-line-comment", context);
    REQUIRE(fixture.buffer.Text() == "a\nb");
}

TEST_CASE("toggle-line-comment's region excludes a line the selection end merely touches at column 0", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    const Mode cMode = CMode();

    Fixture        fixture;
    CommandContext context = fixture.Context();
    context.mode           = &cMode;

    fixture.buffer.InsertAtPoint("a\nb\nc");
    fixture.buffer.SetPoint(0);
    fixture.buffer.SetMark(4); // start of line "c" (index 4), not into it

    registry.Invoke("toggle-line-comment", context);
    REQUIRE(fixture.buffer.Text() == "// a\n// b\nc"); // "c" untouched
}

TEST_CASE("M-;/ESC ; are bound to toggle-line-comment", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);
    Keymap     keymap = BuildDefaultGlobalKeymap();
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    const Mode cMode = CMode();

    Fixture        fixture;
    CommandContext context = fixture.Context();
    context.mode           = &cMode;

    fixture.buffer.InsertAtPoint("x");
    fixture.buffer.SetPoint(0);

    REQUIRE(dispatcher.Feed(ParseKeyChord("M-;"), context) == Dispatcher::Outcome::Invoked);
    REQUIRE(fixture.buffer.Text() == "// x");
}

TEST_CASE("org-set-tags requests a tags prompt on a headline and reports failure off one", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    Fixture        fixture;
    CommandContext context = fixture.Context();
    std::string    message;
    context.message = &message;

    fixture.buffer.InsertAtPoint("* Buy milk\n");
    fixture.buffer.SetPoint(2);

    registry.Invoke("org-set-tags", context);
    REQUIRE(context.interactiveRequest == InteractiveRequest::SetHeadlineTags);
    REQUIRE(message.empty());

    context.interactiveRequest = InteractiveRequest::None;
    fixture.buffer.SetPoint(fixture.buffer.Size());
    fixture.buffer.InsertAtPoint("plain text");
    fixture.buffer.SetPoint(fixture.buffer.Size() - 3); // inside "plain text", not a headline

    registry.Invoke("org-set-tags", context);
    REQUIRE(context.interactiveRequest == InteractiveRequest::None);
    REQUIRE(message == "Not on a headline.");
}

TEST_CASE("OrgMode binds C-c C-t/C-c C-c/C-c C-p/C-c C-q/TAB to the five org commands", "[Commands]") {
    const Mode mode = OrgMode();
    REQUIRE(mode.name == "org-mode");

    const auto todo = mode.keymap.Resolve(ParseKeySequence("C-c C-t"));
    REQUIRE(todo.result == Keymap::LookupResult::Match);
    REQUIRE(todo.commandName == "org-cycle-todo");

    const auto priority = mode.keymap.Resolve(ParseKeySequence("C-c C-p"));
    REQUIRE(priority.result == Keymap::LookupResult::Match);
    REQUIRE(priority.commandName == "org-cycle-priority");

    const auto checkbox = mode.keymap.Resolve(ParseKeySequence("C-c C-c"));
    REQUIRE(checkbox.result == Keymap::LookupResult::Match);
    REQUIRE(checkbox.commandName == "org-toggle-checkbox");

    const auto fold = mode.keymap.Resolve(ParseKeySequence("TAB"));
    REQUIRE(fold.result == Keymap::LookupResult::Match);
    REQUIRE(fold.commandName == "org-cycle");

    const auto tags = mode.keymap.Resolve(ParseKeySequence("C-c C-q"));
    REQUIRE(tags.result == Keymap::LookupResult::Match);
    REQUIRE(tags.commandName == "org-set-tags");
}

TEST_CASE("org-cycle falls back to realigning a table when point isn't on a headline", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    Fixture        fixture;
    CommandContext context = fixture.Context();
    std::string    message;
    context.message = &message;

    fixture.buffer.InsertAtPoint("| N | Age |\n|---|---|\n| Alice | 3 |\n");
    fixture.buffer.SetPoint(2); // inside the header row, not a headline

    registry.Invoke("org-cycle", context);
    REQUIRE(fixture.buffer.Text() == "| N     | Age |\n|-------+-----|\n| Alice | 3   |\n");
    REQUIRE(message.empty());
}

TEST_CASE("org-cycle reports failure when point is neither on a headline nor in a table", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    Fixture        fixture;
    CommandContext context = fixture.Context();
    std::string    message;
    context.message = &message;

    fixture.buffer.InsertAtPoint("plain text");
    fixture.buffer.SetPoint(0);

    registry.Invoke("org-cycle", context);
    REQUIRE(message == "Not on a headline.");
}

TEST_CASE("org-table-align realigns the table at point and reports failure off one", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    Fixture        fixture;
    CommandContext context = fixture.Context();
    std::string    message;
    context.message = &message;

    fixture.buffer.InsertAtPoint("| N | Age |\n|---|---|\n| Alice | 3 |\n");
    fixture.buffer.SetPoint(2);

    registry.Invoke("org-table-align", context);
    REQUIRE(fixture.buffer.Text() == "| N     | Age |\n|-------+-----|\n| Alice | 3   |\n");
    REQUIRE(message.empty());

    fixture.buffer.SetPoint(fixture.buffer.Size());
    fixture.buffer.InsertAtPoint("\nplain text");
    fixture.buffer.SetPoint(fixture.buffer.Size() - 3);

    registry.Invoke("org-table-align", context);
    REQUIRE(message == "Not in a table.");
}

TEST_CASE("markdown-table-align realigns a GFM table and reports failure off one", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    Fixture        fixture;
    CommandContext context = fixture.Context();
    std::string    message;
    context.message = &message;

    fixture.buffer.InsertAtPoint("| N | Age |\n|---|---|\n| Alice | 3 |\n");
    fixture.buffer.SetPoint(2);

    registry.Invoke("markdown-table-align", context);
    REQUIRE(fixture.buffer.Text() == "| N     | Age |\n|-------|-----|\n| Alice | 3   |\n");
    REQUIRE(message.empty());

    // TAB-fallback-outside-table follow-up: off a table, this falls through
    // to indent-for-tab-command's own body (no snippet trigger registered
    // here, so a literal tab) rather than reporting "Not in a table." --
    // unlike org-table-align above, which real Org's C-c ' /TAB-in-table
    // convention keeps a hard stop for.
    fixture.buffer.SetPoint(fixture.buffer.Size());
    fixture.buffer.InsertAtPoint("\nplain text");
    fixture.buffer.SetPoint(fixture.buffer.Size() - 3);

    registry.Invoke("markdown-table-align", context);
    REQUIRE(message.empty());
    REQUIRE(fixture.buffer.Text().ends_with("plain t\text"));
}

TEST_CASE("BuildDefaultGlobalKeymap binds C-c C-l to open-link-at-point", "[Commands]") {
    const Keymap keymap = BuildDefaultGlobalKeymap();

    const auto link = keymap.Resolve(ParseKeySequence("C-c C-l"));
    REQUIRE(link.result == Keymap::LookupResult::Match);
    REQUIRE(link.commandName == "open-link-at-point");
}

TEST_CASE("OrgMode additionally binds its own C-c C-o to open-link-at-point", "[Commands]") {
    const Mode mode = OrgMode();

    const auto link = mode.keymap.Resolve(ParseKeySequence("C-c C-o"));
    REQUIRE(link.result == Keymap::LookupResult::Match);
    REQUIRE(link.commandName == "open-link-at-point");
}

TEST_CASE("MarkdownMode binds TAB to markdown-table-align", "[Commands]") {
    const Mode mode = MarkdownMode();
    REQUIRE(mode.name == "markdown-mode");

    const auto tab = mode.keymap.Resolve(ParseKeySequence("TAB"));
    REQUIRE(tab.result == Keymap::LookupResult::Match);
    REQUIRE(tab.commandName == "markdown-table-align");
}

// --- Emacs-coverage follow-up -------------------------------------------

TEST_CASE("set-mark-command toggles: a second press in place deactivates the mark", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);
    Keymap     keymap = BuildDefaultGlobalKeymap();
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();
    std::string    message;
    context.message = &message;

    Type(dispatcher, context, "hello");
    REQUIRE(dispatcher.Feed(ParseKeyChord("C-SPC"), context) == Dispatcher::Outcome::Invoked);
    REQUIRE(fixture.buffer.HasMark());
    REQUIRE(fixture.buffer.Mark() == 5);
    REQUIRE(message == "Mark set");

    // Second press with point unmoved: Emacs' C-SPC C-SPC deactivation.
    dispatcher.Feed(ParseKeyChord("C-SPC"), context);
    REQUIRE_FALSE(fixture.buffer.HasMark());
    REQUIRE(message == "Mark deactivated");

    // Set, move, press again: re-anchors at point rather than clearing.
    dispatcher.Feed(ParseKeyChord("C-SPC"), context);
    dispatcher.Feed(ParseKeyChord("C-b"), context);
    dispatcher.Feed(ParseKeyChord("C-SPC"), context);
    REQUIRE(fixture.buffer.HasMark());
    REQUIRE(fixture.buffer.Mark() == 4);
}

TEST_CASE("keyboard-quit (C-g) deactivates the mark", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);
    Keymap     keymap = BuildDefaultGlobalKeymap();
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();

    Type(dispatcher, context, "abc");
    dispatcher.Feed(ParseKeyChord("C-SPC"), context);
    dispatcher.Feed(ParseKeyChord("C-b"), context);
    REQUIRE(fixture.buffer.HasMark());

    dispatcher.Feed(ParseKeyChord("C-g"), context);
    REQUIRE_FALSE(fixture.buffer.HasMark());
}

TEST_CASE("kill-word and backward-kill-word kill into the kill ring", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);
    Keymap     keymap = BuildDefaultGlobalKeymap();
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();

    Type(dispatcher, context, "hello world");
    fixture.buffer.SetPoint(0);

    dispatcher.Feed(ParseKeyChord("M-d"), context); // kill-word
    REQUIRE(fixture.buffer.Text() == " world");
    REQUIRE(fixture.killRing.Current() == "hello");

    // kill-append follow-up: a real, dispatched motion command in between
    // (not a raw buffer.SetPoint(), which wouldn't touch lastCommand the
    // way any actual keypress would) -- so the second kill below starts a
    // fresh kill-ring entry rather than appending onto the first.
    dispatcher.Feed(ParseKeyChord("C-e"), context);   // end-of-line
    dispatcher.Feed(ParseKeyChord("M-DEL"), context); // backward-kill-word
    REQUIRE(fixture.buffer.Text() == " ");
    REQUIRE(fixture.killRing.Current() == "world");
}

TEST_CASE("yank-pop replaces a just-yanked entry with the next-older one", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);
    Keymap     keymap = BuildDefaultGlobalKeymap();
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();

    fixture.killRing.Kill("older");
    fixture.killRing.Kill("newer");

    dispatcher.Feed(ParseKeyChord("C-y"), context); // yank "newer"
    REQUIRE(fixture.buffer.Text() == "newer");

    dispatcher.Feed(ParseKeyChord("M-y"), context); // yank-pop -> "older"
    REQUIRE(fixture.buffer.Text() == "older");
    REQUIRE(fixture.buffer.Point() == 5);
}

TEST_CASE("yank-pop refuses when the previous command was not a yank", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);
    Keymap     keymap = BuildDefaultGlobalKeymap();
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();
    std::string    message;
    context.message = &message;

    fixture.killRing.Kill("entry");
    Type(dispatcher, context, "text");

    dispatcher.Feed(ParseKeyChord("M-y"), context);
    REQUIRE(fixture.buffer.Text() == "text");
    REQUIRE(message == "Previous command was not a yank");
}

TEST_CASE("mark-whole-buffer puts point at the start and mark at the end", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);
    Keymap     keymap = BuildDefaultGlobalKeymap();
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();

    Type(dispatcher, context, "abc");
    dispatcher.Feed(ParseKeyChord("C-x"), context);
    dispatcher.Feed(ParseKeyChord("h"), context);
    REQUIRE(fixture.buffer.Point() == 0);
    REQUIRE(fixture.buffer.HasMark());
    REQUIRE(fixture.buffer.Mark() == 3);
}

TEST_CASE("transpose-chars swaps around point and at line end", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);
    Keymap     keymap = BuildDefaultGlobalKeymap();
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();

    Type(dispatcher, context, "ab");
    fixture.buffer.SetPoint(1);
    dispatcher.Feed(ParseKeyChord("C-t"), context);
    REQUIRE(fixture.buffer.Text() == "ba");
    REQUIRE(fixture.buffer.Point() == 2);

    // At line end: the two preceding graphemes swap, Emacs-style.
    dispatcher.Feed(ParseKeyChord("C-t"), context);
    REQUIRE(fixture.buffer.Text() == "ab");
    REQUIRE(fixture.buffer.Point() == 2);
}

TEST_CASE("transpose-chars undoes as a single step", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);
    Keymap     keymap = BuildDefaultGlobalKeymap();
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();

    Type(dispatcher, context, "ab");
    fixture.buffer.SetPoint(1);
    dispatcher.Feed(ParseKeyChord("C-t"), context);
    REQUIRE(fixture.buffer.Text() == "ba");

    dispatcher.Feed(ParseKeyChord("C-_"), context); // undo
    REQUIRE(fixture.buffer.Text() == "ab");
}

TEST_CASE("transpose-words swaps the words around point", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);
    Keymap     keymap = BuildDefaultGlobalKeymap();
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();

    Type(dispatcher, context, "foo bar");
    fixture.buffer.SetPoint(3);
    dispatcher.Feed(ParseKeyChord("M-t"), context);
    REQUIRE(fixture.buffer.Text() == "bar foo");
    REQUIRE(fixture.buffer.Point() == 7);
}

TEST_CASE("transpose-words is a no-op without two words before/after point", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);
    Keymap     keymap = BuildDefaultGlobalKeymap();
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();

    Type(dispatcher, context, "foo bar");
    fixture.buffer.SetPoint(0); // no word *before* point
    dispatcher.Feed(ParseKeyChord("M-t"), context);
    REQUIRE(fixture.buffer.Text() == "foo bar");
    REQUIRE(fixture.buffer.Point() == 0);
}

TEST_CASE("upcase/downcase/capitalize-word transform to the end of the word", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);
    Keymap     keymap = BuildDefaultGlobalKeymap();
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();

    Type(dispatcher, context, "hello world");
    fixture.buffer.SetPoint(0);

    dispatcher.Feed(ParseKeyChord("M-u"), context); // upcase-word
    REQUIRE(fixture.buffer.Text() == "HELLO world");
    REQUIRE(fixture.buffer.Point() == 5);

    dispatcher.Feed(ParseKeyChord("M-c"), context); // capitalize-word ("world" -> "World")
    REQUIRE(fixture.buffer.Text() == "HELLO World");
    REQUIRE(fixture.buffer.Point() == 11);

    fixture.buffer.SetPoint(0);
    dispatcher.Feed(ParseKeyChord("M-l"), context); // downcase-word
    REQUIRE(fixture.buffer.Text() == "hello World");
}

TEST_CASE("open-line inserts a newline after point, leaving point in place", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);
    Keymap     keymap = BuildDefaultGlobalKeymap();
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();

    Type(dispatcher, context, "ab");
    fixture.buffer.SetPoint(1);
    dispatcher.Feed(ParseKeyChord("C-o"), context);
    REQUIRE(fixture.buffer.Text() == "a\nb");
    REQUIRE(fixture.buffer.Point() == 1);
}

TEST_CASE("just-one-space collapses surrounding whitespace to one space", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);
    Keymap     keymap = BuildDefaultGlobalKeymap();
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();

    fixture.buffer.InsertAtPoint("a \t  b");
    fixture.buffer.SetPoint(3);
    dispatcher.Feed(ParseKeyChord("M-SPC"), context);
    REQUIRE(fixture.buffer.Text() == "a b");
    REQUIRE(fixture.buffer.Point() == 2);
}

TEST_CASE("delete-indentation joins this line to the previous with one space", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);
    Keymap     keymap = BuildDefaultGlobalKeymap();
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();

    fixture.buffer.InsertAtPoint("foo  \n   bar");
    fixture.buffer.SetPoint(8); // inside line 1's indentation
    dispatcher.Feed(ParseKeyChord("M-^"), context);
    REQUIRE(fixture.buffer.Text() == "foo bar");
    REQUIRE(fixture.buffer.Point() == 3);
}

TEST_CASE("back-to-indentation moves point to the first non-whitespace character", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);
    Keymap     keymap = BuildDefaultGlobalKeymap();
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();

    fixture.buffer.InsertAtPoint("  \tab");
    dispatcher.Feed(ParseKeyChord("M-m"), context);
    REQUIRE(fixture.buffer.Point() == 3);
}

TEST_CASE("delete-blank-lines collapses a blank run to one line", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);
    Keymap     keymap = BuildDefaultGlobalKeymap();
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();

    fixture.buffer.InsertAtPoint("a\n\n\n\nb");
    fixture.buffer.SetPoint(3); // on a blank line inside the run
    dispatcher.Feed(ParseKeyChord("C-x"), context);
    dispatcher.Feed(ParseKeyChord("C-o"), context);
    REQUIRE(fixture.buffer.Text() == "a\n\nb");
    REQUIRE(fixture.buffer.Point() == 2);
}

TEST_CASE("delete-blank-lines on a non-blank line deletes the following blank run", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);
    Keymap     keymap = BuildDefaultGlobalKeymap();
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();

    fixture.buffer.InsertAtPoint("a\n\n\nb");
    fixture.buffer.SetPoint(0);
    dispatcher.Feed(ParseKeyChord("C-x"), context);
    dispatcher.Feed(ParseKeyChord("C-o"), context);
    REQUIRE(fixture.buffer.Text() == "a\nb");
}

TEST_CASE("delete-blank-lines deletes an isolated blank line outright", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);
    Keymap     keymap = BuildDefaultGlobalKeymap();
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();

    fixture.buffer.InsertAtPoint("a\n\nb");
    fixture.buffer.SetPoint(2); // the lone blank line
    dispatcher.Feed(ParseKeyChord("C-x"), context);
    dispatcher.Feed(ParseKeyChord("C-o"), context);
    REQUIRE(fixture.buffer.Text() == "a\nb");
}

TEST_CASE("C-x u is bound to undo", "[Commands]") {
    const Keymap keymap = BuildDefaultGlobalKeymap();

    const auto lookup = keymap.Resolve(ParseKeySequence("C-x u"));
    REQUIRE(lookup.result == Keymap::LookupResult::Match);
    REQUIRE(lookup.commandName == "undo");
}

TEST_CASE("recenter and goto-line set their interactive requests", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);
    Keymap     keymap = BuildDefaultGlobalKeymap();
    Dispatcher dispatcher(registry, KeymapStack({&keymap}));

    Fixture        fixture;
    CommandContext context = fixture.Context();

    dispatcher.Feed(ParseKeyChord("C-l"), context);
    REQUIRE(context.interactiveRequest == InteractiveRequest::Recenter);

    context.interactiveRequest = InteractiveRequest::None;
    REQUIRE(dispatcher.Feed(ParseKeyChord("M-g"), context) == Dispatcher::Outcome::Pending);
    dispatcher.Feed(ParseKeyChord("g"), context);
    REQUIRE(context.interactiveRequest == InteractiveRequest::GotoLine);

    context.interactiveRequest = InteractiveRequest::None;
    dispatcher.Feed(ParseKeyChord("M-g"), context);
    dispatcher.Feed(ParseKeyChord("M-g"), context);
    REQUIRE(context.interactiveRequest == InteractiveRequest::GotoLine);
}

TEST_CASE("save-some-buffers saves every modified file-backed buffer", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "ned_commands_test_save_some.txt";
    {
        std::ofstream out(path);
        out << "before";
    }

    Fixture            fixture;
    ned::text::Buffer& fileBuffer = fixture.bufferList.OpenOrCreateFile(path);
    fileBuffer.SetPoint(fileBuffer.Size());
    fileBuffer.InsertAtPoint(" after");
    REQUIRE(fileBuffer.Modified());

    CommandContext context = fixture.Context();
    std::string    message;
    context.message = &message;
    registry.Invoke("save-some-buffers", context);

    REQUIRE_FALSE(fileBuffer.Modified());
    REQUIRE(message == "Saved 1 buffer");

    std::filesystem::remove(path);
}

// --- external-modification-safety follow-up ------------------------------

TEST_CASE("save-buffer asks before overwriting an externally-changed file; save-buffer-force writes", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "ned_commands_test_supersession.txt";
    {
        std::ofstream out(path);
        out << "original\n";
    }

    ned::text::Buffer     buffer = ned::text::Buffer::FromFile(path);
    ned::text::KillRing   killRing;
    ned::text::BufferList bufferList;
    CommandContext        context{buffer, killRing, bufferList};

    buffer.SetPoint(0);
    buffer.InsertAtPoint("mine ");

    // Someone else writes the file underneath the buffer (timestamp bumped
    // explicitly so the test never depends on mtime granularity).
    {
        std::ofstream out(path, std::ios::trunc);
        out << "theirs\n";
    }
    std::filesystem::last_write_time(path, std::filesystem::last_write_time(path) + std::chrono::seconds(2));

    registry.Invoke("save-buffer", context);
    REQUIRE(context.interactiveRequest == InteractiveRequest::ConfirmOverwriteSave);
    {
        std::ifstream in(path);
        std::string   onDisk((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        REQUIRE(onDisk == "theirs\n"); // nothing was written
    }

    registry.Invoke("save-buffer-force", context);
    REQUIRE_FALSE(buffer.Modified());
    {
        std::ifstream in(path);
        std::string   onDisk((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        REQUIRE(onDisk == buffer.Text());
    }

    // Back in agreement: a plain save-buffer no longer asks.
    buffer.InsertAtPoint("more");
    context.interactiveRequest = InteractiveRequest::None;
    registry.Invoke("save-buffer", context);
    REQUIRE(context.interactiveRequest == InteractiveRequest::None);
    REQUIRE_FALSE(buffer.Modified());

    std::filesystem::remove(path);
}

TEST_CASE("save-buffer asks before writing unresolved conflict markers; save-buffer-force writes", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_commands_test_conflict_save.txt";
    {
        std::ofstream out(path);
        out << "original\n";
    }

    ned::text::Buffer     buffer = ned::text::Buffer::FromFile(path);
    ned::text::KillRing   killRing;
    ned::text::BufferList bufferList;
    CommandContext        context{buffer, killRing, bufferList};

    buffer.SetPoint(0);
    buffer.InsertAtPoint("<<<<<<< buffer\nmine\n=======\ntheirs\n>>>>>>> disk\n");

    registry.Invoke("save-buffer", context);
    REQUIRE(context.interactiveRequest == InteractiveRequest::ConfirmSaveWithConflicts);
    {
        std::ifstream in(path);
        std::string   onDisk((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        REQUIRE(onDisk == "original\n"); // nothing was written
    }

    registry.Invoke("save-buffer-force", context);
    REQUIRE_FALSE(buffer.Modified());
    {
        std::ifstream in(path);
        std::string   onDisk((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        REQUIRE(onDisk == buffer.Text());
        REQUIRE(onDisk.find("<<<<<<<") != std::string::npos); // saved anyway, markers and all
    }

    std::filesystem::remove(path);
}

TEST_CASE("tab-move-left/right reorder the current buffer among the tabs, stopping at the edges", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    ned::text::KillRing   killRing;
    ned::text::BufferList list;
    ned::text::Buffer&    a = list.CreateBuffer("a");
    ned::text::Buffer&    b = list.CreateBuffer("b");
    ned::text::Buffer&    c = list.CreateBuffer("c");

    std::string    message;
    CommandContext context{b, killRing, list};
    context.message = &message;

    registry.Invoke("tab-move-right", context); // a,b,c -> a,c,b
    REQUIRE(list.Buffers()[1].get() == &c);
    REQUIRE(list.Buffers()[2].get() == &b);

    registry.Invoke("tab-move-right", context); // already rightmost -- stays, reports
    REQUIRE(list.Buffers()[2].get() == &b);
    REQUIRE_FALSE(message.empty());

    message.clear();
    registry.Invoke("tab-move-left", context); // a,c,b -> a,b,c
    registry.Invoke("tab-move-left", context); // a,b,c -> b,a,c
    REQUIRE(list.Buffers()[0].get() == &b);
    REQUIRE(list.Buffers()[1].get() == &a);

    registry.Invoke("tab-move-left", context); // already leftmost -- stays, reports
    REQUIRE(list.Buffers()[0].get() == &b);
    REQUIRE_FALSE(message.empty());
}

// -- backup-and-recovery follow-up: the save-path backup/autosave hooks ------

namespace {

// Mirrors InitFileTest.cpp's own EnvVarGuard exactly (each test file carries
// its own copy by convention) -- here it sandboxes XDG_STATE_HOME so backup
// versions land in a disposable directory, never the developer's real one.
class EnvVarGuard {
  public:
    EnvVarGuard(const char* name, const char* value) : name_(name) {
        if (const char* existing = std::getenv(name)) {
            hadPrevious_ = true;
            previous_    = existing;
        }
        if (value) {
            setenv(name, value, 1);
        }
        else {
            unsetenv(name);
        }
    }

    ~EnvVarGuard() {
        if (hadPrevious_) {
            setenv(name_.c_str(), previous_.c_str(), 1);
        }
        else {
            unsetenv(name_.c_str());
        }
    }

    EnvVarGuard(const EnvVarGuard&)            = delete;
    EnvVarGuard& operator=(const EnvVarGuard&) = delete;

  private:
    std::string name_;
    bool        hadPrevious_ = false;
    std::string previous_;
};

// One disposable backup sandbox per test (BackupTest.cpp's shape, pared to
// what these save-path tests need).
struct BackupHookSandbox {
    explicit BackupHookSandbox(const std::string& name) : root(std::filesystem::temp_directory_path() / name), stateGuard("XDG_STATE_HOME", (root / "state").c_str()),
                                                          dataGuard("XDG_DATA_HOME", (root / "data").c_str()), homeGuard("HOME", nullptr) {
        ResetBackupsForTesting();
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root / "work");
    }

    ~BackupHookSandbox() {
        ResetBackupsForTesting();
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }

    std::filesystem::path root;
    EnvVarGuard           stateGuard;
    EnvVarGuard           dataGuard;
    EnvVarGuard           homeGuard;
};

std::string ReadWholeFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// ProjectRoot() and the project-session-eligibility globals ned-init-project
// touches are all process-wide state (ProjectRoot.h/ProjectSession.h);
// restores both on scope exit even if a REQUIRE fails partway through,
// mirroring ProjectRootTest.cpp's own CurrentPathGuard.
struct ProjectRootGuard {
    ProjectRootGuard() : previous_(ProjectRoot()) {
    }
    ~ProjectRootGuard() {
        SetProjectRoot(previous_);
        ResetProjectSessionForTesting();
    }

  private:
    std::filesystem::path previous_;
};

} // namespace

TEST_CASE("save-buffer preserves the prior disk content as a backup version and drops the autosave", "[Commands]") {
    const BackupHookSandbox sandbox("ned_commands_test_backup_save");
    CommandRegistry         registry;
    RegisterBuiltinCommands(registry);

    const std::filesystem::path path = sandbox.root / "work" / "notes.txt";
    ned::text::Buffer           buffer("notes", ned::text::Rope("original"));
    buffer.SaveToFile(path); // disk now holds "original\n"

    // A crash-recovery autosave from the editing session that this save
    // makes obsolete.
    WriteAutoSave(path, "unsaved edits");
    REQUIRE(std::filesystem::exists(BackupDirectoryForFile(path) / "autosave"));

    buffer.SetPoint(buffer.Size());
    buffer.InsertAtPoint(" plus edits");

    ned::text::KillRing   killRing;
    ned::text::BufferList bufferList;
    std::string           message;
    CommandContext        context{buffer, killRing, bufferList, KeyChord{}, &message};
    registry.Invoke("save-buffer", context);

    REQUIRE(ReadWholeFile(path) == "original plus edits\n");
    const std::vector<BackupVersion> versions = ListBackupVersions(path);
    REQUIRE(versions.size() == 1);
    REQUIRE_FALSE(versions[0].isAutoSave); // the autosave is gone, only the version remains
    REQUIRE(ReadBackupVersion(versions[0].path) == "original\n");
}

TEST_CASE("save-buffer-force backs up externally-written content the buffer never saw", "[Commands]") {
    const BackupHookSandbox sandbox("ned_commands_test_backup_external");
    CommandRegistry         registry;
    RegisterBuiltinCommands(registry);

    const std::filesystem::path path = sandbox.root / "work" / "notes.txt";
    ned::text::Buffer           buffer("notes", ned::text::Rope("original"));
    buffer.SaveToFile(path);

    // Someone else rewrites the file underneath the buffer.
    std::ofstream(path, std::ios::trunc) << "external content";

    ned::text::KillRing   killRing;
    ned::text::BufferList bufferList;
    std::string           message;
    CommandContext        context{buffer, killRing, bufferList, KeyChord{}, &message};
    registry.Invoke("save-buffer-force", context);

    const std::vector<BackupVersion> versions = ListBackupVersions(path);
    REQUIRE(versions.size() == 1);
    REQUIRE(ReadBackupVersion(versions[0].path) == "external content");
}

TEST_CASE("save-buffer's first save of a new file creates no backup version", "[Commands]") {
    const BackupHookSandbox sandbox("ned_commands_test_backup_newfile");
    CommandRegistry         registry;
    RegisterBuiltinCommands(registry);

    const std::filesystem::path path   = sandbox.root / "work" / "brand-new.txt";
    ned::text::Buffer           buffer = ned::text::Buffer::NewFile(path);
    buffer.InsertAtPoint("first content");

    ned::text::KillRing   killRing;
    ned::text::BufferList bufferList;
    std::string           message;
    CommandContext        context{buffer, killRing, bufferList, KeyChord{}, &message};
    registry.Invoke("save-buffer", context);

    REQUIRE(std::filesystem::exists(path)); // the save itself happened
    REQUIRE(ListBackupVersions(path).empty());
}

TEST_CASE("save-some-buffers backs up each existing file it saves", "[Commands]") {
    const BackupHookSandbox sandbox("ned_commands_test_backup_some");
    CommandRegistry         registry;
    RegisterBuiltinCommands(registry);

    ned::text::KillRing   killRing;
    ned::text::BufferList bufferList;

    const std::filesystem::path pathA = sandbox.root / "work" / "a.txt";
    const std::filesystem::path pathB = sandbox.root / "work" / "b.txt";
    std::ofstream(pathA) << "a on disk";
    std::ofstream(pathB) << "b on disk";
    bufferList.OpenOrCreateFile(pathA).InsertAtPoint("edit ");
    bufferList.OpenOrCreateFile(pathB).InsertAtPoint("edit ");

    ned::text::Buffer buffer("driver"); // save-some-buffers works off bufferList, not context.buffer
    std::string       message;
    CommandContext    context{buffer, killRing, bufferList, KeyChord{}, &message};
    registry.Invoke("save-some-buffers", context);

    REQUIRE(ListBackupVersions(pathA).size() == 1);
    REQUIRE(ReadBackupVersion(ListBackupVersions(pathA)[0].path) == "a on disk");
    REQUIRE(ListBackupVersions(pathB).size() == 1);
    REQUIRE(ReadBackupVersion(ListBackupVersions(pathB)[0].path) == "b on disk");
}

TEST_CASE("saving a scratch-directory buffer creates no backup version", "[Commands]") {
    const BackupHookSandbox sandbox("ned_commands_test_backup_scratch");
    CommandRegistry         registry;
    RegisterBuiltinCommands(registry);

    const std::filesystem::path scratchDir = sandbox.root / "data" / "ned" / "scratches";
    std::filesystem::create_directories(scratchDir);
    const std::filesystem::path path = scratchDir / "todo.txt";
    std::ofstream(path) << "scratch on disk";

    ned::text::BufferList bufferList;
    ned::text::Buffer&    buffer = bufferList.OpenOrCreateFile(path);
    buffer.InsertAtPoint("edit ");

    ned::text::KillRing killRing;
    std::string         message;
    CommandContext      context{buffer, killRing, bufferList, KeyChord{}, &message};
    registry.Invoke("save-buffer", context);

    REQUIRE_FALSE(buffer.Modified()); // saved
    REQUIRE(ListBackupVersions(path).empty());
}

TEST_CASE("recover-file signals its interactive request", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    Fixture        fixture;
    CommandContext context = fixture.Context();
    registry.Invoke("recover-file", context);

    REQUIRE(context.interactiveRequest == InteractiveRequest::RecoverFile);
}

TEST_CASE("ned-init-project creates .ned/ and appends session.json to an existing .gitignore", "[Commands]") {
    const ProjectRootGuard      guard;
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "ned_commands_test_init_project";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    std::ofstream(root / ".gitignore") << "build/\n";
    SetProjectRoot(root);

    CommandRegistry registry;
    RegisterBuiltinCommands(registry);
    Fixture        fixture;
    CommandContext context = fixture.Context();
    std::string    message;
    context.message = &message;

    registry.Invoke("ned-init-project", context);

    REQUIRE(std::filesystem::is_directory(root / ".ned"));
    const std::string gitignore = ReadWholeFile(root / ".gitignore");
    REQUIRE(gitignore.find("build/") != std::string::npos);
    REQUIRE(gitignore.find(".ned/session.json") != std::string::npos);
    REQUIRE(message.find(".gitignore") != std::string::npos);

    std::filesystem::remove_all(root);
}

TEST_CASE("ned-init-project doesn't create a .gitignore that doesn't already exist", "[Commands]") {
    const ProjectRootGuard      guard;
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "ned_commands_test_init_project_nogi";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    SetProjectRoot(root);

    CommandRegistry registry;
    RegisterBuiltinCommands(registry);
    Fixture        fixture;
    CommandContext context = fixture.Context();
    std::string    message;
    context.message = &message;

    registry.Invoke("ned-init-project", context);

    REQUIRE(std::filesystem::is_directory(root / ".ned"));
    REQUIRE_FALSE(std::filesystem::exists(root / ".gitignore"));
    REQUIRE(message.find(".gitignore") == std::string::npos);

    std::filesystem::remove_all(root);
}

TEST_CASE("ned-init-project doesn't duplicate an existing session.json .gitignore entry", "[Commands]") {
    const ProjectRootGuard      guard;
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "ned_commands_test_init_project_dup";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    std::ofstream(root / ".gitignore") << "build/\n.ned/session.json\n";
    SetProjectRoot(root);

    CommandRegistry registry;
    RegisterBuiltinCommands(registry);
    Fixture        fixture;
    CommandContext context = fixture.Context();
    std::string    message;
    context.message = &message;

    registry.Invoke("ned-init-project", context);

    const std::string gitignore = ReadWholeFile(root / ".gitignore");
    const std::size_t first     = gitignore.find(".ned/session.json");
    REQUIRE(first != std::string::npos);
    REQUIRE(gitignore.find(".ned/session.json", first + 1) == std::string::npos);
    REQUIRE(message.find(".gitignore") == std::string::npos);

    std::filesystem::remove_all(root);
}

// --- Snippet trigger detection (snippet-expansion follow-up) --------------

namespace {

// The snippet registry is process-wide state (see Editor/SnippetRegistry.h)
// -- same RAII isolation FormatCommandGuard gives FormatCommand.
struct SnippetRegistryGuard {
    SnippetRegistryGuard() {
        ned::editor::ClearAllSnippets();
    }
    ~SnippetRegistryGuard() {
        ned::editor::ClearAllSnippets();
    }
};

} // namespace

TEST_CASE("indent-for-tab-command requests expansion of a registered trigger", "[Commands]") {
    const SnippetRegistryGuard guard;
    RegisterSnippet("", "for", "for (${1:i};)");
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);
    Fixture fixture;
    fixture.buffer.InsertAtPoint("for");
    CommandContext context = fixture.Context();
    registry.Invoke("indent-for-tab-command", context);
    REQUIRE(context.interactiveRequest == InteractiveRequest::SnippetExpand);
    REQUIRE(context.snippetExpansion.has_value());
    REQUIRE(context.snippetExpansion->replaceStart == 0);
    REQUIRE(context.snippetExpansion->replaceEnd == 3);
    REQUIRE(context.snippetExpansion->body == "for (${1:i};)");
    // The command itself mutates nothing -- BufferView performs the expansion.
    REQUIRE(fixture.buffer.Text() == "for");
}

TEST_CASE("indent-for-tab-command inserts a literal tab when nothing matches", "[Commands]") {
    const SnippetRegistryGuard guard;
    RegisterSnippet("", "for", "body");
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);
    Fixture fixture;
    fixture.buffer.InsertAtPoint("while");
    CommandContext context = fixture.Context();
    registry.Invoke("indent-for-tab-command", context);
    REQUIRE(context.interactiveRequest == InteractiveRequest::None);
    REQUIRE(fixture.buffer.Text() == "while\t");
}

TEST_CASE("A snippet trigger only fires with point exactly at the word's end", "[Commands]") {
    const SnippetRegistryGuard guard;
    RegisterSnippet("", "for", "body");
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);
    Fixture fixture;
    fixture.buffer.InsertAtPoint("for");
    fixture.buffer.SetPoint(2); // mid-word
    CommandContext context = fixture.Context();
    registry.Invoke("indent-for-tab-command", context);
    REQUIRE(context.interactiveRequest == InteractiveRequest::None);
    REQUIRE(fixture.buffer.Text() == "fo\tr");
}

TEST_CASE("Snippet trigger lookup keys on the buffer's mode language", "[Commands]") {
    const SnippetRegistryGuard guard;
    RegisterSnippet("cpp", "for", "cpp-body");
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    Fixture cppFixture;
    cppFixture.buffer.InsertAtPoint("for");
    Mode cppMode;
    cppMode.name              = "cpp-mode";
    CommandContext cppContext = cppFixture.Context();
    cppContext.mode           = &cppMode;
    registry.Invoke("indent-for-tab-command", cppContext);
    REQUIRE(cppContext.interactiveRequest == InteractiveRequest::SnippetExpand);
    REQUIRE(cppContext.snippetExpansion->body == "cpp-body");

    // A null mode (headless) sees only the "" global tier -- no cpp match.
    Fixture plainFixture;
    plainFixture.buffer.InsertAtPoint("for");
    CommandContext plainContext = plainFixture.Context();
    registry.Invoke("indent-for-tab-command", plainContext);
    REQUIRE(plainContext.interactiveRequest == InteractiveRequest::None);
    REQUIRE(plainFixture.buffer.Text() == "for\t");
}

TEST_CASE("expand-snippet reports when no trigger matches", "[Commands]") {
    const SnippetRegistryGuard guard;
    CommandRegistry            registry;
    RegisterBuiltinCommands(registry);
    Fixture fixture;
    fixture.buffer.InsertAtPoint("nomatch");
    std::string    message;
    CommandContext context = fixture.Context();
    context.message        = &message;
    registry.Invoke("expand-snippet", context);
    REQUIRE(context.interactiveRequest == InteractiveRequest::None);
    REQUIRE(message == "No snippet matches the word before point.");
    REQUIRE(fixture.buffer.Text() == "nomatch"); // no literal-tab fallback here
}

TEST_CASE("indent-for-tab-command reindents the current line in place for a mode with indentColumn configured",
          "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    Fixture        fixture;
    CommandContext context = fixture.Context();
    const Mode     cMode   = CMode();
    context.mode           = &cMode;

    // Well-formed/closed source -- an unclosed brace triggers tree-sitter's
    // own error recovery, which nests unpredictably and isn't what this
    // test means to exercise (Tests/IndentTest.cpp covers the algorithm
    // itself against real, well-formed C source).
    fixture.buffer.InsertAtPoint("int f(void) {\n\n}\n");
    fixture.buffer.SetPoint(14); // start of the blank line1, right after "int f(void) {\n"
    registry.Invoke("indent-for-tab-command", context);

    REQUIRE(fixture.buffer.Text() == "int f(void) {\n    \n}\n");
    REQUIRE(fixture.buffer.Point() == 18); // point lands at the new indent's end
}

TEST_CASE("indent-for-tab-command falls back to literal-tab/snippet behavior when indentColumn is unset",
          "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    Fixture        fixture;
    CommandContext context = fixture.Context(); // context.mode stays nullptr

    fixture.buffer.InsertAtPoint("int f(void) {\n");
    registry.Invoke("indent-for-tab-command", context);

    REQUIRE(fixture.buffer.Text() == "int f(void) {\n\t"); // unchanged, pre-existing literal-tab behavior
}

TEST_CASE("indent-for-tab-command falls back to a literal tab when point is past the line's leading whitespace",
          "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    Fixture        fixture;
    CommandContext context = fixture.Context();
    const Mode     cMode   = CMode();
    context.mode           = &cMode;

    fixture.buffer.InsertAtPoint("int f(void) {\n    return 0;");
    // Point is at the end of "return 0;", well past the line's own leading
    // whitespace -- TAB here must not silently reindent the line instead of
    // inserting, matching every mode without indentColumn configured.
    registry.Invoke("indent-for-tab-command", context);

    REQUIRE(fixture.buffer.Text() == "int f(void) {\n    return 0;\t");
}

TEST_CASE("newline electric-indents the new line for a mode with indentColumn configured", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    Fixture        fixture;
    CommandContext context = fixture.Context();
    const Mode     cMode   = CMode();
    context.mode           = &cMode;

    // Well-formed/closed source, point right after "{" -- an unclosed brace
    // triggers tree-sitter's own error recovery, which nests unpredictably
    // and isn't what this test means to exercise (Tests/IndentTest.cpp
    // covers the algorithm itself against real, well-formed C source).
    fixture.buffer.InsertAtPoint("int f(void) {\n}\n");
    fixture.buffer.SetPoint(13); // right after "{", before the existing "\n"
    registry.Invoke("newline", context);

    REQUIRE(fixture.buffer.Text() == "int f(void) {\n    \n}\n");
    REQUIRE(fixture.buffer.Point() == 18); // point lands at the new indent's end
}

TEST_CASE("newline stays a bare newline when indentColumn is unset", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    Fixture        fixture;
    CommandContext context = fixture.Context(); // context.mode stays nullptr

    fixture.buffer.InsertAtPoint("int f(void) {");
    registry.Invoke("newline", context);

    REQUIRE(fixture.buffer.Text() == "int f(void) {\n");
}

TEST_CASE("indent-region reindents the marked region as one undo step and requires a mark", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    Fixture        fixture;
    CommandContext context = fixture.Context();
    const Mode     cMode   = CMode();
    context.mode           = &cMode;

    std::string message;
    context.message = &message;

    // No mark -- reports and does nothing, matching kill-region's own
    // no-mark-no-op convention.
    fixture.buffer.InsertAtPoint("int f(void) {\nreturn 0;\n}\n");
    registry.Invoke("indent-region", context);
    REQUIRE(message == "No region selected.");
    REQUIRE(fixture.buffer.Text() == "int f(void) {\nreturn 0;\n}\n");

    fixture.buffer.SetPoint(0);
    fixture.buffer.SetMark(fixture.buffer.Text().size());
    registry.Invoke("indent-region", context);

    REQUIRE(fixture.buffer.Text() == "int f(void) {\n    return 0;\n}\n");
    REQUIRE_FALSE(fixture.buffer.HasMark());

    REQUIRE(fixture.buffer.CanUndo());
    fixture.buffer.Undo();
    REQUIRE(fixture.buffer.Text() == "int f(void) {\nreturn 0;\n}\n");
}

TEST_CASE("indent-buffer reindents the whole buffer and reports how many lines changed", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    Fixture        fixture;
    CommandContext context = fixture.Context();
    const Mode     cMode   = CMode();
    context.mode           = &cMode;
    std::string message;
    context.message = &message;

    fixture.buffer.InsertAtPoint("int f(void) {\nreturn 0;\n}\n");
    registry.Invoke("indent-buffer", context);

    REQUIRE(fixture.buffer.Text() == "int f(void) {\n    return 0;\n}\n");
    REQUIRE(message == "1 line(s) reindented.");
}

TEST_CASE("indent-region/indent-buffer report when no indent rules are configured for the mode", "[Commands]") {
    CommandRegistry registry;
    RegisterBuiltinCommands(registry);

    Fixture        fixture;
    CommandContext context = fixture.Context(); // context.mode stays nullptr
    std::string    message;
    context.message = &message;

    fixture.buffer.InsertAtPoint("anything\n");
    fixture.buffer.SetPoint(0);
    fixture.buffer.SetMark(fixture.buffer.Text().size());

    registry.Invoke("indent-region", context);
    REQUIRE(message == "No indent rules configured for this mode.");

    registry.Invoke("indent-buffer", context);
    REQUIRE(message == "No indent rules configured for this mode.");
    REQUIRE(fixture.buffer.Text() == "anything\n");
}
