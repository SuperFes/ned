// change-signature end to end, per language: a source file in, the file it
// becomes out. Each language's signatures/calls queries and its synthetic
// wrapper (Mode::signatureTemplate) are what is under test; the mapping and
// rewrite themselves are ChangeSignatureTest.cpp's.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Editor/ChangeSignature.h"
#include "Editor/Mode.h"
#include "Editor/ModeOverrides.h"

namespace {

using ned::editor::CallMarker;
using ned::editor::SignatureMarker;
namespace changesig = ned::editor::changesig;

std::string_view Slice(std::string_view text, std::size_t start, std::size_t end) {
    return text.substr(start, end - start);
}

// What change-signature does to one file: the definition named `function`
// (its first one) takes `newParameters`, and so does every same-named,
// same-arity signature; every call to it is rewritten. A declined operation
// comes back as "declined: <reason>", and a call site left alone is marked
// in place with "/*declined*/" so a test sees which one.
std::string ChangeSignature(std::string_view language, const std::string& source, std::string_view function,
                            const std::string& newParameters) {
    const std::optional<ned::editor::Mode> mode = ned::editor::ModeByName(std::string(language) + "-mode");
    REQUIRE(mode.has_value());
    REQUIRE(static_cast<bool>(mode->signatures));
    REQUIRE(static_cast<bool>(mode->calls));
    const std::size_t placeholder = mode->signatureTemplate.find("{}");
    REQUIRE(placeholder != std::string::npos);

    const std::vector<SignatureMarker> signatures = mode->signatures(source);
    const auto                         target     = std::find_if(signatures.begin(), signatures.end(), [&](const SignatureMarker& signature) {
        return Slice(source, signature.nameStartByte, signature.nameEndByte) == function;
    });
    REQUIRE(target != signatures.end());
    const std::string_view callName = Slice(source, target->callNameStartByte, target->callNameEndByte);

    const std::string                  synthetic = mode->signatureTemplate.substr(0, placeholder) + newParameters +
                                                   mode->signatureTemplate.substr(placeholder + 2);
    const std::vector<SignatureMarker> parsed    = mode->signatures(synthetic);
    REQUIRE_FALSE(parsed.empty());
    const changesig::MappingResult mapping =
        changesig::BuildPositionMapping(source, target->parameters, synthetic, parsed.front().parameters);
    if (mapping.declined) {
        return "declined: " + mapping.declineReason;
    }

    struct Edit {
        std::size_t start, end;
        std::string text;
    };
    std::vector<Edit> edits;
    for (const SignatureMarker& signature : signatures) {
        if (Slice(source, signature.callNameStartByte, signature.callNameEndByte) == callName &&
            signature.parameters.size() == target->parameters.size()) {
            edits.push_back({signature.parametersStartByte + 1, signature.parametersEndByte - 1, newParameters});
        }
    }
    for (const CallMarker& call : mode->calls(source)) {
        if (Slice(source, call.calleeStartByte, call.calleeEndByte) != callName) {
            continue;
        }
        const changesig::ArgumentRewrite rewrite =
            changesig::RewriteArgumentList(source, call.arguments, synthetic, mapping, call.receiver);
        if (rewrite.declined) {
            edits.push_back({call.argumentsStartByte, call.argumentsStartByte, "/*declined*/"});
        }
        else {
            edits.push_back({call.argumentsStartByte + 1, call.argumentsEndByte - 1, rewrite.argumentListText});
        }
    }
    std::sort(edits.begin(), edits.end(), [](const Edit& a, const Edit& b) { return a.start > b.start; });
    std::string result = source;
    for (const Edit& edit : edits) {
        result.replace(edit.start, edit.end - edit.start, edit.text);
    }
    return result;
}

} // namespace

TEST_CASE("PHP change-signature reorders, drops and defaults across functions and methods", "[ChangeSignature]") {
    const std::string source = "<?php\n"
                               "function total(array $items, bool $round = false, int $scale = 2) {}\n"
                               "total($cart, true, 4);\n"
                               "\\App\\total($cart, false, 1);\n";
    CHECK(ChangeSignature("php", source, "total", "int $scale, array $items, string $currency = 'EUR'") ==
          "<?php\n"
          "function total(int $scale, array $items, string $currency = 'EUR') {}\n"
          "total(4, $cart, 'EUR');\n"
          "\\App\\total(1, $cart, 'EUR');\n");

    const std::string methods = "<?php\n"
                                "class Cart {\n"
                                "  public function add(Item $item, int $qty = 1) {}\n"
                                "  public static function make(array $rows) {}\n"
                                "}\n"
                                "$cart->add($apple, 3);\n"
                                "$cart?->add($pear, 2);\n"
                                "Cart::make($rows);\n";
    CHECK(ChangeSignature("php", methods, "add", "int $qty, Item $item") ==
          "<?php\n"
          "class Cart {\n"
          "  public function add(int $qty, Item $item) {}\n"
          "  public static function make(array $rows) {}\n"
          "}\n"
          "$cart->add(3, $apple);\n"
          "$cart?->add(2, $pear);\n"
          "Cart::make($rows);\n");
}

