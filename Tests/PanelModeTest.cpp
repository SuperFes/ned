#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "Editor/Key.h"
#include "Editor/Keymap.h"
#include "Editor/PanelMode.h"
#include "Editor/WhichKeySettings.h"
#include "Janet/EditorBindings.h"
#include "Janet/Environment.h"
#include "JanetTestSupport.h"
#include "UI/PanelKeys.h"

using Catch::Matchers::ContainsSubstring;
using ned::editor::ParseKeyChord;
using ned::editor::ParseKeySequence;

namespace {

struct PanelKeysGuard {
    ~PanelKeysGuard() {
        ned::editor::ResetPanelKeysForTesting();
    }
};

std::optional<std::string> Resolve(const ned::editor::PanelMode& mode, std::string_view keys) {
    const ned::editor::Keymap::Lookup lookup = ned::editor::PanelKeymap(mode).Resolve(ParseKeySequence(keys));
    if (lookup.result != ned::editor::Keymap::LookupResult::Match) {
        return std::nullopt;
    }
    return lookup.commandName;
}

} // namespace

TEST_CASE("Every panel mode's default keys parse, are unambiguous and name its own commands", "[PanelMode]") {
    for (const ned::editor::PanelMode& mode : ned::editor::PanelModes()) {
        INFO(mode.name);
        REQUIRE(ned::editor::FindPanelMode(mode.name) == &mode);
        const ned::editor::Keymap keymap = ned::editor::PanelKeymap(mode);
        CHECK(keymap.AmbiguousBindings().empty());
        for (const ned::editor::PanelCommand& command : mode.commands) {
            INFO(command.name);
            CHECK_FALSE(command.docstring.empty());
            if (!command.keys.empty()) {
                CHECK(Resolve(mode, command.keys) == std::string(command.name));
            }
        }
        CHECK(keymap.AllBindings().size() <= mode.commands.size());
    }
}

TEST_CASE("ned/define-panel-key rebinds, adds and unbinds a panel mode's keys", "[PanelMode][Janet]") {
    PanelKeysGuard           guard;
    ned::janet::Environment& env = ned_tests::TestEnvironment();
    ned::janet::InstallEditorBindings(env);
    const ned::editor::PanelMode& mode = ned::editor::RemotePanelMode();

    env.DoString(R"(
        (ned/define-panel-key "remote-panel-mode" "r" "remote-panel-rename")
        (ned/define-panel-key "remote-panel-mode" "R" "")
        (ned/define-panel-key "remote-panel-mode" "C-c d" "remote-panel-delete")
    )");
    CHECK(Resolve(mode, "r") == "remote-panel-rename");
    CHECK_FALSE(Resolve(mode, "R"));
    CHECK(Resolve(mode, "C-c d") == "remote-panel-delete");
    CHECK(Resolve(mode, "D") == "remote-panel-delete");

    CHECK_THROWS(env.DoString(R"((ned/define-panel-key "no-such-mode" "r" "remote-panel-rename"))"));
    CHECK_THROWS(env.DoString(R"((ned/define-panel-key "remote-panel-mode" "r" "save-buffer"))"));

    ned::editor::ResetPanelKeysForTesting();
    CHECK(Resolve(mode, "R") == "remote-panel-rename");
    CHECK_FALSE(Resolve(mode, "r"));
}

TEST_CASE("DefinePanelKey refuses an unknown mode, a foreign command and empty keys", "[PanelMode]") {
    PanelKeysGuard guard;
    CHECK_THROWS_AS(ned::editor::DefinePanelKey("files-panel-mode", "r", "remote-panel-rename"), std::invalid_argument);
    CHECK_THROWS_AS(ned::editor::DefinePanelKey("remote-panel-mode", "r", "kill-line"), std::invalid_argument);
    CHECK_THROWS_AS(ned::editor::DefinePanelKey("remote-panel-mode", "", "remote-panel-rename"), std::invalid_argument);
}

TEST_CASE("PanelKeys collects a prefix, hints its next keys and reports an unbound sequence", "[PanelMode]") {
    PanelKeysGuard guard;
    ned::editor::DefinePanelKey("remote-panel-mode", "C-c d", "remote-panel-delete");
    ned::editor::SetWhichKeyEnabled(true);

    ned::ui::PanelKeys                                keys(ned::editor::RemotePanelMode());
    std::string                                       message;
    std::vector<std::optional<ned::ui::WhichKeyHint>> hints;
    keys.SetOnMessage([&](std::string text) { message = std::move(text); });
    keys.SetOnHint([&](std::optional<ned::ui::WhichKeyHint> hint) { hints.push_back(std::move(hint)); });

    CHECK(keys.Feed(ParseKeyChord("g")) == "remote-panel-refresh");
    CHECK(hints.empty()); // nothing was showing, so nothing to hide

    CHECK_FALSE(keys.Feed(ParseKeyChord("C-c")));
    CHECK(message == "C-c-");
    REQUIRE(hints.size() == 1);
    REQUIRE(hints.back());
    CHECK(hints.back()->prefixLabel == "C-c-");
    REQUIRE(hints.back()->bindings.size() == 1);
    CHECK(hints.back()->bindings[0] == std::pair<std::string, std::string>{"d", "remote-panel-delete"});

    CHECK(keys.Feed(ParseKeyChord("d")) == "remote-panel-delete");
    REQUIRE(hints.size() == 2);
    CHECK_FALSE(hints.back());

    CHECK_FALSE(keys.Feed(ParseKeyChord("C-c")));
    CHECK_FALSE(keys.Feed(ParseKeyChord("z")));
    CHECK_THAT(message, ContainsSubstring("C-c z is undefined in Remote panel"));
    CHECK_FALSE(hints.back());

    CHECK_FALSE(keys.Feed(ParseKeyChord("C-c")));
    keys.Reset();
    CHECK_FALSE(hints.back());
    CHECK(keys.Feed(ParseKeyChord("x")) == "remote-panel-remove-root");
}
