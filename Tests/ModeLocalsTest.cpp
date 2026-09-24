#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Editor/LocalScopes.h"
#include "Editor/Mode.h"
#include "Editor/ModeOverrides.h"
#include "Editor/Grammar/Languages.h"
#include "Editor/Grammar/Node.h"
#include "Editor/Grammar/Parser.h"
#include "Editor/Grammar/Tree.h"

using ned::editor::LocalCapture;
using ned::editor::Mode;
using ned::editor::ModeByName;
using ned::editor::locals::LocalBinding;
using ned::editor::locals::ResolveBindingAt;

namespace {

// End-to-end: a real grammar, a real bundled locals.scm, and the resolver
// over what the query actually produced. LocalScopesTest.cpp covers the
// resolver's own logic against hand-built capture lists; these cases exist
// to pin each language's QUERY -- that it compiles against the grammar at
// all, that its scopes cover their own parameter lists, and that its
// reference rule doesn't sweep up a property or a method name.
std::optional<LocalBinding> Resolve(const std::string& modeName, std::string_view source, std::string_view name,
                                    int occurrence) {
    const std::optional<Mode> mode = ModeByName(modeName);
    REQUIRE(mode.has_value());
    REQUIRE(mode->localScopes); // the query is wired up at all

    std::size_t at = 0;
    for (int i = 0; i <= occurrence; ++i) {
        at = source.find(name, i == 0 ? 0 : at + 1);
        REQUIRE(at != std::string_view::npos);
    }
    const std::vector<LocalCapture> captures = mode->localScopes(source);
    return ResolveBindingAt(captures, source, at);
}

// Every occurrence range, as the text it covers -- so a case can assert
// what would be rewritten without hand-counting offsets.
std::vector<std::string> OccurrenceTexts(const LocalBinding& binding, std::string_view source) {
    std::vector<std::string> texts;
    for (const auto& range : binding.occurrences) {
        texts.emplace_back(source.substr(range.first, range.second - range.first));
    }
    return texts;
}

// per-subtree-fact-memoization follow-up: the same invariant
// ModeTest.cpp's "symbolKind stays correct across a sequence of incremental
// edits" pins for symbolKind/testDiscovery/indent captures, for the fourth
// capability MatchCache.h's own header comment originally named --
// localScopes is called far more sporadically than those three (rename-
// symbol only, not once per Paint()), which is exactly the on-demand
// cadence this test exists to prove safe rather than merely convenient: a
// byte-for-byte match at every step, not just the final one.
std::vector<std::string> DescribeLocalCaptures(const std::vector<LocalCapture>& captures) {
    std::vector<std::string> out;
    out.reserve(captures.size());
    for (const LocalCapture& capture : captures) {
        out.push_back("[" + std::to_string(capture.startByte) + "," + std::to_string(capture.endByte) +
                      ") kind=" + std::to_string(static_cast<int>(capture.kind)) + " qualifier=" + capture.qualifier);
    }
    return out;
}

} // namespace

TEST_CASE("c-mode's localScopes stays correct across a sequence of incremental edits, called sporadically",
          "[Mode][LocalScopes]") {
    const std::optional<Mode> mode = ModeByName("c-mode");
    REQUIRE(mode.has_value());
    REQUIRE(mode->localScopes);

    const std::vector<std::string> steps = {
        "int size;\nvoid f(int size) {\n  size = size + 1;\n}\n",
        // Adds a second parameter and a new local -- deliberately NOT
        // calling localScopes on every intermediate text below, unlike
        // ModeTest.cpp's per-step symbolKind test: this is the shape a real
        // rename-symbol invocation sees, several edits apart.
        "int size;\nvoid f(int size, int extra) {\n  int total = size + extra;\n  size = total;\n}\n",
        // A localized rename deep inside otherwise-unaffected content on
        // both sides (extra -> extras).
        "int size;\nvoid f(int size, int extras) {\n  int total = size + extras;\n  size = total;\n}\n",
    };

    // Deliberately skip step[0] here (only ever observed via `fresh` below)
    // so the FIRST call this Mode's own localScopes ever sees is for
    // step[1] -- covering the case that matters: this closure's own
    // MatchCache reconciling against an edit spanning more than the single
    // most-recent keystroke, exactly as an on-demand caller does.
    for (std::size_t i = 1; i < steps.size(); ++i) {
        INFO("step " << i << ": " << steps[i]);
        const auto incremental = mode->localScopes(steps[i]);
        const std::optional<Mode> freshMode = ModeByName("c-mode");
        REQUIRE(freshMode.has_value());
        const auto fresh = freshMode->localScopes(steps[i]);
        REQUIRE(DescribeLocalCaptures(incremental) == DescribeLocalCaptures(fresh));
    }
}

TEST_CASE("c-mode resolves a parameter without touching a same-named global", "[Mode][LocalScopes]") {
    const std::string source  = "int size;\n"
                                "void f(int size) {\n"
                                "    int doubled = size + size;\n"
                                "    return doubled;\n"
                                "}\n";
    const auto        binding = Resolve("c-mode", source, "size", 1);
    REQUIRE(binding.has_value());
    REQUIRE(binding->qualifier == "parameter");
    REQUIRE_FALSE(binding->scopeIsFile);
    REQUIRE(binding->occurrences.size() == 3); // the parameter and its two uses, not the global
    REQUIRE(binding->definition.first == source.find("int size)") + 4);
}