TEST_CASE("PHP change-signature follows a constructor to its `new` call sites", "[ChangeSignature]") {
    const std::string source = "<?php\n"
                               "class Mailer {\n"
                               "  public function __construct(private Transport $transport, int $retries = 3) {}\n"
                               "}\n"
                               "$m = new Mailer($smtp, 5);\n"
                               "$n = new \\App\\Mailer($smtp, 1);\n";
    CHECK(ChangeSignature("php", source, "__construct", "int $retries, private Transport $transport, ?Logger $log = null") ==
          "<?php\n"
          "class Mailer {\n"
          "  public function __construct(int $retries, private Transport $transport, ?Logger $log = null) {}\n"
          "}\n"
          "$m = new Mailer(5, $smtp, null);\n"
          "$n = new \\App\\Mailer(1, $smtp, null);\n");
}

TEST_CASE("PHP change-signature leaves named and spread call sites alone", "[ChangeSignature]") {
    const std::string source = "<?php\n"
                               "function pair(int $a, int $b) {}\n"
                               "pair(1, 2);\n"
                               "pair(b: 2, a: 1);\n"
                               "pair(...$both);\n";
    CHECK(ChangeSignature("php", source, "pair", "int $b, int $a") ==
          "<?php\n"
          "function pair(int $b, int $a) {}\n"
          "pair(2, 1);\n"
          "pair/*declined*/(b: 2, a: 1);\n"
          "pair/*declined*/(...$both);\n");
}

TEST_CASE("PHP change-signature declines a variadic parameter", "[ChangeSignature]") {
    const std::string source = "<?php\nfunction log(string $msg, ...$args) {}\nlog('x', 1);\n";
    CHECK(ChangeSignature("php", source, "log", "...$args, string $msg").starts_with("declined: "));
}

TEST_CASE("C change-signature reorders a function's parameters and its calls", "[ChangeSignature]") {
    const std::string source = "int area(int w, int *h) { return w * *h; }\n"
                               "int main(void) { return area(3, &four) + shape.area(1, &two); }\n";
    CHECK(ChangeSignature("c", source, "area", "int *h, int w") ==
          "int area(int *h, int w) { return w * *h; }\n"
          "int main(void) { return area(&four, 3) + shape.area(&two, 1); }\n");
    // C has no default values, so nothing can be added.
    CHECK(ChangeSignature("c", source, "area", "int w, int *h, int d").starts_with("declined: "));
}

TEST_CASE("Go change-signature reorders and drops, and declines grouped names", "[ChangeSignature]") {
    const std::string source = "package main\n"
                               "func Scale(v int, factor int) int { return v * factor }\n"
                               "func main() { Scale(2, 3); m.Scale(4, 5); Scale(xs...) }\n";
    CHECK(ChangeSignature("go", source, "Scale", "factor int, v int") ==
          "package main\n"
          "func Scale(factor int, v int) int { return v * factor }\n"
          "func main() { Scale(3, 2); m.Scale(5, 4); Scale/*declined*/(xs...) }\n");
    CHECK(ChangeSignature("go", source, "Scale", "v int") ==
          "package main\n"
          "func Scale(v int) int { return v * factor }\n"
          "func main() { Scale(2); m.Scale(4); Scale/*declined*/(xs...) }\n");

    const std::string grouped = "package main\nfunc Pair(a, b int) {}\nfunc main() { Pair(1, 2) }\n";
    CHECK(ChangeSignature("go", grouped, "Pair", "b int, a int").starts_with("declined: "));
}

TEST_CASE("Rust change-signature keeps self out of method calls but not path calls", "[ChangeSignature]") {
    const std::string source = "struct S;\n"
                               "impl S {\n"
                               "    fn scale(&self, v: i32, factor: i32) -> i32 { v * factor }\n"
                               "}\n"
                               "fn main() { let s = S; s.scale(2, 3); S::scale(&s, 4, 5); }\n";
    CHECK(ChangeSignature("rust", source, "scale", "&self, factor: i32, v: i32") ==
          "struct S;\n"
          "impl S {\n"
          "    fn scale(&self, factor: i32, v: i32) -> i32 { v * factor }\n"
          "}\n"
          "fn main() { let s = S; s.scale(3, 2); S::scale(&s, 5, 4); }\n");
    CHECK(ChangeSignature("rust", source, "scale", "factor: i32, &self, v: i32").starts_with("declined: "));

    const std::string free = "fn add(mut a: i32, b: i32) -> i32 { a + b }\nfn main() { add(1, 2); m::add(3, 4); }\n";
    CHECK(ChangeSignature("rust", free, "add", "b: i32, mut a: i32") ==
          "fn add(b: i32, mut a: i32) -> i32 { a + b }\nfn main() { add(2, 1); m::add(4, 3); }\n");
}

