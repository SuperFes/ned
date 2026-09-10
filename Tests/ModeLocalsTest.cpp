#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Editor/LocalScopes.h"
#include "Editor/Mode.h"
#include "Editor/ModeOverrides.h"

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

} // namespace

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
    // A deliberate list rather than a backlog -- see Queries.h. json/yaml/
    // toml/xml have no binding construct at all; html and css have one whose
    // scoping is DOM containment rather than lexical, which this model
    // cannot express without producing a rename that misses descendant uses.
    for (const char* name : {"json-mode", "yaml-mode", "toml-mode", "xml-mode", "html-mode", "css-mode"}) {
        const std::optional<Mode> mode = ModeByName(name);
        REQUIRE(mode.has_value());
        REQUIRE_FALSE(static_cast<bool>(mode->localScopes));
    }
}
