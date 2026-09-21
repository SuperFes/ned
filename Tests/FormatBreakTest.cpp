#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>

#include "Editor/FormatBreak.h"
#include "Editor/FormatEdit.h"
#include "Editor/FormatRules.h"
#include "Editor/Mode.h"
#include "Text/Buffer.h"

using ned::editor::ApplyFormatTextEdits;
using ned::editor::CMode;
using ned::editor::ComputeBreakEdits;
using ned::editor::CppMode;
using ned::editor::CSharpMode;
using ned::editor::FormatCapture;
using ned::editor::GoMode;
using ned::editor::JavaMode;
using ned::editor::JavaScriptMode;
using ned::editor::KotlinMode;
using ned::editor::Mode;
using ned::editor::PhpMode;
using ned::editor::RustMode;
using ned::editor::SetBreakAfter;
using ned::editor::SetBreakBefore;
using ned::text::Buffer;

TEST_CASE("php-mode's format.janet names control.keyword on every continuation keyword", "[FormatBreak]") {
    const Mode        mode = PhpMode();
    const std::string text =
        "<?php\nif ($x) {\n    a();\n} else {\n    b();\n}\ntry {\n    c();\n} catch (E $e) {\n} finally {\n}\ndo {\n} while ($x);\n";

    std::string found;
    for (const FormatCapture& capture : mode.formatCaptures(text)) {
        if (capture.name == "control.keyword")
            found += text.substr(capture.startByte, capture.endByte - capture.startByte) + " ";
    }
    INFO("control.keyword captures: " << found);
    CHECK(found == "else catch finally while ");
}

namespace {

struct BreakRulesGuard {
    ~BreakRulesGuard() {
        SetBreakBefore("control.keyword", std::nullopt);
        SetBreakAfter("control.keyword", std::nullopt);
    }
};

std::string Formatted(const Mode& mode, const std::string& source) {
    Buffer buffer("t.php");
    buffer.InsertAtPoint(source);
    ApplyFormatTextEdits(buffer, ComputeBreakEdits(buffer.Text(), "php", mode.formatCaptures(buffer.Text())));
    return buffer.Text();
}

} // namespace

TEST_CASE("break-before puts a continuation keyword on its own line at the closer's column", "[FormatBreak]") {
    const BreakRulesGuard guard;
    SetBreakBefore("control.keyword", true);

    const Mode mode = PhpMode();
    CHECK(Formatted(mode, "<?php\nif ($x) {\n    a();\n} else {\n    b();\n}\n") ==
          "<?php\nif ($x) {\n    a();\n}\nelse {\n    b();\n}\n");

    // Indented one level in: the keyword inherits the closer's own column,
    // not the document's margin.
    CHECK(Formatted(mode, "<?php\nfunction f() {\n    if ($x) {\n    } else {\n    }\n}\n") ==
          "<?php\nfunction f() {\n    if ($x) {\n    }\n    else {\n    }\n}\n");
}

TEST_CASE("break-before false normalises horizontal whitespace only", "[FormatBreak]") {
    // It collapses a run on one line, and deliberately does NOT un-break a
    // keyword already on its own -- see the no-join rule in FormatBreak.h.
    const BreakRulesGuard guard;
    SetBreakBefore("control.keyword", false);

    const Mode mode = PhpMode();
    CHECK(Formatted(mode, "<?php\nif ($x) {\n}    else {\n}\n") == "<?php\nif ($x) {\n} else {\n}\n");

    const std::string alreadyBroken = "<?php\nif ($x) {\n}\nelse {\n}\n";
    CHECK(Formatted(mode, alreadyBroken) == alreadyBroken);
}

TEST_CASE("break-before is idempotent", "[FormatBreak]") {
    const BreakRulesGuard guard;
    SetBreakBefore("control.keyword", true);

    const Mode        mode  = PhpMode();
    const std::string once  = Formatted(mode, "<?php\nif ($x) {\n} else {\n}\n");
    const std::string twice = Formatted(mode, once);
    CHECK(once == twice);
}

TEST_CASE("break-before leaves a comment in the gap where it is and breaks after it", "[FormatBreak]") {
    // Only the whitespace run touching the keyword is rewritten, so inserting
    // a newline can never displace the comment.
    const BreakRulesGuard guard;
    SetBreakBefore("control.keyword", true);

    const Mode mode = PhpMode();
    CHECK(Formatted(mode, "<?php\nif ($x) {\n} /* done */ else {\n}\n") ==
          "<?php\nif ($x) {\n} /* done */\nelse {\n}\n");
}