TEST_CASE("Java change-signature covers methods and constructors", "[ChangeSignature]") {
    const std::string source = "class Box {\n"
                               "  Box(int w, int h) {}\n"
                               "  int area(int w, int h) { return w * h; }\n"
                               "  void use() { area(1, 2); this.area(3, 4); new Box(5, 6); }\n"
                               "}\n";
    CHECK(ChangeSignature("java", source, "area", "int h, int w") ==
          "class Box {\n"
          "  Box(int w, int h) {}\n"
          "  int area(int h, int w) { return w * h; }\n"
          "  void use() { area(2, 1); this.area(4, 3); new Box(5, 6); }\n"
          "}\n");
    CHECK(ChangeSignature("java", source, "Box", "int h, int w") ==
          "class Box {\n"
          "  Box(int h, int w) {}\n"
          "  int area(int w, int h) { return w * h; }\n"
          "  void use() { area(1, 2); this.area(3, 4); new Box(6, 5); }\n"
          "}\n");
}

TEST_CASE("Kotlin change-signature reads sibling defaults and declines named and short calls", "[ChangeSignature]") {
    const std::string source = "fun scale(v: Int, factor: Int = 2): Int = v * factor\n"
                               "class Box(val w: Int, val h: Int)\n"
                               "fun use() { scale(1, 3); scale(4); scale(v = 1, factor = 2); Box(5, 6) }\n";
    CHECK(ChangeSignature("kotlin", source, "scale", "factor: Int = 2, v: Int") ==
          "fun scale(factor: Int = 2, v: Int): Int = v * factor\n"
          "class Box(val w: Int, val h: Int)\n"
          "fun use() { scale(3, 1); scale/*declined*/(4); scale/*declined*/(v = 1, factor = 2); Box(5, 6) }\n");
    CHECK(ChangeSignature("kotlin", source, "scale", "v: Int, factor: Int = 2, bias: Int = 0") ==
          "fun scale(v: Int, factor: Int = 2, bias: Int = 0): Int = v * factor\n"
          "class Box(val w: Int, val h: Int)\n"
          "fun use() { scale(1, 3, 0); scale/*declined*/(4); scale/*declined*/(v = 1, factor = 2); Box(5, 6) }\n");
    CHECK(ChangeSignature("kotlin", source, "Box", "val h: Int, val w: Int") ==
          "fun scale(v: Int, factor: Int = 2): Int = v * factor\n"
          "class Box(val h: Int, val w: Int)\n"
          "fun use() { scale(1, 3); scale(4); scale(v = 1, factor = 2); Box(6, 5) }\n");
}

TEST_CASE("C# change-signature covers methods, constructors and extension methods", "[ChangeSignature]") {
    const std::string source = "class Box {\n"
                               "  public Box(int w, int h) {}\n"
                               "  int Area(int w, int h = 1) => w * h;\n"
                               "  void Use() { Area(1, 2); this.Area(3, 4); Area(w: 1, h: 2); new Box(5, 6); }\n"
                               "}\n";
    CHECK(ChangeSignature("csharp", source, "Area", "int h = 1, int w") ==
          "class Box {\n"
          "  public Box(int w, int h) {}\n"
          "  int Area(int h = 1, int w) => w * h;\n"
          "  void Use() { Area(2, 1); this.Area(4, 3); Area/*declined*/(w: 1, h: 2); new Box(5, 6); }\n"
          "}\n");
    CHECK(ChangeSignature("csharp", source, "Box", "int h, int w") ==
          "class Box {\n"
          "  public Box(int h, int w) {}\n"
          "  int Area(int w, int h = 1) => w * h;\n"
          "  void Use() { Area(1, 2); this.Area(3, 4); Area(w: 1, h: 2); new Box(6, 5); }\n"
          "}\n");

    // The object an extension method is called on is its `this` parameter;
    // called statically, the extra argument gives it away.
    const std::string extension = "static class Ext {\n"
                                  "  public static int Twice(this int x, int by) => x * by;\n"
                                  "}\n"
                                  "class U { void F() { 3.Twice(2); Ext.Twice(3, 2); } }\n";
    CHECK(ChangeSignature("csharp", extension, "Twice", "this int x, int by, int plus = 0") ==
          "static class Ext {\n"
          "  public static int Twice(this int x, int by, int plus = 0) => x * by;\n"
          "}\n"
          "class U { void F() { 3.Twice(2, 0); Ext.Twice/*declined*/(3, 2); } }\n");
}