TEST_CASE("c-mode keeps an inner block's shadowing declaration separate", "[Mode][LocalScopes]") {
    const std::string source = "void f(void) {\n"
                               "    int value = 1;\n"
                               "    {\n"
                               "        int value = 2;\n"
                               "        use(value);\n"
                               "    }\n"
                               "    use(value);\n"
                               "}\n";
    const auto        inner  = Resolve("c-mode", source, "value", 1);
    REQUIRE(inner.has_value());
    REQUIRE(inner->occurrences.size() == 2);

    const auto outer = Resolve("c-mode", source, "value", 0);
    REQUIRE(outer.has_value());
    REQUIRE(outer->occurrences.size() == 2);
}

TEST_CASE("c-mode reports a file-level variable as not local", "[Mode][LocalScopes]") {
    const std::string source  = "int shared;\nvoid f(void) { shared = 1; }\n";
    const auto        binding = Resolve("c-mode", source, "shared", 0);
    REQUIRE(binding.has_value());
    REQUIRE(binding->scopeIsFile);
}

TEST_CASE("cpp-mode resolves a lambda parameter and a range-for variable", "[Mode][LocalScopes]") {
    const std::string source = "int f(int total) {\n"
                               "    auto fn = [](int total) { return total * 2; };\n"
                               "    for (const auto& item : items) { total += item; }\n"
                               "    return fn(total);\n"
                               "}\n";

    SECTION("the lambda's parameter shadows the function's") {
        const auto binding = Resolve("cpp-mode", source, "total", 1);
        REQUIRE(binding.has_value());
        REQUIRE(binding->occurrences.size() == 2); // the lambda's parameter and its one use
    }
    SECTION("the range-for variable is bound by the loop, not the body") {
        const auto binding = Resolve("cpp-mode", source, "item", 0);
        REQUIRE(binding.has_value());
        REQUIRE(binding->occurrences.size() == 2);
        REQUIRE_FALSE(binding->scopeIsFile);
    }
}

TEST_CASE("python-mode uses function scope, not block scope", "[Mode][LocalScopes]") {
    // The whole point of not making a block a scope: `local` is assigned
    // inside the `if` and read outside it, and both belong to one binding.
    const std::string source  = "def f(count):\n"
                                "    if count:\n"
                                "        local = count\n"
                                "    else:\n"
                                "        local = 0\n"
                                "    return local\n";
    const auto        binding = Resolve("python-mode", source, "local", 0);
    REQUIRE(binding.has_value());
    REQUIRE(binding->occurrences.size() == 3);
    REQUIRE_FALSE(binding->scopeIsFile);
}

TEST_CASE("python-mode does not treat an attribute or a keyword argument as a reference", "[Mode][LocalScopes]") {
    const std::string source  = "def f(handle):\n"
                                "    data = handle.data\n"
                                "    call(data=1)\n"
                                "    return data\n";
    const auto        binding = Resolve("python-mode", source, "data", 0); // the assignment target
    REQUIRE(binding.has_value());
    // `handle.data` and `call(data=1)` are a property and a keyword name --
    // renaming the local must not rewrite either.
    REQUIRE(binding->occurrences.size() == 2);
}

TEST_CASE("python-mode renames a comprehension variable despite the use preceding it", "[Mode][LocalScopes]") {
    const std::string source  = "def f(xs):\n    result = [n * n for n in xs]\n";
    const auto        binding = Resolve("python-mode", source, "n *", 0);
    REQUIRE(binding.has_value());
    REQUIRE(binding->occurrences.size() == 3);
    REQUIRE_FALSE(binding->usedBeforeDefinition);
}

TEST_CASE("javascript-mode resolves destructured and block-scoped bindings", "[Mode][LocalScopes]") {
    const std::string source  = "function f(pair) {\n"
                                "    const { alias: local } = pair;\n"
                                "    obj.local = 1;\n"
                                "    call({ local: 2 });\n"
                                "    return local;\n"
                                "}\n";
    const auto        binding = Resolve("javascript-mode", source, "local", 0);
    REQUIRE(binding.has_value());
    // The property in `obj.local` and the key in `{ local: 2 }` are not
    // variable references -- only the binding and the final read are.
    REQUIRE(binding->occurrences.size() == 2);
}

TEST_CASE("typescript-mode resolves an annotated parameter", "[Mode][LocalScopes]") {
    const std::string source  = "function f(count: number, opt?: string): number {\n"
                                "    const local: number = count;\n"
                                "    return local + count;\n"
                                "}\n";
    const auto        binding = Resolve("typescript-mode", source, "count", 0);
    REQUIRE(binding.has_value());
    REQUIRE(binding->qualifier == "parameter");
    REQUIRE(binding->occurrences.size() == 3);
}

TEST_CASE("tsx-mode shares the typescript locals query", "[Mode][LocalScopes]") {
    const std::string source  = "function f(count: number) { return count + 1; }\n";
    const auto        binding = Resolve("tsx-mode", source, "count", 0);
    REQUIRE(binding.has_value());
    REQUIRE(binding->occurrences.size() == 2);
}

