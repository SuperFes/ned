#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <vector>

#include "Editor/LocalScopes.h"

using ned::editor::LocalCapture;
using ned::editor::LocalCaptureKind;
using ned::editor::LocalCaptureKindFromCaptureName;
using ned::editor::locals::LocalBinding;
using ned::editor::locals::Range;
using ned::editor::locals::ResolveBindingAt;

namespace {

// Captures are built against real text by searching for a substring
// occurrence rather than by hand-counting offsets -- the tests below are
// about scoping, and a miscounted offset would fail them for the wrong
// reason.
std::size_t Nth(std::string_view text, std::string_view needle, int n) {
    std::size_t at = 0;
    for (int i = 0; i <= n; ++i) {
        at = text.find(needle, i == 0 ? 0 : at + 1);
        REQUIRE(at != std::string_view::npos);
    }
    return at;
}

// The n-th '{' and the '}' that actually MATCHES it -- a first-'}' search
// silently makes an outer scope stop at an inner block's close, which is
// precisely the containment these tests assert about.
LocalCapture BraceScope(std::string_view text, int n = 0) {
    const std::size_t start = Nth(text, "{", n);
    int               depth = 0;
    for (std::size_t i = start; i < text.size(); ++i) {
        if (text[i] == '{') {
            ++depth;
        }
        else if (text[i] == '}' && --depth == 0) {
            return LocalCapture{start, i + 1, LocalCaptureKind::Scope, {}};
        }
    }
    FAIL("unbalanced braces in test fixture");
    return LocalCapture{start, text.size(), LocalCaptureKind::Scope, {}};
}

// A scope running from `from` to the '}' that MATCHES the next '{' after it
// -- a first-'}' search silently makes an outer scope stop at an inner
// block's close, which is precisely the containment these tests assert
// about. `from` exists because a function's scope has to cover its own
// PARAMETER LIST, not just its body: capturing the body alone leaves every
// parameter outside every scope, i.e. looking file-level. Real locals.scm
// queries capture the whole function node for the same reason.
LocalCapture ScopeFrom(std::string_view text, std::string_view from) {
    const std::size_t at = text.find(from);
    REQUIRE(at != std::string_view::npos);
    LocalCapture scope = BraceScope(text.substr(at));
    return LocalCapture{at, scope.endByte + at, LocalCaptureKind::Scope, {}};
}

LocalCapture Definition(std::string_view text, std::string_view name, int n = 0, std::string qualifier = {}) {
    const std::size_t at = Nth(text, name, n);
    return LocalCapture{at, at + name.size(), LocalCaptureKind::Definition, std::move(qualifier)};
}

LocalCapture Reference(std::string_view text, std::string_view name, int n = 0) {
    const std::size_t at = Nth(text, name, n);
    return LocalCapture{at, at + name.size(), LocalCaptureKind::Reference, {}};
}

bool HasOccurrenceAt(const LocalBinding& binding, std::size_t offset) {
    for (const Range& range : binding.occurrences) {
        if (range.first == offset) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("LocalCaptureKindFromCaptureName maps the three capture kinds", "[LocalScopes]") {
    std::string qualifier = "stale";
    REQUIRE(LocalCaptureKindFromCaptureName("local.scope", &qualifier) == LocalCaptureKind::Scope);
    REQUIRE(qualifier.empty());
    REQUIRE(LocalCaptureKindFromCaptureName("local.reference") == LocalCaptureKind::Reference);
    REQUIRE(LocalCaptureKindFromCaptureName("local.definition") == LocalCaptureKind::Definition);

    REQUIRE(LocalCaptureKindFromCaptureName("local.definition.parameter", &qualifier) == LocalCaptureKind::Definition);
    REQUIRE(qualifier == "parameter");

    // A dotted qualifier can itself be dotted; everything after the first
    // "local.definition." is the qualifier.
    REQUIRE(LocalCaptureKindFromCaptureName("local.definition.var.builtin", &qualifier) == LocalCaptureKind::Definition);
    REQUIRE(qualifier == "var.builtin");

    REQUIRE_FALSE(LocalCaptureKindFromCaptureName("definition.function").has_value());
    REQUIRE_FALSE(LocalCaptureKindFromCaptureName("_macro").has_value());
    // Not a definition with a qualifier of "s" -- the '.' has to be there.
    REQUIRE_FALSE(LocalCaptureKindFromCaptureName("local.definitions").has_value());
}

TEST_CASE("ResolveBindingAt resolves a parameter to its own occurrences only", "[LocalScopes]") {
    const std::string               text = "int size;\nvoid f(int size) { return size + size; }\n";
    const std::vector<LocalCapture> captures{
        ScopeFrom(text, "void f"),                // the whole function, parameter list included
        Definition(text, "size", 0),              // the file-level int size;
        Definition(text, "size", 1, "parameter"), // the parameter
        Reference(text, "size", 2),
        Reference(text, "size", 3),
    };

    const auto binding = ResolveBindingAt(captures, text, Nth(text, "size", 2));
    REQUIRE(binding.has_value());
    REQUIRE(binding->name == "size");
    REQUIRE(binding->qualifier == "parameter");
    REQUIRE_FALSE(binding->scopeIsFile);
    REQUIRE(binding->definition.first == Nth(text, "size", 1));
    REQUIRE(binding->occurrences.size() == 3);
    REQUIRE_FALSE(HasOccurrenceAt(*binding, Nth(text, "size", 0))); // the file-level one is a different binding
    REQUIRE_FALSE(binding->usedBeforeDefinition);
}

TEST_CASE("ResolveBindingAt reports a file-level binding as such", "[LocalScopes]") {
    const std::string               text = "int total;\nvoid f() { total = 1; }\n";
    const std::vector<LocalCapture> captures{
        BraceScope(text),
        Definition(text, "total", 0),
        Reference(text, "total", 1),
    };

    const auto binding = ResolveBindingAt(captures, text, Nth(text, "total", 0));
    REQUIRE(binding.has_value());
    REQUIRE(binding->scopeIsFile);
    REQUIRE_FALSE(binding->scope.has_value());
    REQUIRE(binding->occurrences.size() == 2);
}

TEST_CASE("ResolveBindingAt keeps a shadowed inner binding separate from its outer one", "[LocalScopes]") {
    const std::string               text = "void f() {\n  int x = 1;\n  {\n    int x = 2;\n    use(x);\n  }\n  use(x);\n}\n";
    const std::vector<LocalCapture> captures{
        BraceScope(text, 0), // the whole function body
        BraceScope(text, 1), // the inner block
        Definition(text, "x", 0),
        Definition(text, "x", 1),
        Reference(text, "x", 2),
        Reference(text, "x", 3),
    };

    SECTION("point on the inner definition sees only the inner block") {
        const auto binding = ResolveBindingAt(captures, text, Nth(text, "x", 1));
        REQUIRE(binding.has_value());
        REQUIRE(binding->occurrences.size() == 2);
        REQUIRE(HasOccurrenceAt(*binding, Nth(text, "x", 1)));
        REQUIRE(HasOccurrenceAt(*binding, Nth(text, "x", 2)));
        REQUIRE_FALSE(binding->usedBeforeDefinition);
    }
    SECTION("point on the outer definition skips everything the inner one shadows") {
        const auto binding = ResolveBindingAt(captures, text, Nth(text, "x", 0));
        REQUIRE(binding.has_value());
        REQUIRE(binding->occurrences.size() == 2);
        REQUIRE(HasOccurrenceAt(*binding, Nth(text, "x", 0)));
        REQUIRE(HasOccurrenceAt(*binding, Nth(text, "x", 3)));
        REQUIRE_FALSE(binding->usedBeforeDefinition);
    }
}

TEST_CASE("ResolveBindingAt flags a use that precedes its own definition in scope", "[LocalScopes]") {
    // The whole-scope-binding case (Python/JS `var`): use(x) really binds to
    // the local x below it, but a position-ordered walk resolves it to the
    // file-level one. The occurrence is missed, and saying so is the point.
    const std::string               text = "x = 0\ndef f():\n    use(x)\n    x = 2\n    use(x)\n";
    const std::vector<LocalCapture> captures{
        LocalCapture{text.find("def f():"), text.size(), LocalCaptureKind::Scope, {}},
        Definition(text, "x", 0),
        Reference(text, "x", 1),
        Definition(text, "x", 2),
        Reference(text, "x", 3),
    };

    const auto binding = ResolveBindingAt(captures, text, Nth(text, "x", 2));
    REQUIRE(binding.has_value());
    REQUIRE_FALSE(binding->scopeIsFile);
    REQUIRE(binding->usedBeforeDefinition);
    REQUIRE(binding->occurrences.size() == 2); // the definition and the use AFTER it
}

TEST_CASE("ResolveBindingAt binds a preceding use when nothing else binds the name", "[LocalScopes]") {
    // The comprehension shape: `n` is read twice before it is bound, and no
    // outer binding of `n` exists to confuse it with -- so the position rule
    // is skipped and all three occurrences belong together.
    const std::string               text = "squares = [n * n for n in xs]\n";
    const std::vector<LocalCapture> captures{
        LocalCapture{text.find('['), text.find(']') + 1, LocalCaptureKind::Scope, {}},
        Reference(text, "n", 0),
        Reference(text, "n", 1),
        Definition(text, "n", 2),
    };

    const auto binding = ResolveBindingAt(captures, text, Nth(text, "n", 0));
    REQUIRE(binding.has_value());
    REQUIRE(binding->occurrences.size() == 3);
    REQUIRE_FALSE(binding->usedBeforeDefinition);
}

TEST_CASE("ResolveBindingAt does not flag an independent earlier inner scope", "[LocalScopes]") {
    // An inner block binding its own x before the outer definition is not
    // the position rule biting -- it resolves INWARD, which is ordinary
    // shadowing, not a missed occurrence.
    const std::string               text = "void f() {\n  {\n    int x = 2;\n    use(x);\n  }\n  int x = 1;\n  use(x);\n}\n";
    const std::vector<LocalCapture> captures{
        BraceScope(text, 0),
        BraceScope(text, 1),
        Definition(text, "x", 0),
        Reference(text, "x", 1),
        Definition(text, "x", 2),
        Reference(text, "x", 3),
    };

    const auto binding = ResolveBindingAt(captures, text, Nth(text, "x", 2));
    REQUIRE(binding.has_value());
    REQUIRE_FALSE(binding->usedBeforeDefinition);
    REQUIRE(binding->occurrences.size() == 2);
}

TEST_CASE("ResolveBindingAt groups repeated definitions of one binding", "[LocalScopes]") {
    const std::string               text = "import sys\ndef f():\n    x = 1\n    x = 2\n    use(x)\n";
    const std::vector<LocalCapture> captures{
        LocalCapture{0, text.size(), LocalCaptureKind::Scope, {}}, // whole-buffer: not a real scope
        LocalCapture{text.find("def f():"), text.size(), LocalCaptureKind::Scope, {}},
        Definition(text, "x", 0),
        Definition(text, "x", 1),
        Reference(text, "x", 2),
    };

    const auto binding = ResolveBindingAt(captures, text, Nth(text, "x", 2));
    REQUIRE(binding.has_value());
    REQUIRE_FALSE(binding->scopeIsFile);                     // the whole-buffer capture was discarded, the def-scope one wasn't
    REQUIRE(binding->definition.first == Nth(text, "x", 0)); // the FIRST definition is canonical
    REQUIRE(binding->occurrences.size() == 3);
}

TEST_CASE("ResolveBindingAt accepts point at either edge of a token", "[LocalScopes]") {
    const std::string               text = "void f(int nn) { return nn; }\n";
    const std::vector<LocalCapture> captures{
        ScopeFrom(text, "void f"),
        Definition(text, "nn", 0),
        Reference(text, "nn", 1),
    };
    const std::size_t start = Nth(text, "nn", 1);

    for (const std::size_t point : {start, start + 1, start + 2}) {
        const auto binding = ResolveBindingAt(captures, text, point);
        REQUIRE(binding.has_value());
        REQUIRE(binding->occurrences.size() == 2);
    }
}

TEST_CASE("ResolveBindingAt declines what it cannot resolve", "[LocalScopes]") {
    const std::string text = "void f(int count) { return count; }\n";

    SECTION("no capture covers point") {
        const std::vector<LocalCapture> captures{ScopeFrom(text, "void f"), Definition(text, "count", 0)};
        REQUIRE_FALSE(ResolveBindingAt(captures, text, 0).has_value());
    }
    SECTION("a reference nothing in this file binds") {
        const std::vector<LocalCapture> captures{ScopeFrom(text, "void f"), Reference(text, "count", 1)};
        REQUIRE_FALSE(ResolveBindingAt(captures, text, Nth(text, "count", 1)).has_value());
    }
    SECTION("a capture whose text is not a plain identifier") {
        const std::vector<LocalCapture> captures{
            LocalCapture{0, text.size(), LocalCaptureKind::Definition, {}},
        };
        REQUIRE_FALSE(ResolveBindingAt(captures, text, 3).has_value());
    }
    SECTION("an out-of-range capture is ignored, not fatal") {
        const std::vector<LocalCapture> captures{
            LocalCapture{text.size() + 10, text.size() + 20, LocalCaptureKind::Definition, {}},
        };
        REQUIRE_FALSE(ResolveBindingAt(captures, text, 3).has_value());
    }
    SECTION("an empty capture list") {
        REQUIRE_FALSE(ResolveBindingAt({}, text, 3).has_value());
    }
}