TEST_CASE("JavaScript change-signature covers functions, arrows, methods and constructors", "[ChangeSignature]") {
    const std::string source = "function greet(name, greeting = \"hi\") {}\n"
                               "class Box { constructor(w, h) {} area(w, h) {} }\n"
                               "greet(\"a\", \"b\"); greet(\"a\"); greet(...args);\n"
                               "obj.area(1, 2); new Box(3, 4);\n"
                               "const add = (a, b) => a + b; add(1, 2);\n";
    CHECK(ChangeSignature("javascript", source, "greet", "greeting = \"hi\", name") ==
          "function greet(greeting = \"hi\", name) {}\n"
          "class Box { constructor(w, h) {} area(w, h) {} }\n"
          "greet(\"b\", \"a\"); greet/*declined*/(\"a\"); greet/*declined*/(...args);\n"
          "obj.area(1, 2); new Box(3, 4);\n"
          "const add = (a, b) => a + b; add(1, 2);\n");
    CHECK(ChangeSignature("javascript", source, "constructor", "h, w, d = 0") ==
          "function greet(name, greeting = \"hi\") {}\n"
          "class Box { constructor(h, w, d = 0) {} area(w, h) {} }\n"
          "greet(\"a\", \"b\"); greet(\"a\"); greet(...args);\n"
          "obj.area(1, 2); new Box(4, 3, 0);\n"
          "const add = (a, b) => a + b; add(1, 2);\n");
    CHECK(ChangeSignature("javascript", source, "add", "b, a") ==
          "function greet(name, greeting = \"hi\") {}\n"
          "class Box { constructor(w, h) {} area(w, h) {} }\n"
          "greet(\"a\", \"b\"); greet(\"a\"); greet(...args);\n"
          "obj.area(1, 2); new Box(3, 4);\n"
          "const add = (b, a) => a + b; add(2, 1);\n");
}

TEST_CASE("TypeScript change-signature reads typed and optional parameters and never passes `this`", "[ChangeSignature]") {
    const std::string source = "class Box {\n"
                               "  constructor(private w: number, h: number) {}\n"
                               "  area(this: Box, scale: number, unit?: string) {}\n"
                               "}\n"
                               "b.area(2, \"cm\"); new Box(1, 2);\n";
    CHECK(ChangeSignature("typescript", source, "area", "this: Box, unit: string = \"px\", scale: number") ==
          "class Box {\n"
          "  constructor(private w: number, h: number) {}\n"
          "  area(this: Box, unit: string = \"px\", scale: number) {}\n"
          "}\n"
          "b.area(\"cm\", 2); new Box(1, 2);\n");
    CHECK(ChangeSignature("typescript", source, "constructor", "h: number, private w: number") ==
          "class Box {\n"
          "  constructor(h: number, private w: number) {}\n"
          "  area(this: Box, scale: number, unit?: string) {}\n"
          "}\n"
          "b.area(2, \"cm\"); new Box(2, 1);\n");
}

TEST_CASE("Python change-signature handles self, cls and __init__ receivers", "[ChangeSignature]") {
    const std::string source = "class Box:\n"
                               "    def __init__(self, w, h=1):\n"
                               "        pass\n"
                               "    def area(self, scale, unit=\"cm\"):\n"
                               "        pass\n"
                               "    @classmethod\n"
                               "    def square(cls, side):\n"
                               "        pass\n"
                               "b = Box(2, 3)\n"
                               "b.area(2, \"mm\")\n"
                               "Box.area(b, 3, \"m\")\n"
                               "Box.square(4)\n"
                               "b.area(scale=2)\n";
    CHECK(ChangeSignature("python", source, "area", "self, unit, scale") ==
          "class Box:\n"
          "    def __init__(self, w, h=1):\n"
          "        pass\n"
          "    def area(self, unit, scale):\n"
          "        pass\n"
          "    @classmethod\n"
          "    def square(cls, side):\n"
          "        pass\n"
          "b = Box(2, 3)\n"
          "b.area(\"mm\", 2)\n"
          "Box.area(b, \"m\", 3)\n"
          "Box.square(4)\n"
          "b.area/*declined*/(scale=2)\n");
    CHECK(ChangeSignature("python", source, "__init__", "self, h=1, w") ==
          "class Box:\n"
          "    def __init__(self, h=1, w):\n"
          "        pass\n"
          "    def area(self, scale, unit=\"cm\"):\n"
          "        pass\n"
          "    @classmethod\n"
          "    def square(cls, side):\n"
          "        pass\n"
          "b = Box(3, 2)\n"
          "b.area(2, \"mm\")\n"
          "Box.area(b, 3, \"m\")\n"
          "Box.square(4)\n"
          "b.area(scale=2)\n");
    CHECK(ChangeSignature("python", source, "square", "cls, side, pad=0") ==
          "class Box:\n"
          "    def __init__(self, w, h=1):\n"
          "        pass\n"
          "    def area(self, scale, unit=\"cm\"):\n"
          "        pass\n"
          "    @classmethod\n"
          "    def square(cls, side, pad=0):\n"
          "        pass\n"
          "b = Box(2, 3)\n"
          "b.area(2, \"mm\")\n"
          "Box.area(b, 3, \"m\")\n"
          "Box.square(4, 0)\n"
          "b.area(scale=2)\n");
}