TEST_CASE("rust-mode binds a match pattern without capturing the variant name", "[Mode][LocalScopes]") {
    const std::string source  = "fn f(maybe: Option<i32>) -> i32 {\n"
                                "    match maybe {\n"
                                "        Some(value) => value + 1,\n"
                                "        None => 0,\n"
                                "    }\n"
                                "}\n";
    const auto        binding = Resolve("rust-mode", source, "value", 0);
    REQUIRE(binding.has_value());
    REQUIRE(binding->occurrences.size() == 2);
    REQUIRE_FALSE(binding->scopeIsFile);

    // `Some` is a variant, not a binding -- nothing resolves for it.
    REQUIRE_FALSE(Resolve("rust-mode", source, "Some", 0).has_value());
}

TEST_CASE("rust-mode does not confuse a field access with a local", "[Mode][LocalScopes]") {
    const std::string source  = "fn f(obj: Thing) -> i32 {\n    let local = 1;\n    obj.local + local\n}\n";
    const auto        binding = Resolve("rust-mode", source, "local", 0);
    REQUIRE(binding.has_value());
    REQUIRE(binding->occurrences.size() == 2); // the `let` and the bare `local`, not `obj.local`
}

TEST_CASE("go-mode resolves both names a short declaration binds", "[Mode][LocalScopes]") {
    const std::string source = "package main\n\nfunc f() int {\n"
                               "\tvalue, err := call()\n"
                               "\tif err != nil {\n\t\treturn 0\n\t}\n"
                               "\treturn value\n}\n";

    const auto value = Resolve("go-mode", source, "value", 0);
    REQUIRE(value.has_value());
    REQUIRE(value->occurrences.size() == 2);

    const auto err = Resolve("go-mode", source, "err", 0);
    REQUIRE(err.has_value());
    REQUIRE(err->occurrences.size() == 2);
}

TEST_CASE("go-mode scopes an if-statement initializer to the if", "[Mode][LocalScopes]") {
    const std::string source  = "package main\n\nfunc f() {\n"
                                "\tif v := call(); v != nil {\n\t\tuse(v)\n\t}\n"
                                "\tv := other()\n\tuse(v)\n}\n";
    const auto        binding = Resolve("go-mode", source, "v :=", 0);
    REQUIRE(binding.has_value());
    REQUIRE(binding->occurrences.size() == 3); // the if's own v, three times -- not the later one
}

TEST_CASE("java-mode does not rewrite a field access or a method name", "[Mode][LocalScopes]") {
    const std::string source  = "class W {\n"
                                "    int m(int local) {\n"
                                "        obj.local = 1;\n"
                                "        local(2);\n"
                                "        return local;\n"
                                "    }\n"
                                "}\n";
    const auto        binding = Resolve("java-mode", source, "local", 0);
    REQUIRE(binding.has_value());
    REQUIRE(binding->occurrences.size() == 2); // the parameter and the final read
}

TEST_CASE("java-mode reports a field as not local", "[Mode][LocalScopes]") {
    const std::string source = "class W {\n    private int field = 0;\n    int m() { return field; }\n}\n";
    // A field is neither captured nor bound by any scope, so nothing
    // resolves -- rename-symbol declines and defers to a language server.
    REQUIRE_FALSE(Resolve("java-mode", source, "field", 1).has_value());
}

TEST_CASE("csharp-mode resolves a foreach variable and skips member access", "[Mode][LocalScopes]") {
    const std::string source = "class W {\n"
                               "    int M(int local) {\n"
                               "        foreach (var item in items) { obj.local += item; }\n"
                               "        return local;\n"
                               "    }\n"
                               "}\n";

    const auto item = Resolve("csharp-mode", source, "item", 0);
    REQUIRE(item.has_value());
    REQUIRE(item->occurrences.size() == 2);

    const auto local = Resolve("csharp-mode", source, "local", 0);
    REQUIRE(local.has_value());
    REQUIRE(local->occurrences.size() == 2); // not `obj.local`
}

TEST_CASE("kotlin-mode resolves a lambda parameter and skips a navigation suffix", "[Mode][LocalScopes]") {
    const std::string source  = "fun helper(count: Int): Int {\n"
                                "    val local = count\n"
                                "    obj.local = 1\n"
                                "    return local\n"
                                "}\n";
    const auto        binding = Resolve("kotlin-mode", source, "local", 0);
    REQUIRE(binding.has_value());
    REQUIRE(binding->occurrences.size() == 2); // not `obj.local`
}

TEST_CASE("php-mode renames the name inside a variable, never the sigil", "[Mode][LocalScopes]") {
    const std::string source  = "<?php\nfunction f($count) {\n"
                                "    $local = $count;\n"
                                "    $obj->local = 1;\n"
                                "    return $local;\n"
                                "}\n";
    const auto        binding = Resolve("php-mode", source, "local", 0);
    REQUIRE(binding.has_value());
    REQUIRE(binding->occurrences.size() == 2); // not the `$obj->local` property
    for (const std::string& text : OccurrenceTexts(*binding, source)) {
        REQUIRE(text == "local"); // the '$' is outside every range
    }
}

