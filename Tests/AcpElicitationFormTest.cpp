//
// UI/AcpPanel/ElicitationForm.h -- an agent's structured question as a
// keyboard form.
//

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "Editor/Key.h"
#include "UI/AcpPanel/ElicitationForm.h"

using ned::editor::acp::Json;
using ned::ui::acppanel::ElicitationForm;
using Result = ElicitationForm::KeyResult;

namespace {

ned::editor::KeyChord Key(char32_t codepoint) {
    ned::editor::KeyChord chord;
    chord.Codepoint = codepoint;
    return chord;
}

ned::editor::KeyChord Special(ned::editor::SpecialKey key, bool shift = false, bool control = false) {
    ned::editor::KeyChord chord;
    chord.Special = key;
    chord.Shift   = shift;
    chord.Control = control;
    return chord;
}

// What claude-agent-acp sends for a one-question AskUserQuestion.
Json AskOneQuestion() {
    return {{"type", "object"},
            {"properties",
             {{"question_0",
               {{"type", "string"},
                {"title", "Approach"},
                {"oneOf",
                 Json::array({Json{{"const", "Refactor"}, {"title", "Refactor"}, {"description", "Clean it up first"}},
                              Json{{"const", "Patch"}, {"title", "Patch"}}})}}},
              {"question_0_custom", {{"type", "string"}, {"title", "Other"}}}}}};
}

} // namespace

TEST_CASE("ElicitationForm picks an option by number and sends from the last field", "[AcpElicitation]") {
    ElicitationForm form("How should we fix it?", AskOneQuestion());
    REQUIRE(form.Fields().size() == 2);
    REQUIRE(form.Fields()[0].kind == ElicitationForm::FieldKind::Choice);

    REQUIRE(form.HandleKey(Key(U'2')) == Result::Handled);
    REQUIRE(form.CurrentField() == 1);
    REQUIRE(form.HandleKey(Special(ned::editor::SpecialKey::Enter)) == Result::Submit);

    std::string problem;
    REQUIRE(form.Content(problem) == Json{{"question_0", "Patch"}});
    REQUIRE(form.Summary() == "Patch");
}

TEST_CASE("ElicitationForm types into a text field and moves between fields with Tab", "[AcpElicitation]") {
    ElicitationForm form("How should we fix it?", AskOneQuestion());
    form.HandleKey(Special(ned::editor::SpecialKey::Tab));
    for (const char32_t c : std::u32string(U"both 1")) {
        form.HandleKey(Key(c));
    }
    form.HandleKey(Special(ned::editor::SpecialKey::Backspace));
    form.HandleKey(Special(ned::editor::SpecialKey::Tab, true));
    form.HandleKey(Special(ned::editor::SpecialKey::Down));
    form.HandleKey(Key(U' '));

    std::string problem;
    REQUIRE(form.Content(problem) == Json{{"question_0", "Patch"}, {"question_0_custom", "both "}});
    REQUIRE(form.HandleKey(Special(ned::editor::SpecialKey::Enter, false, true)) == Result::Submit);
    REQUIRE(form.HandleKey(Special(ned::editor::SpecialKey::Escape)) == Result::Decline);
}

TEST_CASE("ElicitationForm checks multiple choices and won't send without a required answer", "[AcpElicitation]") {
    const Json      schema = {{"type", "object"},
                              {"required", Json::array({"name"})},
                              {"properties",
                               {{"langs", {{"type", "array"}, {"items", {{"type", "string"}, {"enum", Json::array({"C++", "Janet", "Lua"})}}}}},
                                {"name", {{"type", "string"}}},
                                {"count", {{"type", "integer"}, {"default", 2}}},
                                {"sure", {{"type", "boolean"}, {"default", true}}}}}};
    ElicitationForm form("Setup", schema);
    // Properties come back in key order: count, langs, name, sure.
    REQUIRE(form.Fields()[1].key == "langs");

    std::string problem;
    REQUIRE_FALSE(form.Content(problem));
    REQUIRE(problem == "Answer \"name\" first.");
    REQUIRE(form.CurrentField() == 2);

    for (const char32_t c : std::u32string(U"ned")) {
        form.HandleKey(Key(c));
    }
    form.HandleKey(Special(ned::editor::SpecialKey::Tab, true));
    form.HandleKey(Key(U'1'));
    form.HandleKey(Key(U'3'));
    REQUIRE(form.Content(problem) == Json{{"count", 2}, {"langs", Json::array({"C++", "Lua"})}, {"name", "ned"}, {"sure", true}});

    const auto lines = form.Format(20);
    REQUIRE(lines.front().text == "? Setup");
    bool sawChecked = false;
    for (const auto& line : lines) {
        sawChecked = sawChecked || line.text.find("☑ Lua") != std::string::npos;
    }
    REQUIRE(sawChecked);
}

TEST_CASE("ElicitationForm rejects a number it can't read", "[AcpElicitation]") {
    ElicitationForm form("Size", Json{{"type", "object"}, {"properties", {{"n", {{"type", "number"}}}}}});
    form.HandleKey(Key(U'x'));
    std::string problem;
    REQUIRE_FALSE(form.Content(problem));
    REQUIRE(problem == "\"n\" needs a number.");
}

TEST_CASE("ElicitationForm puts fields in the given order, unnamed ones last", "[AcpPanel]") {
    const Json               schema = {{"type", "object"},
                                       {"properties", {{"a", {{"type", "string"}}}, {"b", {{"type", "string"}}}, {"c", {{"type", "string"}}}, {"d", {{"type", "string"}}}}}};
    const ElicitationForm    form("Q", schema, {"c", "a"});
    std::vector<std::string> keys;
    for (const auto& field : form.Fields()) {
        keys.push_back(field.key);
    }
    REQUIRE(keys == std::vector<std::string>{"c", "a", "b", "d"});
}