TEST_CASE("Python change-signature passes keyword-only arguments through by name", "[ChangeSignature]") {
    const std::string source = "def fetch(url, retries=3, *, timeout=10, verify=True):\n"
                               "    pass\n"
                               "fetch(u, 5, timeout=2, verify=False)\n"
                               "fetch(u)\n"
                               "fetch(url=u)\n";
    CHECK(ChangeSignature("python", source, "fetch", "retries, url, *, timeout=10, headers=None") ==
          "def fetch(retries, url, *, timeout=10, headers=None):\n"
          "    pass\n"
          "fetch(5, u, timeout=2)\n"
          "fetch/*declined*/(u)\n"
          "fetch/*declined*/(url=u)\n");
    // Moving a parameter across the `*` changes how every caller has to pass it.
    CHECK(ChangeSignature("python", source, "fetch", "url, retries=3, timeout=10, *, verify=True").starts_with("declined: "));
}

TEST_CASE("Ruby change-signature covers methods, keywords and initialize", "[ChangeSignature]") {
    const std::string source = "class Mailer\n"
                               "  def initialize(host, port = 25)\n"
                               "  end\n"
                               "  def deliver(to, subject, cc: nil, bcc: nil)\n"
                               "  end\n"
                               "end\n"
                               "m = Mailer.new(\"smtp\", 587)\n"
                               "m.deliver(\"a@b\", \"hi\", bcc: \"x@y\")\n"
                               "m.deliver \"c@d\", \"yo\"\n";
    CHECK(ChangeSignature("ruby", source, "deliver", "subject, to, cc: nil, reply_to: nil") ==
          "class Mailer\n"
          "  def initialize(host, port = 25)\n"
          "  end\n"
          "  def deliver(subject, to, cc: nil, reply_to: nil)\n"
          "  end\n"
          "end\n"
          "m = Mailer.new(\"smtp\", 587)\n"
          "m.deliver(\"hi\", \"a@b\")\n"
          "m.deliver /*declined*/\"c@d\", \"yo\"\n");
    CHECK(ChangeSignature("ruby", source, "initialize", "port = 25, host") ==
          "class Mailer\n"
          "  def initialize(port = 25, host)\n"
          "  end\n"
          "  def deliver(to, subject, cc: nil, bcc: nil)\n"
          "  end\n"
          "end\n"
          "m = Mailer.new(587, \"smtp\")\n"
          "m.deliver(\"a@b\", \"hi\", bcc: \"x@y\")\n"
          "m.deliver \"c@d\", \"yo\"\n");
}

TEST_CASE("Lua change-signature supplies self through colon calls", "[ChangeSignature]") {
    const std::string source = "local M = {}\n"
                               "function M.scale(self, v, factor) end\n"
                               "function M:shift(dx, dy) end\n"
                               "local function add(a, b) end\n"
                               "obj:scale(2, 3)\n"
                               "M.scale(obj, 4, 5)\n"
                               "obj:shift(1, 2)\n"
                               "M.shift(obj, 1, 2)\n"
                               "add(1, 2)\n";
    CHECK(ChangeSignature("lua", source, "scale", "self, factor, v") ==
          "local M = {}\n"
          "function M.scale(self, factor, v) end\n"
          "function M:shift(dx, dy) end\n"
          "local function add(a, b) end\n"
          "obj:scale(3, 2)\n"
          "M.scale(obj, 5, 4)\n"
          "obj:shift(1, 2)\n"
          "M.shift(obj, 1, 2)\n"
          "add(1, 2)\n");
    // A colon-defined function called with a dot passes self explicitly: one
    // argument more than the list shows, so that call is left alone.
    CHECK(ChangeSignature("lua", source, "shift", "dy, dx") ==
          "local M = {}\n"
          "function M.scale(self, v, factor) end\n"
          "function M:shift(dy, dx) end\n"
          "local function add(a, b) end\n"
          "obj:scale(2, 3)\n"
          "M.scale(obj, 4, 5)\n"
          "obj:shift(2, 1)\n"
          "M.shift/*declined*/(obj, 1, 2)\n"
          "add(1, 2)\n");
}