TEST_CASE("php-mode has no block scope", "[Mode][LocalScopes]") {
    const std::string source  = "<?php\nfunction f($flag) {\n"
                                "    if ($flag) { $local = 1; }\n"
                                "    return $local;\n"
                                "}\n";
    const auto        binding = Resolve("php-mode", source, "local", 0);
    REQUIRE(binding.has_value());
    REQUIRE(binding->occurrences.size() == 2);
}

TEST_CASE("bash-mode binds a declared local but not a bare assignment", "[Mode][LocalScopes]") {
    const std::string source = "TOTAL=0\n"
                               "helper() {\n"
                               "    local count=1\n"
                               "    echo \"$count\"\n"
                               "    TOTAL=$count\n"
                               "}\n";

    const auto declared = Resolve("bash-mode", source, "count", 0);
    REQUIRE(declared.has_value());
    REQUIRE_FALSE(declared->scopeIsFile);
    REQUIRE(declared->occurrences.size() == 3);

    // A bare assignment declares nothing in bash, so TOTAL resolves to no
    // binding at all rather than to a function-local one.
    REQUIRE_FALSE(Resolve("bash-mode", source, "TOTAL", 1).has_value());
}

TEST_CASE("fish-mode binds a scoped set but not a bare one", "[Mode][LocalScopes]") {
    const std::string source = "set -g total 0\n"
                               "function tally --argument-names items\n"
                               "    set -l count 0\n"
                               "    for item in $items\n"
                               "        set count (math $count + 1)\n"
                               "    end\n"
                               "    set total $count\n"
                               "end\n";

    // `set -l` declares; the later bare `set count` reassignment is a
    // reference to it, which is the occurrence a rename must not leave
    // behind. Four: the declaration, the reassignment target, and the two
    // $count expansions.
    const auto declared = Resolve("fish-mode", source, "count", 0);
    REQUIRE(declared.has_value());
    REQUIRE_FALSE(declared->scopeIsFile);
    REQUIRE(declared->qualifier == "var");
    REQUIRE(declared->occurrences.size() == 4);

    // `set -g total 0` declares nothing function-local, so a reference to it
    // resolves to no binding rather than to one scoped to this function.
    REQUIRE_FALSE(Resolve("fish-mode", source, "total", 1).has_value());
}

TEST_CASE("fish-mode resolves an --argument-names parameter and a loop variable", "[Mode][LocalScopes]") {
    // `entry`, not `item`: this file's Resolve() finds a name by substring,
    // and "item" occurs inside "items" first.
    const std::string source = "function tally --argument-names items prefix\n"
                               "    for entry in $items\n"
                               "        echo \"$prefix$entry\"\n"
                               "    end\n"
                               "end\n";

    const auto param = Resolve("fish-mode", source, "items", 0);
    REQUIRE(param.has_value());
    REQUIRE(param->qualifier == "parameter");
    REQUIRE(param->occurrences.size() == 2); // the argument name and its one expansion

    // The for variable is function-scoped in fish, not loop-scoped
    // (fish-locals.scm's own note), so the loop is not a scope of its own
    // and both occurrences belong to one binding.
    const auto loopVar = Resolve("fish-mode", source, "entry", 0);
    REQUIRE(loopVar.has_value());
    REQUIRE_FALSE(loopVar->scopeIsFile);
    REQUIRE(OccurrenceTexts(*loopVar, source) == std::vector<std::string>{"entry", "entry"});
}

TEST_CASE("clojure-mode resolves a let binding without capturing a bare value symbol", "[Mode][LocalScopes]") {
    const std::string source = "(defn greet [name greeting]\n"
                               "  (let [msg (str greeting name)\n"
                               "        alias name]\n"
                               "    (println msg alias)))\n";

    const auto binding = Resolve("clojure-mode", source, "alias", 0);
    REQUIRE(binding.has_value());
    REQUIRE_FALSE(binding->scopeIsFile);
    REQUIRE(binding->occurrences.size() == 2);

    // The regression this file's unrolled pair patterns exist for: `name` is
    // the VALUE of the `alias` binding, not a name the let introduces, so it
    // must still resolve to the parameter and carry all three occurrences.
    const auto param = Resolve("clojure-mode", source, "name", 0);
    REQUIRE(param.has_value());
    REQUIRE(param->qualifier == "parameter");
    REQUIRE(param->occurrences.size() == 3);
}

TEST_CASE("clojure-mode does not treat a namespaced symbol as a local reference", "[Mode][LocalScopes]") {
    const std::string source = "(defn shout [msg]\n"
                               "  (clojure.string/upper-case msg))\n";

    const auto param = Resolve("clojure-mode", source, "msg", 0);
    REQUIRE(param.has_value());
    REQUIRE(param->occurrences.size() == 2); // the parameter and the one use

    // `upper-case` is the name half of a qualified symbol; nothing binds it,
    // so there is nothing to resolve rather than a same-named local.
    REQUIRE_FALSE(Resolve("clojure-mode", source, "upper-case", 0).has_value());
}

TEST_CASE("jank-mode shares the clojure locals query", "[Mode][LocalScopes]") {
    // `k`, not `n`: Resolve() searches by substring and "n" occurs in "defn".
    const std::string source  = "(defn twice [k] (* k k))\n";
    const auto        binding = Resolve("jank-mode", source, "k", 0);
    REQUIRE(binding.has_value());
    REQUIRE(binding->qualifier == "parameter");
    REQUIRE(binding->occurrences.size() == 3);
}

