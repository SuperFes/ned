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

Mode ModeNamed(const std::string& name) {
    for (const LanguageDefinition& definition : BundledLanguages()) {
        if (definition.name == name) {
            return ModeFromDefinition(definition);
        }
    }
    FAIL("no bundled definition named " + name);
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

const std::string kDesktopEntry = "[Desktop Entry]\n"
                                  "Type=Application\n"
                                  "Name=Text Editor\n"
                                  "Name[es]=Editor de texto\n"
                                  "Exec=ned %F\n"
                                  "Terminal=false\n";

const std::string kGitignore = "# build output\n"
                               "build/\n"
                               "!build/keep.txt\n"
                               "*.o\n";

const std::string kNginx = "# main\n"
                           "worker_processes auto;\n"
                           "http {\n"
                           "    server {\n"
                           "        listen 80;\n"
                           "        location /api {\n"
                           "            proxy_read_timeout 60s;\n"
                           "        }\n"
                           "    }\n"
                           "}\n";

const std::string kApache = "# vhost\n"
                            "<VirtualHost *:443>\n"
                            "    ServerName example.com\n"
                            "    DocumentRoot \"/srv/www\"\n"
                            "    MadeUpDirective yes\n"
                            "</VirtualHost>\n";

} // namespace

// The group header is upstream's @markup.heading, which reaches a class only
// through the definition's own :capture-classes -- the rest is upstream's.
TEST_CASE("A .desktop entry highlights its group, keys and locales", "[ConfigFormats]") {
    const auto mode  = ModeNamed("desktop");
    const auto spans = mode.highlight(kDesktopEntry, ned::editor::HighlightWindow{});

    CHECK(HasSpanContaining(spans, kDesktopEntry.find("Desktop Entry"), SyntaxClass::Type));
    CHECK(HasSpanContaining(spans, kDesktopEntry.find("Exec"), SyntaxClass::Property));
    CHECK(HasSpanContaining(spans, kDesktopEntry.find("es]") , SyntaxClass::String));
    CHECK(HasSpanContaining(spans, kDesktopEntry.find("false"), SyntaxClass::ConstantBuiltin));
}

// ned's own query: what a reader needs picked out of a pattern is the parts
// that change what it matches.
TEST_CASE("A .gitignore highlights negation, wildcards and separators", "[ConfigFormats]") {
    const auto mode  = ModeNamed("gitignore");
    const auto spans = mode.highlight(kGitignore, ned::editor::HighlightWindow{});

    CHECK(HasSpanContaining(spans, kGitignore.find("# build output"), SyntaxClass::Comment));
    CHECK(HasSpanContaining(spans, kGitignore.find('!'), SyntaxClass::Operator));
    CHECK(HasSpanContaining(spans, kGitignore.find("*.o"), SyntaxClass::String));
    CHECK(HasSpanContaining(spans, kGitignore.find("build/") + 5, SyntaxClass::Punctuation));
}

TEST_CASE("An nginx config highlights its blocks, directives and values", "[ConfigFormats]") {
    const auto mode  = ModeNamed("nginx");
    const auto spans = mode.highlight(kNginx, ned::editor::HighlightWindow{});

    CHECK(HasSpanContaining(spans, kNginx.find("# main"), SyntaxClass::Comment));
    CHECK(HasSpanContaining(spans, kNginx.find("location"), SyntaxClass::Keyword));
    CHECK(HasSpanContaining(spans, kNginx.find("worker_processes"), SyntaxClass::Constant));
    CHECK(HasSpanContaining(spans, kNginx.find("60s"), SyntaxClass::Number));
}

// Upstream's query separates httpd's own directive list (@keyword) from
// anything else (@function), and it spells that list with an inline (?i)
// flag -- which only distinguishes the two once #match? honours the flag.
TEST_CASE("An apache config tells a real directive from an invented one", "[ConfigFormats]") {
    const auto mode  = ModeNamed("apacheconf");
    const auto spans = mode.highlight(kApache, ned::editor::HighlightWindow{});

    CHECK(HasSpanContaining(spans, kApache.find("# vhost"), SyntaxClass::Comment));
    CHECK(HasSpanContaining(spans, kApache.find("VirtualHost"), SyntaxClass::Keyword));
    CHECK(HasSpanContaining(spans, kApache.find("ServerName"), SyntaxClass::Keyword));
    CHECK(HasSpanContaining(spans, kApache.find("\"/srv/www\""), SyntaxClass::String));
    CHECK(HasSpanContaining(spans, kApache.find("MadeUpDirective"), SyntaxClass::Function));
}