TEST_CASE("Scala change-signature covers methods, curried lists and class constructors", "[ChangeSignature]") {
    const std::string source = "def add(a: Int, b: Int = 2): Int = a + b\n"
                               "def curry(a: Int, b: Int)(c: Int): Int = a\n"
                               "class Box(w: Int, h: Int)\n"
                               "add(1, 3); add(1, b = 4); curry(1, 2)(3); new Box(5, 6); Box(7, 8)\n";
    CHECK(ChangeSignature("scala", source, "add", "b: Int = 2, a: Int") ==
          "def add(b: Int = 2, a: Int): Int = a + b\n"
          "def curry(a: Int, b: Int)(c: Int): Int = a\n"
          "class Box(w: Int, h: Int)\n"
          "add(3, 1); add/*declined*/(1, b = 4); curry(1, 2)(3); new Box(5, 6); Box(7, 8)\n");
    CHECK(ChangeSignature("scala", source, "curry", "b: Int, a: Int") ==
          "def add(a: Int, b: Int = 2): Int = a + b\n"
          "def curry(b: Int, a: Int)(c: Int): Int = a\n"
          "class Box(w: Int, h: Int)\n"
          "add(1, 3); add(1, b = 4); curry(2, 1)(3); new Box(5, 6); Box(7, 8)\n");
    CHECK(ChangeSignature("scala", source, "Box", "h: Int, w: Int") ==
          "def add(a: Int, b: Int = 2): Int = a + b\n"
          "def curry(a: Int, b: Int)(c: Int): Int = a\n"
          "class Box(h: Int, w: Int)\n"
          "add(1, 3); add(1, b = 4); curry(1, 2)(3); new Box(6, 5); Box(8, 7)\n");
}

TEST_CASE("R change-signature reads named commas and declines named calls", "[ChangeSignature]") {
    const std::string source = "scale <- function(v, factor = 2) v * factor\n"
                               "scale(1, 3)\n"
                               "scale(v = 1)\n"
                               "stats::scale(4, 5)\n";
    CHECK(ChangeSignature("r", source, "scale", "factor = 2, v") ==
          "scale <- function(factor = 2, v) v * factor\n"
          "scale(3, 1)\n"
          "scale/*declined*/(v = 1)\n"
          "stats::scale(5, 4)\n");
}

TEST_CASE("GDScript change-signature covers plain and attribute calls", "[ChangeSignature]") {
    const std::string source = "func move(dx: int, dy := 0):\n"
                               "\tpass\n"
                               "func _ready():\n"
                               "\tmove(1, 2)\n"
                               "\tplayer.move(3, 4)\n";
    CHECK(ChangeSignature("gdscript", source, "move", "dy := 0, dx: int, speed = 1.0") ==
          "func move(dy := 0, dx: int, speed = 1.0):\n"
          "\tpass\n"
          "func _ready():\n"
          "\tmove(2, 1, 1.0)\n"
          "\tplayer.move(4, 3, 1.0)\n");
}

TEST_CASE("Dart change-signature reads optional groups and keeps named arguments by name", "[ChangeSignature]") {
    const std::string source = "int scale(int v, [int factor = 2]) => v * factor;\n"
                               "void send(String to, {String? cc, int retries = 3}) {}\n"
                               "class Box { Box(this.w, this.h); int w; int h; }\n"
                               "void main() {\n"
                               "  scale(1, 3); scale(4); m.scale(5, 6);\n"
                               "  send('a', retries: 1, cc: 'b');\n"
                               "  var b = Box(1, 2); var c = new Box(3, 4); var d = const Box(5, 6);\n"
                               "}\n";
    CHECK(ChangeSignature("dart", source, "scale", "int factor, int v") ==
          "int scale(int factor, int v) => v * factor;\n"
          "void send(String to, {String? cc, int retries = 3}) {}\n"
          "class Box { Box(this.w, this.h); int w; int h; }\n"
          "void main() {\n"
          "  scale(3, 1); scale/*declined*/(4); m.scale(6, 5);\n"
          "  send('a', retries: 1, cc: 'b');\n"
          "  var b = Box(1, 2); var c = new Box(3, 4); var d = const Box(5, 6);\n"
          "}\n");
    // Dropping `cc` drops its named argument; `urgent` needs nothing at the call.
    CHECK(ChangeSignature("dart", source, "send", "String to, {int retries = 3, bool urgent = false}") ==
          "int scale(int v, [int factor = 2]) => v * factor;\n"
          "void send(String to, {int retries = 3, bool urgent = false}) {}\n"
          "class Box { Box(this.w, this.h); int w; int h; }\n"
          "void main() {\n"
          "  scale(1, 3); scale(4); m.scale(5, 6);\n"
          "  send('a', retries: 1);\n"
          "  var b = Box(1, 2); var c = new Box(3, 4); var d = const Box(5, 6);\n"
          "}\n");
    CHECK(ChangeSignature("dart", source, "Box", "this.h, this.w") ==
          "int scale(int v, [int factor = 2]) => v * factor;\n"
          "void send(String to, {String? cc, int retries = 3}) {}\n"
          "class Box { Box(this.h, this.w); int w; int h; }\n"
          "void main() {\n"
          "  scale(1, 3); scale(4); m.scale(5, 6);\n"
          "  send('a', retries: 1, cc: 'b');\n"
          "  var b = Box(2, 1); var c = new Box(4, 3); var d = const Box(6, 5);\n"
          "}\n");
}