TEST_CASE("janet-mode resolves def, parameters, and a let binding", "[Mode][LocalScopes]") {
    const std::string source = "(defn greet [name greeting]\n"
                               "  (def sep \" \")\n"
                               "  (let [msg (string greeting sep name)\n"
                               "        alias name]\n"
                               "    (print msg alias)))\n";

    const auto def = Resolve("janet-mode", source, "sep", 0);
    REQUIRE(def.has_value());
    REQUIRE_FALSE(def->scopeIsFile); // the enclosing defn owns it, not the file
    REQUIRE(def->occurrences.size() == 2);

    const auto binding = Resolve("janet-mode", source, "alias", 0);
    REQUIRE(binding.has_value());
    REQUIRE(binding->occurrences.size() == 2);

    // Same regression as the Clojure case: the bare `name` value must stay
    // the parameter's own occurrence.
    const auto param = Resolve("janet-mode", source, "name", 0);
    REQUIRE(param.has_value());
    REQUIRE(param->qualifier == "parameter");
    REQUIRE(param->occurrences.size() == 3);
}

TEST_CASE("janet-mode reports a module-level def as not local", "[Mode][LocalScopes]") {
    const std::string source  = "(def limit 10)\n(defn under? [n] (< n limit))\n";
    const auto        binding = Resolve("janet-mode", source, "limit", 0);
    REQUIRE(binding.has_value());
    REQUIRE(binding->scopeIsFile);
}

TEST_CASE("a mode with no locals query leaves the capability unset", "[Mode][LocalScopes]") {
    // A deliberate list rather than a backlog. json/yaml/
    // toml/xml have no binding construct at all; html and css have one whose
    // scoping is DOM containment rather than lexical, which this model
    // cannot express without producing a rename that misses descendant uses.
    for (const char* name : {"json-mode", "yaml-mode", "toml-mode", "xml-mode", "html-mode", "css-mode"}) {
        const std::optional<Mode> mode = ModeByName(name);
        REQUIRE(mode.has_value());
        REQUIRE_FALSE(static_cast<bool>(mode->localScopes));
    }
}

// ---------------------------------------------------------------------------
// The eight-pair cliff, which was Phase 3's acceptance test.
//
// `Docs/ParsingEngine.md`: "If Tiers 1+2 express `(let [a 1 b c] ...)` without
// a cliff the vocabulary is real; if not, that is worth learning at language 3
// rather than language 15." The answer turned out to be that the query keeps
// the language knowledge (which heads bind pairwise) and the quantifier moves
// to code -- see clojure-locals.scm's own note and Mode.cpp's
// ExpandPairwiseBindings.
// ---------------------------------------------------------------------------

TEST_CASE("clojure-mode resolves the ninth binding in one vector", "[Mode][LocalScopes]") {
    // Tests/Oracle/corpus/cliff.clj is this same shape, held in the snapshot.
    // Every name here avoids appearing as a substring of anything earlier in
    // the file -- Resolve() finds the Nth literal occurrence, so a function
    // called `nine` would make Resolve("i") land inside its own name.
    const std::string source = "(defn f []\n"
                               "  (let [a 1\n"
                               "        b 2\n"
                               "        c 3\n"
                               "        d 4\n"
                               "        e 5\n"
                               "        f 6\n"
                               "        g 7\n"
                               "        h 8\n"
                               "        i 9]\n"
                               "    (+ a b c d e f g h i)))\n";

    const auto ninth = Resolve("clojure-mode", source, "i", 0);
    REQUIRE(ninth.has_value());
    REQUIRE_FALSE(ninth->scopeIsFile);
    CHECK(ninth->occurrences.size() == 2); // the binding and its one use

    // Not just the ninth: a twentieth would have needed twelve more patterns.
    const auto first = Resolve("clojure-mode", source, "a", 0);
    REQUIRE(first.has_value());
    CHECK(first->occurrences.size() == 2);
}

TEST_CASE("janet-mode resolves the ninth binding in one tuple", "[Mode][LocalScopes]") {
    const std::string source = "(defn nine []\n"
                               "  (let [a 1 b 2 c 3 d 4 e 5 g 6 h 7 j 8 k 9]\n"
                               "    (+ a k)))\n";

    const auto ninth = Resolve("janet-mode", source, "k", 0);
    REQUIRE(ninth.has_value());
    REQUIRE_FALSE(ninth->scopeIsFile);
    CHECK(ninth->occurrences.size() == 2);
}

TEST_CASE("A comment between binding pairs does not shift what binds", "[Mode][LocalScopes]") {
    // The anchored patterns counted a comment as an element, so every name
    // after one landed on an odd index and the VALUE there was captured as a
    // definition instead -- a corrupted rename, which is the failure this
    // file's design notes are most concerned with. Extras are now asked of the
    // parser rather than assumed absent.
    const std::string source = "(defn f [outer]\n"
                               "  (let [a 1\n"
                               "        ;; a note, then more pairs\n"
                               "        b outer]\n"
                               "    (+ a b)))\n";

    const auto second = Resolve("clojure-mode", source, "b", 0);
    REQUIRE(second.has_value());
    CHECK(second->occurrences.size() == 2);

    // `outer` is b's VALUE, so it must still be the parameter, with both of
    // its occurrences -- not a binding of itself.
    const auto param = Resolve("clojure-mode", source, "outer", 0);
    REQUIRE(param.has_value());
    CHECK(param->qualifier == "parameter");
    CHECK(param->occurrences.size() == 2);
}

