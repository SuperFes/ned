#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <string>
#include <thread>
#include <vector>

#include "Editor/CaptureClassifiers.h"
#include "Editor/LanguageFiles.h"
#include "Editor/LanguageParse.h"
#include "Janet/EditorBindings.h"
#include "JanetTestSupport.h"

using ned::editor::CaptureClassification;
using ned::editor::ClearCaptureClassifiers;
using ned::editor::FindCaptureClassifier;
using ned::editor::HighlightSpan;
using ned::editor::Mode;
using ned::editor::SyntaxClass;

namespace {

struct ClassifierGuard {
    ~ClassifierGuard() {
        ClearCaptureClassifiers();
    }
};

Mode JsonMode() {
    ned::editor::LanguageDefinition definition = ned::editor::ParseLanguageDefinition("json", "{:name \"json\"}");
    ned::editor::DiscoverQueryFiles(
        definition, [](std::string_view path) { return ned::editor::FindEmbeddedLanguageFile(path).has_value(); });
    return ned::editor::ModeFromDefinition(definition);
}

} // namespace

// The rooting landmine this build carries (Value.h's CAUTION: 3+ rooted
// values + janet_pcall corrupts) is exactly the shape a careless
// implementation of these bindings would hit -- several held functions,
// invoked repeatedly per repaint. Register several and hammer them.
TEST_CASE("Janet capture classifiers: keyword classifies, false suppresses, nil falls through -- and 3+ held "
          "functions survive repeated invocation",
          "[JanetCaptureClassifier]") {
    const ClassifierGuard    guard;
    ned::janet::Environment& env = ned_tests::TestEnvironment();
    ned::janet::InstallEditorBindings(env);

    env.DoString(R"((ned/register-capture-classifier "json" "string.special.key"
        (fn [text] (if (= text "\"todo\"") :todo-keyword nil))))");
    env.DoString(R"((ned/register-capture-classifier "json" "number"
        (fn [text] (if (= text "0") false :constant))))");
    env.DoString(R"((ned/register-capture-classifier "json" "constant.builtin"
        (fn [text] :done-keyword)))");

    const Mode        mode = JsonMode();
    const std::string text = R"({"todo": 1, "other": 0, "flag": true})";
    for (int round = 0; round < 50; ++round) {
        const std::vector<HighlightSpan> spans   = mode.highlight(text, ned::editor::HighlightWindow{});
        bool                             sawTodo = false, sawZero = false, sawTrue = false, sawOne = false;
        for (const HighlightSpan& span : spans) {
            if (span.startByte == 1) {
                REQUIRE(span.syntaxClass == SyntaxClass::TodoKeyword);
                sawTodo = true;
            }
            if (span.startByte == text.find('0')) {
                sawZero = true; // suppressed: must not appear
            }
            if (span.startByte == text.find('1')) {
                REQUIRE(span.syntaxClass == SyntaxClass::Constant);
                sawOne = true;
            }
            if (span.startByte == text.find("true")) {
                REQUIRE(span.syntaxClass == SyntaxClass::DoneKeyword);
                sawTrue = true;
            }
        }
        REQUIRE(sawTodo);
        REQUIRE(sawOne);
        REQUIRE(sawTrue);
        REQUIRE_FALSE(sawZero);
    }
}

TEST_CASE("A Janet classifier called off the Janet thread answers all-Fallthrough", "[JanetCaptureClassifier]") {
    const ClassifierGuard    guard;
    ned::janet::Environment& env = ned_tests::TestEnvironment();
    ned::janet::InstallEditorBindings(env);

    env.DoString(R"((ned/register-capture-classifier "json" "string.special.key" (fn [text] :todo-keyword)))");

    const ned::editor::CaptureClassifier classifier = FindCaptureClassifier("json", "string.special.key");
    REQUIRE(static_cast<bool>(classifier));

    const std::string_view             texts[] = {"\"todo\""};
    std::vector<CaptureClassification> offThread;
    std::thread([&] { offThread = classifier(texts); }).join();
    REQUIRE(offThread.size() == 1);
    REQUIRE(offThread[0].kind == CaptureClassification::Kind::Fallthrough);

    const std::vector<CaptureClassification> onThread = classifier(texts);
    REQUIRE(onThread.size() == 1);
    REQUIRE(onThread[0].kind == CaptureClassification::Kind::Classified);
    REQUIRE(onThread[0].cls == SyntaxClass::TodoKeyword);
}

TEST_CASE("A Janet classifier error surfaces as an exception naming the classifier", "[JanetCaptureClassifier]") {
    const ClassifierGuard    guard;
    ned::janet::Environment& env = ned_tests::TestEnvironment();
    ned::janet::InstallEditorBindings(env);

    env.DoString(R"((ned/register-capture-classifier "json" "string.special.key" (fn [text] (error "boom"))))");
    const ned::editor::CaptureClassifier classifier = FindCaptureClassifier("json", "string.special.key");
    const std::string_view               texts[]    = {"x"};
    REQUIRE_THROWS_WITH(classifier(texts), Catch::Matchers::ContainsSubstring("json/string.special.key"));
}