TEST_CASE("CUDA, HLSL, Objective-C and GLSL change-signature borrow C and C++'s queries", "[ChangeSignature]") {
    const std::string c       = "float mix(float a, float b) { return a + b; }\n"
                                "void run(void) { mix(1.0, 2.0); }\n";
    const std::string swapped = "float mix(float b, float a) { return a + b; }\n"
                                "void run(void) { mix(2.0, 1.0); }\n";
    for (const std::string_view language : {"cuda", "hlsl", "objc", "glsl"}) {
        INFO(language);
        CHECK(ChangeSignature(language, c, "mix", "float b, float a") == swapped);
    }
}

TEST_CASE("Groovy change-signature reorders and declines map-argument calls", "[ChangeSignature]") {
    const std::string source = "def scale(int v, factor = 2) { v * factor }\n"
                               "scale(1, 3)\n"
                               "obj.scale(4, 5)\n"
                               "scale(v: 1)\n";
    CHECK(ChangeSignature("groovy", source, "scale", "factor = 2, int v") ==
          "def scale(factor = 2, int v) { v * factor }\n"
          "scale(3, 1)\n"
          "obj.scale(5, 4)\n"
          "scale/*declined*/(v: 1)\n");
}

TEST_CASE("Crystal change-signature covers methods and initialize", "[ChangeSignature]") {
    const std::string source = "class Box\n"
                               "  def initialize(w : Int32, h : Int32)\n"
                               "  end\n"
                               "  def scale(v, factor = 2)\n"
                               "  end\n"
                               "end\n"
                               "b = Box.new(1, 2)\n"
                               "b.scale(3, 4)\n"
                               "b.scale(v: 5)\n";
    CHECK(ChangeSignature("crystal", source, "scale", "factor = 2, v") ==
          "class Box\n"
          "  def initialize(w : Int32, h : Int32)\n"
          "  end\n"
          "  def scale(factor = 2, v)\n"
          "  end\n"
          "end\n"
          "b = Box.new(1, 2)\n"
          "b.scale(4, 3)\n"
          "b.scale/*declined*/(v: 5)\n");
    CHECK(ChangeSignature("crystal", source, "initialize", "h : Int32, w : Int32") ==
          "class Box\n"
          "  def initialize(h : Int32, w : Int32)\n"
          "  end\n"
          "  def scale(v, factor = 2)\n"
          "  end\n"
          "end\n"
          "b = Box.new(2, 1)\n"
          "b.scale(3, 4)\n"
          "b.scale(v: 5)\n");
}

TEST_CASE("D change-signature covers plain and member calls", "[ChangeSignature]") {
    const std::string source = "int scale(int v, int factor = 2) { return v * factor; }\n"
                               "void f() { scale(1, 3); obj.scale(4, 5); scale(v: 1); }\n";
    CHECK(ChangeSignature("d", source, "scale", "int factor = 2, int v") ==
          "int scale(int factor = 2, int v) { return v * factor; }\n"
          "void f() { scale(3, 1); obj.scale(5, 4); scale/*declined*/(v: 1); }\n");
}

TEST_CASE("Julia change-signature leaves a definition's own signature alone as a call", "[ChangeSignature]") {
    const std::string source = "function scale(v, factor=2)\n"
                               "    v * factor\n"
                               "end\n"
                               "scale(1, 3)\n"
                               "Base.scale(4, 5)\n"
                               "scale(1, factor=3)\n";
    CHECK(ChangeSignature("julia", source, "scale", "factor=2, v") ==
          "function scale(factor=2, v)\n"
          "    v * factor\n"
          "end\n"
          "scale(3, 1)\n"
          "Base.scale(5, 4)\n"
          "scale/*declined*/(1, factor=3)\n");
}