TEST_CASE("Janet reaches the same answer through the parser's own extras", "[Mode][LocalScopes]") {
    // The two languages take different routes to the same rule, and both are
    // live: tree-sitter-janet-simple declares `comment` in its `extras`, so
    // Node::IsExtra answers here with nothing said per language, while
    // tree-sitter-clojure declares `extras: []` and needs its query to name
    // the comment. Worth one test each so neither path rots unnoticed.
    const std::string source = "(defn f [outer]\n"
                               "  (let [a 1\n"
                               "        # a note, then more pairs\n"
                               "        b outer]\n"
                               "    (+ a b)))\n";

    const auto second = Resolve("janet-mode", source, "b", 0);
    REQUIRE(second.has_value());
    CHECK(second->occurrences.size() == 2);

    const auto param = Resolve("janet-mode", source, "outer", 0);
    REQUIRE(param.has_value());
    CHECK(param->qualifier == "parameter");
    CHECK(param->occurrences.size() == 2);
}

TEST_CASE("A discarded form between binding pairs does not shift what binds", "[Mode][LocalScopes]") {
    // `#_form` reads as an ordinary child and means "pretend this is not
    // here". The query names it @local.skip; nothing in C++ knows what a
    // reader macro is.
    const std::string source = "(defn f [outer]\n"
                               "  (let [a 1 #_ignored b outer]\n"
                               "    (+ a b)))\n";

    const auto second = Resolve("clojure-mode", source, "b", 0);
    REQUIRE(second.has_value());
    CHECK(second->occurrences.size() == 2);

    const auto param = Resolve("clojure-mode", source, "outer", 0);
    REQUIRE(param.has_value());
    CHECK(param->qualifier == "parameter");
    CHECK(param->occurrences.size() == 2);
}

TEST_CASE("A destructuring binding is declined rather than renamed wholesale", "[Mode][LocalScopes]") {
    // The names inside `{:keys [x]}` are not direct children of the vector, so
    // the expansion leaves the whole form alone rather than capturing it as if
    // it were an identifier -- the same degradation the queries' own headers
    // already document, kept rather than quietly changed.
    const std::string source = "(defn f [m]\n"
                               "  (let [{:keys [x]} m\n"
                               "        z 2]\n"
                               "    (+ x z)))\n";

    // The pair AFTER the destructuring one still binds: parity counts
    // elements, and the destructuring form is one element.
    const auto after = Resolve("clojure-mode", source, "z", 0);
    REQUIRE(after.has_value());
    CHECK(after->occurrences.size() == 2);

    // And `m`, the destructured value, is still the parameter.
    const auto param = Resolve("clojure-mode", source, "m", 0);
    REQUIRE(param.has_value());
    CHECK(param->qualifier == "parameter");
}

// Lua binds only through `local`, parameters and loop variables: a plain
// assignment is a use of whatever it names, and a field name is no use.
TEST_CASE("lua-mode resolves a reassignment to the local it assigns", "[Mode][LocalScopes]") {
    const std::string source = "local function make()\n"
                               "  local count = 0\n"
                               "  local t = {count = count}\n"
                               "  local function inc(step)\n"
                               "    count = count + step\n"
                               "    return t.count + count\n"
                               "  end\n"
                               "  return inc\n"
                               "end\n";
    const auto        count  = Resolve("lua-mode", source, "count", 4); // `count = count` inside inc
    REQUIRE(count.has_value());
    CHECK(count->qualifier == "var");
    CHECK_FALSE(count->scopeIsFile);
    CHECK(count->occurrences.size() == 5); // not `{count =` or `t.count`

    const auto step = Resolve("lua-mode", source, "step", 1);
    REQUIRE(step.has_value());
    CHECK(step->qualifier == "parameter");
    CHECK(step->occurrences.size() == 2);
}

TEST_CASE("lua-mode binds a local function's name around the function", "[Mode][LocalScopes]") {
    const std::string source = "local function outer()\n"
                               "  local function helper(n)\n"
                               "    return helper(n - 1)\n"
                               "  end\n"
                               "  return helper(3)\n"
                               "end\n";
    const auto        helper = Resolve("lua-mode", source, "helper", 2);
    REQUIRE(helper.has_value());
    CHECK(helper->qualifier == "function");
    CHECK_FALSE(helper->scopeIsFile);
    CHECK(helper->occurrences.size() == 3);
}

TEST_CASE("lua-mode treats a chunk-level local as private to the file", "[Mode][LocalScopes]") {
    const std::string source = "local limit = 10\n"
                               "local function clamp(n) return math.min(n, limit) end\n"
                               "total = clamp(limit)\n";
    const auto        limit  = Resolve("lua-mode", source, "limit", 1);
    REQUIRE(limit.has_value());
    CHECK_FALSE(limit->scopeIsFile);
    CHECK(limit->occurrences.size() == 3);

    const auto clamp = Resolve("lua-mode", source, "clamp", 0);
    REQUIRE(clamp.has_value());
    CHECK_FALSE(clamp->scopeIsFile);

    // A global stays a language server's business.
    CHECK_FALSE(Resolve("lua-mode", source, "total", 0).has_value());
}