TEST_CASE("break-before false never joins a gap that already spans lines", "[FormatBreak]") {
    // The hazard this protects against: joining "} // done" and "else" would
    // comment the keyword out. Same call collapse-simple already makes.
    const BreakRulesGuard guard;
    SetBreakBefore("control.keyword", false);

    const Mode        mode   = PhpMode();
    const std::string source = "<?php\nif ($x) {\n} // done\nelse {\n}\n";
    CHECK(Formatted(mode, source) == source);
}

TEST_CASE("An unconfigured control.keyword capture contributes nothing", "[FormatBreak]") {
    const Mode        mode   = PhpMode();
    const std::string source = "<?php\nif ($x) {\n} else {\n}\n";
    CHECK(Formatted(mode, source) == source);
}

namespace {

std::string KeywordsFound(const Mode& mode, const std::string& source) {
    std::string found;
    for (const FormatCapture& capture : mode.formatCaptures(source)) {
        if (capture.name == "control.keyword")
            found += source.substr(capture.startByte, capture.endByte - capture.startByte) + " ";
    }
    return found;
}

} // namespace

TEST_CASE("Every brace language names control.keyword on its own continuation keywords",
          "[FormatBreak]") {
    SECTION("cpp") {
        CHECK(KeywordsFound(CppMode(), "int main() {\n  if (x) {\n  } else {\n  }\n  try {\n  } catch (...) {\n  }\n  do {\n  } while (x);\n}\n") ==
              "else catch while ");
    }
    SECTION("c") {
        CHECK(KeywordsFound(CMode(), "int main() {\n  if (x) {\n  } else {\n  }\n  do {\n  } while (x);\n}\n") ==
              "else while ");
    }
    SECTION("javascript") {
        CHECK(KeywordsFound(JavaScriptMode(), "if (x) {\n} else {\n}\ntry {\n} catch (e) {\n} finally {\n}\ndo {\n} while (x);\n") ==
              "else catch finally while ");
    }
    SECTION("java") {
        CHECK(KeywordsFound(JavaMode(), "class C { void m() {\n  if (x) {\n  } else {\n  }\n  try {\n  } catch (E e) {\n  } finally {\n  }\n  do {\n  } while (x);\n} }\n") ==
              "else catch finally while ");
    }
    SECTION("csharp") {
        CHECK(KeywordsFound(CSharpMode(), "class C { void M() {\n  if (x) {\n  } else {\n  }\n  try {\n  } catch (E e) {\n  } finally {\n  }\n  do {\n  } while (x);\n} }\n") ==
              "else catch finally while ");
    }
    SECTION("rust") {
        CHECK(KeywordsFound(RustMode(), "fn m() {\n  if x {\n  } else {\n  }\n}\n") == "else ");
    }
    SECTION("go declares none, deliberately") {
        // `}` newline `else` is "syntax error: unexpected else" under Go's
        // automatic semicolon insertion -- not a style choice.
        CHECK(KeywordsFound(GoMode(), "func m() {\n\tif x {\n\t} else {\n\t}\n}\n").empty());
    }
    SECTION("kotlin") {
        CHECK(KeywordsFound(KotlinMode(), "fun m() {\n  if (x) {\n  } else {\n  }\n  try {\n  } catch (e: E) {\n  } finally {\n  }\n  do {\n  } while (x)\n}\n") ==
              "else catch finally while ");
    }
}

TEST_CASE("Language keys for every mode declaring control.keyword", "[FormatBreak]") {
    // The key a user writes in `"<language>/control.keyword"` has to be the
    // key the rule resolves under.
    CHECK(ned::editor::LanguageKeyForMode(CppMode()) == "cpp");
    CHECK(ned::editor::LanguageKeyForMode(CMode()) == "c");
    CHECK(ned::editor::LanguageKeyForMode(PhpMode()) == "php");
    CHECK(ned::editor::LanguageKeyForMode(JavaScriptMode()) == "javascript");
    CHECK(ned::editor::LanguageKeyForMode(JavaMode()) == "java");
    CHECK(ned::editor::LanguageKeyForMode(CSharpMode()) == "csharp");
    CHECK(ned::editor::LanguageKeyForMode(RustMode()) == "rust");
    CHECK(ned::editor::LanguageKeyForMode(KotlinMode()) == "kotlin");
}

TEST_CASE("Go refuses a keyword break even with a capture and a rule", "[FormatBreak]") {
    // go/format.janet declares no control.keyword capture, so this drives
    // the code guard directly with a synthetic one -- the second line of
    // defence, against a query added here by mistake.
    const BreakRulesGuard guard;
    SetBreakBefore("control.keyword", true);

    const std::string                text = "if x {\n} else {\n}\n";
    const std::vector<FormatCapture> captures{FormatCapture{"control.keyword", text.find("else"), text.find("else") + 4}};
    CHECK(ComputeBreakEdits(text, "go", captures).empty());
    CHECK_FALSE(ComputeBreakEdits(text, "cpp", captures).empty()); // the same input, unguarded
}
