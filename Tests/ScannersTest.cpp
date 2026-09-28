#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#include "Editor/BundledLanguages.h"
#include "Editor/Grammar/Languages.h"
#include "Editor/Grammar/Parser.h"
#include "Editor/Grammar/Tree.h"
#include "Editor/Languages/Scanners/Scanners.h"
#include "Editor/Parse/Abi.h"
#include "Editor/Parse/Node.h"
#include "Editor/Parse/Sexp.h"

using ned::editor::languages::scanners::FindBundledScanner;

namespace {

const ned::editor::parse::abi::LanguageData* Tables(const ned::editor::grammar::Language& language) {
    return language.Raw();
}

void NoLog(const ned::editor::parse::abi::LexerData*, const char*, ...) {
}

// A scanner's lexer over a plain ASCII string, for driving one Scan call
// directly.
struct StringLexer {
    ned::editor::parse::abi::LexerData data{};
    std::string_view                   text;
    std::size_t                        position = 0;

    explicit StringLexer(std::string_view source) : text(source) {
        data.advance = [](ned::editor::parse::abi::LexerData* lexer, bool) {
            auto* self = reinterpret_cast<StringLexer*>(lexer);
            if (self->position < self->text.size())
                ++self->position;
            self->Sync();
        };
        data.markEnd   = [](ned::editor::parse::abi::LexerData*) {};
        data.getColumn = [](ned::editor::parse::abi::LexerData* lexer) {
            const auto*       self      = reinterpret_cast<StringLexer*>(lexer);
            const std::size_t lineStart = self->text.rfind('\n', self->position == 0 ? 0 : self->position - 1);
            return static_cast<std::uint32_t>(lineStart == std::string_view::npos || self->position == 0
                                                  ? self->position
                                                  : self->position - lineStart - 1);
        };
        data.isAtIncludedRangeStart = [](const ned::editor::parse::abi::LexerData*) { return false; };
        data.eof                    = [](const ned::editor::parse::abi::LexerData* lexer) {
            const auto* self = reinterpret_cast<const StringLexer*>(lexer);
            return self->position >= self->text.size();
        };
        data.log        = NoLog;
        data.lookbehind = [](const ned::editor::parse::abi::LexerData* lexer) -> std::int32_t {
            const auto* self = reinterpret_cast<const StringLexer*>(lexer);
            return self->position == 0 ? '\n' : self->text[self->position - 1];
        };
        Sync();
    }

    void Sync() {
        data.lookahead = position < text.size() ? static_cast<unsigned char>(text[position]) : 0;
    }
};

} // namespace

// The generated parser.c files still linked reach their scanners through
// tree-sitter's C symbol names, which only the ported files define now
// (no scanner.c is compiled), so linking at all proves they run the ports;
// this holds the registry to the same set.
TEST_CASE("Every bundled grammar with external tokens has its scanner in the registry", "[Scanners]") {
    std::size_t withScanner = 0;
    for (const ned::editor::LanguageDefinition& definition : ned::editor::BundledLanguages()) {
        if (definition.grammarless)
            continue;
        const std::string grammar = definition.grammar.empty() ? definition.name : definition.grammar;
        if (grammar != definition.name)
            continue; // jank/clojure, tsx/typescript borrow a grammar; the grammar's own definition covers it
        INFO("language: " << definition.name);
        const auto language = ned::editor::grammar::LanguageByName(definition.name);
        REQUIRE(language.has_value());
        const auto* data    = Tables(*language);
        const auto* scanner = FindBundledScanner(definition.name);
        if (data->externalTokenCount == 0) {
            CHECK(scanner == nullptr);
            continue;
        }
        ++withScanner;
        REQUIRE(scanner != nullptr);
        CHECK(scanner->create != nullptr);
        CHECK(scanner->scan != nullptr);
        CHECK(data->externalScanner.scan != nullptr);
    }
    CHECK(withScanner == 82); // +nginx, awk, caddy, cue, jsonnet, pkl, wgsl
    CHECK(FindBundledScanner("no-such-language") == nullptr);
}