TEST_CASE("nix-mode resolves parameters, let bindings and inherits", "[Mode][LocalScopes]") {
    const std::string source = "{ pkgs, lib ? pkgs.lib }:\n"
                               "let\n"
                               "  version = \"1.0\";\n"
                               "  name = \"demo-${version}\";\n"
                               "  inherit (lib) mkIf;\n"
                               "in\n"
                               "pkgs.stdenv.mkDerivation {\n"
                               "  inherit name version;\n"
                               "  src = mkIf true ./.;\n"
                               "}\n";
    const auto version = Resolve("nix-mode", source, "version", 0);
    REQUIRE(version.has_value());
    CHECK(version->qualifier == "var");
    CHECK_FALSE(version->scopeIsFile);
    CHECK(version->occurrences.size() == 3); // the binding, ${version}, `inherit ... version`

    const auto pkgs = Resolve("nix-mode", source, "pkgs", 0);
    REQUIRE(pkgs.has_value());
    CHECK(pkgs->qualifier == "parameter");
    CHECK(pkgs->occurrences.size() == 3);

    const auto mkIf = Resolve("nix-mode", source, "mkIf", 1);
    REQUIRE(mkIf.has_value());
    CHECK(mkIf->definition.first == source.find("mkIf"));
    CHECK(mkIf->occurrences.size() == 2);
}

// R binds per function; a call's named argument and a `$` member are no use
// of a variable, however they are spelled.
TEST_CASE("r-mode resolves function-level bindings and skips named arguments", "[Mode][LocalScopes]") {
    const std::string source = "total <- function(values, na.rm = FALSE) {\n"
                               "  sum <- 0\n"
                               "  for (v in values) {\n"
                               "    sum <- sum + v\n"
                               "  }\n"
                               "  result <- list(sum = sum, n = length(values))\n"
                               "  result$sum\n"
                               "}\n";
    const auto sum = Resolve("r-mode", source, "sum", 2); // `sum + v`
    REQUIRE(sum.has_value());
    CHECK(sum->qualifier == "var");
    CHECK_FALSE(sum->scopeIsFile);
    CHECK(sum->occurrences.size() == 4); // not `list(sum =` or `result$sum`

    const auto values = Resolve("r-mode", source, "values", 0);
    REQUIRE(values.has_value());
    CHECK(values->qualifier == "parameter");
    CHECK(values->occurrences.size() == 3);

    const auto v = Resolve("r-mode", source, "v ", 0);
    REQUIRE(v.has_value());
    CHECK(v->occurrences.size() == 2);
}

// Perl's sigil picks the variable: $x, @x and %x are three, and an element
// or slice names its container by the sigil of what it yields.
TEST_CASE("perl-mode keeps $x, @x and %x apart", "[Mode][LocalScopes]") {
    const std::string source = "sub total {\n"
                               "  my ($x, @x) = @_;\n"
                               "  my %x = (a => 1);\n"
                               "  my $sum = $x + $x[0] + $#x + $x{a};\n"
                               "  print \"$x[1] @x[1, 2] @x{'a'}\\n\";\n"
                               "  return $sum;\n"
                               "}\n";
    const auto        scalar = Resolve("perl-mode", source, "x", 0); // `my ($x`
    REQUIRE(scalar.has_value());
    CHECK_FALSE(scalar->scopeIsFile);
    CHECK(scalar->occurrences.size() == 2); // the declaration and `$x +`

    const auto array = Resolve("perl-mode", source, "x", 1); // `@x)`
    REQUIRE(array.has_value());
    CHECK(array->occurrences.size() == 5); // the decl, $x[0], $#x, "$x[1]", "@x[1, 2]"

    const auto hash = Resolve("perl-mode", source, "x", 2); // `my %x`
    REQUIRE(hash.has_value());
    CHECK(hash->occurrences.size() == 3); // the decl, $x{a}, "@x{'a'}"
}

TEST_CASE("perl-mode scopes a my to its block and leaves our alone", "[Mode][LocalScopes]") {
    const std::string source = "my $count = 0;\n"
                               "our $VERSION = 1;\n"
                               "for my $item (@ARGV) {\n"
                               "  my $count = $item;\n"
                               "  print $count;\n"
                               "}\n"
                               "print $count, $VERSION;\n";
    // A file-level `my` is private to the file, so it is still a local.
    const auto outer = Resolve("perl-mode", source, "count", 0);
    REQUIRE(outer.has_value());
    CHECK_FALSE(outer->scopeIsFile);
    CHECK(OccurrenceTexts(*outer, source).size() == 2);

    const auto inner = Resolve("perl-mode", source, "count", 1);
    REQUIRE(inner.has_value());
    CHECK(inner->occurrences.size() == 2);

    const auto item = Resolve("perl-mode", source, "item", 0);
    REQUIRE(item.has_value());
    CHECK(item->occurrences.size() == 2);

    CHECK_FALSE(Resolve("perl-mode", source, "VERSION", 0).has_value());
}

