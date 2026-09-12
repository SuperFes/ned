#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "Editor/BundledLanguages.h"
#include "Editor/CaptureClassifiers.h"
#include "Editor/LanguageDefinition.h"
#include "Editor/LanguageFiles.h"
#include "Editor/LanguageParse.h"
#include "Editor/SyntaxTheme.h"

using ned::editor::BundledLanguage;
using ned::editor::CaptureClassification;
using ned::editor::ClearCaptureClassifiers;
using ned::editor::HighlightSpan;
using ned::editor::HighlightWindow;
using ned::editor::LanguageDefinition;
using ned::editor::Mode;
using ned::editor::ModeFromDefinition;
using ned::editor::ParseLanguageDefinition;
using ned::editor::RegisterCaptureClassifier;
using ned::editor::SyntaxClass;

namespace {

// Registration is process-wide; every test clears after itself.
struct ClassifierGuard {
    ~ClassifierGuard() {
        ClearCaptureClassifiers();
    }
};

// A JSON-grammar definition whose captures are easy to hit: upstream's
// highlights capture a pair's key as "string.special.key".
LanguageDefinition JsonDefinition(const std::string& extra = {}) {
    return ParseLanguageDefinition("json", "{:name \"json\"" + extra + "}");
}

std::vector<HighlightSpan> Highlight(const Mode& mode, std::string_view text) {
    return mode.highlight(text, HighlightWindow{});
}

const HighlightSpan* SpanCovering(const std::vector<HighlightSpan>& spans, std::size_t offset, SyntaxClass cls) {
    for (const HighlightSpan& span : spans) {
        if (span.startByte <= offset && offset < span.endByte && span.syntaxClass == cls) {
            return &span;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("A classifier reclassifies a capture's spans from their text", "[CaptureClassifier]") {
    const ClassifierGuard guard;
    LanguageDefinition    definition = JsonDefinition();
    ned::editor::DiscoverQueryFiles(definition,
                                    [](std::string_view path) { return ned::editor::FindEmbeddedLanguageFile(path).has_value(); });

    // Keys named "todo" become TodoKeyword; every other key falls through.
    RegisterCaptureClassifier("json", "string.special.key",
                              [](std::span<const std::string_view> texts) {
                                  std::vector<CaptureClassification> out(texts.size());
                                  for (std::size_t i = 0; i < texts.size(); ++i) {
                                      if (texts[i] == "\"todo\"") {
                                          out[i] = CaptureClassification::Class(SyntaxClass::TodoKeyword);
                                      }
                                  }
                                  return out;
                              });

    const Mode        mode  = ModeFromDefinition(definition);
    const std::string text  = R"({"todo": 1, "other": 2})";
    const auto        spans = Highlight(mode, text);
    REQUIRE(SpanCovering(spans, 2, SyntaxClass::TodoKeyword) != nullptr); // "todo" reclassified
    REQUIRE(SpanCovering(spans, 13, SyntaxClass::String) != nullptr);     // "other" untouched
    REQUIRE(SpanCovering(spans, 13, SyntaxClass::TodoKeyword) == nullptr);
}

TEST_CASE("A classifier can suppress a span outright", "[CaptureClassifier]") {
    const ClassifierGuard guard;
    LanguageDefinition    definition = JsonDefinition();
    ned::editor::DiscoverQueryFiles(definition,
                                    [](std::string_view path) { return ned::editor::FindEmbeddedLanguageFile(path).has_value(); });

    RegisterCaptureClassifier("json", "string.special.key", [](std::span<const std::string_view> texts) {
        return std::vector<CaptureClassification>(texts.size(), CaptureClassification::Suppressed());
    });

    const Mode        mode  = ModeFromDefinition(definition);
    const std::string text  = R"({"key": 1})";
    const auto        spans = Highlight(mode, text);
    // The key's own capture is gone; a wrong-sized result would have left it.
    for (const HighlightSpan& span : spans) {
        REQUIRE_FALSE((span.startByte == 1 && span.endByte == 6 && span.captureId != ned::editor::kNoCapture &&
                       ned::editor::CaptureNameForId(span.captureId) == "string.special.key"));
    }
}

TEST_CASE("A wrong-sized classifier result is all-Fallthrough, not a crash or misalignment", "[CaptureClassifier]") {
    const ClassifierGuard guard;
    LanguageDefinition    definition = JsonDefinition();
    ned::editor::DiscoverQueryFiles(definition,
                                    [](std::string_view path) { return ned::editor::FindEmbeddedLanguageFile(path).has_value(); });

    RegisterCaptureClassifier("json", "string.special.key", [](std::span<const std::string_view>) {
        return std::vector<CaptureClassification>{CaptureClassification::Suppressed()}; // always size 1
    });

    const Mode        mode  = ModeFromDefinition(definition);
    const std::string text  = R"({"a": 1, "b": 2})";
    const auto        spans = Highlight(mode, text);
    REQUIRE(SpanCovering(spans, 2, SyntaxClass::String) != nullptr); // both keys still classified normally
    REQUIRE(SpanCovering(spans, 10, SyntaxClass::String) != nullptr);
}

TEST_CASE(":capture-spans :line-end widens a capture's span through its line", "[CaptureClassifier]") {
    const ClassifierGuard guard;
    LanguageDefinition    definition = JsonDefinition(" :capture-spans {\"string.special.key\" :line-end}");
    ned::editor::DiscoverQueryFiles(definition,
                                    [](std::string_view path) { return ned::editor::FindEmbeddedLanguageFile(path).has_value(); });

    const Mode        mode    = ModeFromDefinition(definition);
    const std::string text    = "{\"key\": 1,\n \"next\": 2}";
    const auto        spans   = Highlight(mode, text);
    bool              widened = false;
    for (const HighlightSpan& span : spans) {
        if (span.startByte == 1 && span.captureId != ned::editor::kNoCapture &&
            ned::editor::CaptureNameForId(span.captureId) == "string.special.key") {
            REQUIRE(span.endByte == 10); // through the end of line 1, not just the key
            widened = true;
        }
    }
    REQUIRE(widened);
    // The second line's key is widened to end-of-text (no trailing newline).
    for (const HighlightSpan& span : spans) {
        if (span.startByte == 12 && ned::editor::CaptureNameForId(span.captureId) == "string.special.key") {
            REQUIRE(span.endByte == text.size());
        }
    }
}

TEST_CASE("Classification sees the original range, widening happens after", "[CaptureClassifier]") {
    const ClassifierGuard guard;
    LanguageDefinition    definition = JsonDefinition(" :capture-spans {\"string.special.key\" :line-end}");
    ned::editor::DiscoverQueryFiles(definition,
                                    [](std::string_view path) { return ned::editor::FindEmbeddedLanguageFile(path).has_value(); });

    std::vector<std::string> seen;
    RegisterCaptureClassifier("json", "string.special.key", [&seen](std::span<const std::string_view> texts) {
        for (const std::string_view text : texts) {
            seen.emplace_back(text);
        }
        return std::vector<CaptureClassification>(texts.size());
    });

    const Mode mode = ModeFromDefinition(definition);
    (void)Highlight(mode, R"({"key": 1})");
    REQUIRE(seen == std::vector<std::string>{"\"key\""}); // the key's own text, not the widened line
}