TEST_CASE("A scanner's state round-trips through its serialization buffer", "[Scanners]") {
    for (const char* name : {"bash", "python", "markdown", "yaml", "ruby"}) {
        INFO("language: " << name);
        const auto* scanner = FindBundledScanner(name);
        REQUIRE(scanner != nullptr);
        void* payload = scanner->create();
        REQUIRE(payload != nullptr);
        char           buffer[ned::editor::parse::abi::kSerializationBufferSize];
        const unsigned length = scanner->serialize(payload, buffer);
        CHECK(length <= sizeof(buffer));
        scanner->deserialize(payload, buffer, length);
        scanner->deserialize(payload, nullptr, 0);
        scanner->destroy(payload);
    }
}

// A heredoc line that is only "\" reaches the line-continuation branch after
// the terminator check found nothing at column 0; a markEnd that check left
// behind used to make the continuation zero-width, which the heredoc body's
// repetition then absorbed forever.
TEST_CASE("Crystal: a heredoc line holding only a line continuation is two bytes of body", "[Scanners]") {
    const auto language = ned::editor::grammar::LanguageByName("crystal");
    REQUIRE(language.has_value());
    const ned::editor::grammar::Parser parser(*language);

    const std::string                text = "x = <<-D\n\\\ny\nD\n";
    const ned::editor::grammar::Tree tree = parser.Parse(text);
    REQUIRE_FALSE(tree.IsNull());
    const std::string sexp = ned::editor::parse::SubtreeToSexp(tree.Green().Root(), tree.Green().Language());
    INFO(sexp);
    CHECK_FALSE(ned::editor::parse::NodeHasError(tree.RootNode().Raw()));
    CHECK(sexp.find("(heredoc_end)") != std::string::npos);
}

// indents[0] is the base level F#'s deserialize always restores; a DEDENT
// that popped it left the serialized state unchanged, so the same DEDENT was
// offered again in the same state forever. Every DEDENT must close a level
// the serialized state actually records.
TEST_CASE("F#: every DEDENT changes the scanner's serialized state", "[Scanners]") {
    const auto* scanner = FindBundledScanner("fsharp");
    REQUIRE(scanner != nullptr);
    constexpr unsigned kTokenCount = 21; // FsharpScanner.cpp's TokenType, through ERROR_SENTINEL
    constexpr unsigned kDedent     = 2;
    bool               valid[kTokenCount]{};
    valid[kDedent] = true;

    for (const char* text : {"then x", "and x", "with x", "else x", "elif x", "end x", ") x", "#if X\n"}) {
        INFO("text: " << text);
        void*          payload = scanner->create();
        char           before[ned::editor::parse::abi::kSerializationBufferSize];
        char           after[ned::editor::parse::abi::kSerializationBufferSize];
        const unsigned beforeLength = scanner->serialize(payload, before);

        StringLexer lexer(text);
        const bool  found = scanner->scan(payload, &lexer.data, valid);
        if (found && lexer.data.resultSymbol == kDedent) {
            const unsigned afterLength = scanner->serialize(payload, after);
            CHECK((afterLength != beforeLength || std::memcmp(before, after, beforeLength) != 0));
        }
        scanner->destroy(payload);
    }

    // One level open (serialized as no preprocessor indents, then indent 4):
    // closing it is a real DEDENT, and the state records the close.
    void*      payload     = scanner->create();
    const char indented[2] = {0, 4};
    scanner->deserialize(payload, indented, sizeof(indented));
    StringLexer lexer("then x");
    REQUIRE(scanner->scan(payload, &lexer.data, valid));
    CHECK(lexer.data.resultSymbol == kDedent);
    char           after[ned::editor::parse::abi::kSerializationBufferSize];
    const unsigned afterLength = scanner->serialize(payload, after);
    CHECK((afterLength != sizeof(indented) || std::memcmp(after, indented, afterLength) != 0));
    scanner->destroy(payload);
}