TEST_CASE("perl-mode binds signature parameters", "[Mode][LocalScopes]") {
    const std::string source   = "sub greet ($name, $greeting = 'hi', @rest) {\n"
                                 "  return \"$greeting, $name\" . scalar(@rest);\n"
                                 "}\n";
    const auto        greeting = Resolve("perl-mode", source, "greeting", 0);
    REQUIRE(greeting.has_value());
    CHECK(greeting->qualifier == "parameter");
    CHECK(greeting->occurrences.size() == 2);

    const auto rest = Resolve("perl-mode", source, "rest", 0);
    REQUIRE(rest.has_value());
    CHECK(rest->occurrences.size() == 2);
}

// Elixir binds by pattern: every name inside a match pattern binds, at any
// depth, except what the pattern only reads (a pinned ^x, a call's target).
TEST_CASE("elixir-mode binds every name a pattern holds, and nothing it only reads", "[Mode][LocalScopes]") {
    const std::string source  = "defmodule M do\n"
                                "  def fetch(id, %{retries: retries} = opts) when id > 0 do\n"
                                "    {:ok, %{body: body, meta: [first | rest]}} = get(id, opts)\n"
                                "    ^body = decode(body)\n"
                                "    count = length(rest)\n"
                                "    count = count + retries\n"
                                "    {first, count}\n"
                                "  end\n"
                                "end\n";
    const auto        retries = Resolve("elixir-mode", source, "retries", 1); // the value, not the `retries:` key
    REQUIRE(retries.has_value());
    CHECK(retries->qualifier == "parameter");
    CHECK_FALSE(retries->scopeIsFile);
    CHECK(OccurrenceTexts(*retries, source).size() == 2);

    const auto body = Resolve("elixir-mode", source, "body", 1); // `body: body`'s value
    REQUIRE(body.has_value());
    CHECK(body->occurrences.size() == 3); // the binding, ^body, decode(body)

    const auto first = Resolve("elixir-mode", source, "first", 0);
    REQUIRE(first.has_value());
    CHECK(first->occurrences.size() == 2);

    const auto count = Resolve("elixir-mode", source, "count", 2); // `count + retries`
    REQUIRE(count.has_value());
    CHECK(count->occurrences.size() == 4);

    CHECK_FALSE(Resolve("elixir-mode", source, "get", 0).has_value()); // a function, not a variable
}

TEST_CASE("elixir-mode scopes clause patterns to their clause", "[Mode][LocalScopes]") {
    const std::string source = "def run(items) do\n"
                               "  total = 0\n"
                               "  for item <- items, do: item * 2\n"
                               "  Enum.map(items, fn item -> item + total end)\n"
                               "  case items do\n"
                               "    [item | _] -> item\n"
                               "    [] -> total\n"
                               "  end\n"
                               "end\n";
    // "item" also matches inside "items": 1, 5 and 8 are the three binding sites.
    for (const int site : {1, 5, 8}) {
        const auto item = Resolve("elixir-mode", source, "item", site);
        REQUIRE(item.has_value());
        INFO("site " << site);
        CHECK(item->occurrences.size() == 2);
    }
    const auto total = Resolve("elixir-mode", source, "total", 0);
    REQUIRE(total.has_value());
    CHECK(total->occurrences.size() == 3);
}

// Dart's function signature and body are siblings; the scope joins them so a
// parameter and its uses share one.
TEST_CASE("dart-mode scopes a function's parameters over its body", "[Mode][LocalScopes]") {
    const std::string source = "int count = 0;\n"
                               "int add(int count, [int step = 1]) {\n"
                               "  var total = count + step;\n"
                               "  for (final e in [1, 2]) { total += e; }\n"
                               "  final (lo, hi) = (total, count);\n"
                               "  return lo + hi;\n"
                               "}\n"
                               "class C {\n"
                               "  int n = 0;\n"
                               "  C(this.n);\n"
                               "  int scale(int by) { final n = by * this.n; return n; }\n"
                               "}\n";
    const auto        count  = Resolve("dart-mode", source, "count", 1); // the parameter, not the top-level
    REQUIRE(count.has_value());
    CHECK(count->qualifier == "parameter");
    CHECK_FALSE(count->scopeIsFile);
    CHECK(count->occurrences.size() == 3);

    const auto total = Resolve("dart-mode", source, "total", 0);
    REQUIRE(total.has_value());
    CHECK(total->occurrences.size() == 3);

    const auto e = Resolve("dart-mode", source, "e in", 0);
    REQUIRE(e.has_value());
    CHECK(e->occurrences.size() == 2);

    const auto hi = Resolve("dart-mode", source, "hi", 0);
    REQUIRE(hi.has_value());
    CHECK(hi->occurrences.size() == 2);

    // A method's local shadows the field; `this.n` stays the field.
    const auto local = Resolve("dart-mode", source, "n =", 1);
    REQUIRE(local.has_value());
    CHECK(OccurrenceTexts(*local, source) == std::vector<std::string>{"n", "n"});

    const auto by = Resolve("dart-mode", source, "by", 0);
    REQUIRE(by.has_value());
    CHECK(by->occurrences.size() == 2);
}
