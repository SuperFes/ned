#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "Editor/BundledLanguages.h"
#include "Editor/LanguageDefinition.h"
#include "Editor/Mode.h"

using ned::editor::BundledLanguages;
using ned::editor::HighlightSpan;
using ned::editor::LanguageDefinition;
using ned::editor::Mode;
using ned::editor::ModeFromDefinition;
using ned::editor::SyntaxClass;

namespace {

Mode SystemdMode() {
    for (const LanguageDefinition& definition : BundledLanguages()) {
        if (definition.name == "systemd") {
            return ModeFromDefinition(definition);
        }
    }
    FAIL("no bundled systemd definition");
    return {};
}

bool HasSpanContaining(const std::vector<HighlightSpan>& spans, std::size_t offset, SyntaxClass cls) {
    for (const HighlightSpan& span : spans) {
        if (span.startByte <= offset && offset < span.endByte && span.syntaxClass == cls) {
            return true;
        }
    }
    return false;
}

const std::string kUnit = "[Unit]\n"
                          "Description=Backup %i\n"
                          "# a comment\n"
                          "\n"
                          "[Service]\n"
                          "Restart=on-failure\n"
                          "RemainAfterExit=yes\n"
                          "TimeoutStartSec=90s\n"
                          "Environment=\"LOG=/var/log/app.log\"\n"
                          "ExecStart=/usr/bin/backup $HOME \\\n"
                          "    --verbose\n";

} // namespace

// The queries are ned's own (the grammar ships none), so what each node is
// worth is only recorded here.
TEST_CASE("A unit file highlights its sections, directives and specifiers", "[Systemd]") {
    const auto mode  = SystemdMode();
    const auto spans = mode.highlight(kUnit, ned::editor::HighlightWindow{});

    CHECK(HasSpanContaining(spans, kUnit.find("Unit]"), SyntaxClass::Type));
    CHECK(HasSpanContaining(spans, kUnit.find("Description"), SyntaxClass::Property));
    CHECK(HasSpanContaining(spans, kUnit.find("%i"), SyntaxClass::Constant));
    CHECK(HasSpanContaining(spans, kUnit.find("# a comment"), SyntaxClass::Comment));
    CHECK(HasSpanContaining(spans, kUnit.find("yes"), SyntaxClass::ConstantBuiltin));
    CHECK(HasSpanContaining(spans, kUnit.find("90s"), SyntaxClass::Number));
    CHECK(HasSpanContaining(spans, kUnit.find("\"LOG="), SyntaxClass::String));
    CHECK(HasSpanContaining(spans, kUnit.find("$HOME"), SyntaxClass::Constant));
}

// "on-failure" and "off-by-one" start with a boolean spelling; the value
// tokens carry no precedence, so the longest match keeps them one word.
TEST_CASE("A value that merely starts with a boolean is not half-coloured", "[Systemd]") {
    const auto mode  = SystemdMode();
    const auto spans = mode.highlight(kUnit, ned::editor::HighlightWindow{});

    CHECK_FALSE(HasSpanContaining(spans, kUnit.find("on-failure"), SyntaxClass::ConstantBuiltin));
}