TEST_CASE("V change-signature reorders and drops", "[ChangeSignature]") {
    const std::string source = "fn scale(v int, factor int) int { return v * factor }\n"
                               "fn main() { scale(1, 3) obj.scale(4, 5) scale(...xs) }\n";
    CHECK(ChangeSignature("v", source, "scale", "factor int, v int") ==
          "fn scale(factor int, v int) int { return v * factor }\n"
          "fn main() { scale(3, 1) obj.scale(5, 4) scale/*declined*/(...xs) }\n");
}

TEST_CASE("Nim change-signature keeps the dot-supplied first parameter first", "[ChangeSignature]") {
    const std::string source = "proc scale(s: Shape, v: int, factor = 2): int = v\n"
                               "scale(sh, 1, 3)\n"
                               "sh.scale(4, 5)\n"
                               "scale sh, 6, 7\n";
    CHECK(ChangeSignature("nim", source, "scale", "s: Shape, factor = 2, v: int") ==
          "proc scale(s: Shape, factor = 2, v: int): int = v\n"
          "scale(sh, 3, 1)\n"
          "sh.scale(5, 4)\n"
          "scale /*declined*/sh, 6, 7\n");
    CHECK(ChangeSignature("nim", source, "scale", "v: int, s: Shape, factor = 2").starts_with("declined: "));
}

// Solidity, Vala and Odin write a definition's parameters or a call's
// arguments flat, as its own children after the paren.
TEST_CASE("Solidity change-signature reorders a flat parameter list and its calls", "[ChangeSignature]") {
    const std::string source = "contract Vault {\n"
                               "  function deposit(address who, uint amount) public returns (bool) { return true; }\n"
                               "  function run() public { deposit(msg.sender, 5); this.deposit(owner, 1); deposit({who: a, amount: 2}); }\n"
                               "}\n";
    CHECK(ChangeSignature("solidity", source, "deposit", "uint amount, address who") ==
          "contract Vault {\n"
          "  function deposit(uint amount, address who) public returns (bool) { return true; }\n"
          "  function run() public { deposit(5, msg.sender); this.deposit(1, owner); deposit/*declined*/({who: a, amount: 2}); }\n"
          "}\n");
}

TEST_CASE("Vala change-signature handles methods, defaults and creation methods", "[ChangeSignature]") {
    const std::string source = "int scale (int value, int factor = 2) requires (value > 0) { return value * factor; }\n"
                               "void main () { scale (3, 4); obj.scale (5, 6); }\n";
    CHECK(ChangeSignature("vala", source, "scale", "int factor, int value, bool clamp = false") ==
          "int scale (int factor, int value, bool clamp = false) requires (value > 0) { return value * factor; }\n"
          "void main () { scale (4, 3, false); obj.scale (6, 5, false); }\n");

    const std::string creation = "class Widget { public Widget (string name, int size) {} }\n"
                                 "void main () { var w = new Widget (\"a\", 3); }\n";
    CHECK(ChangeSignature("vala", creation, "Widget", "int size, string name") ==
          "class Widget { public Widget (int size, string name) {} }\n"
          "void main () { var w = new Widget (3, \"a\"); }\n");
}

TEST_CASE("Odin change-signature rewrites flat call arguments and declines named ones", "[ChangeSignature]") {
    const std::string source = "clamp :: proc(v: int, lo := 0) -> int { return v }\n"
                               "main :: proc() { clamp(5, 1); m.clamp(7); clamp(v = 2) }\n"; // m.clamp(7) is short: declined
    CHECK(ChangeSignature("odin", source, "clamp", "lo := 0, v: int") ==
          "clamp :: proc(lo := 0, v: int) -> int { return v }\n"
          "main :: proc() { clamp(1, 5); m.clamp/*declined*/(7); clamp/*declined*/(v = 2) }\n");
}

TEST_CASE("Swift change-signature moves labels with their arguments and labels new defaults", "[ChangeSignature]") {
    const std::string source = "func greet(name n: String, _ times: Int = 1, loud: Bool) {}\n"
                               "greet(name: \"a\", 2, loud: true)\n"
                               "host.greet(name: who, 3, loud: false)\n"
                               "greet(name: \"b\", 1, loud: false) { print(\"done\") }\n";
    CHECK(ChangeSignature("swift", source, "greet", "loud: Bool, name n: String, _ times: Int = 1, style s: Int = 0, _ tag: String = \"\"") ==
          "func greet(loud: Bool, name n: String, _ times: Int = 1, style s: Int = 0, _ tag: String = \"\") {}\n"
          "greet(loud: true, name: \"a\", 2, style: 0, \"\")\n"
          "host.greet(loud: false, name: who, 3, style: 0, \"\")\n"
          "greet(loud: false, name: \"b\", 1, style: 0, \"\") { print(\"done\") }\n");
}
