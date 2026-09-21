#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>

#include "Editor/FormatBreak.h"
#include "Editor/FormatEdit.h"
#include "Editor/FormatRules.h"
#include "Editor/Mode.h"
#include "Text/Buffer.h"

using ned::editor::ApplyFormatTextEdits;
using ned::editor::ComputeBreakEdits;
using ned::editor::FormatCapture;
using ned::editor::Mode;
using ned::editor::PhpMode;
using ned::editor::SetBreakAfter;
using ned::editor::SetBreakBefore;
using ned::text::Buffer;

TEST_CASE("php-mode's format.janet names break.control on every continuation keyword", "[FormatBreak]") {
    const Mode        mode = PhpMode();
    const std::string text =
        "<?php\nif ($x) {\n    a();\n} else {\n    b();\n}\ntry {\n    c();\n} catch (E $e) {\n} finally {\n}\ndo {\n} while ($x);\n";

    std::string found;
    for (const FormatCapture& capture : mode.formatCaptures(text)) {
        if (capture.name == "break.control")
            found += text.substr(capture.startByte, capture.endByte - capture.startByte) + " ";
    }
    INFO("break.control captures: " << found);
    CHECK(found == "else catch finally while ");
}

namespace {

struct BreakRulesGuard {
    ~BreakRulesGuard() {
        SetBreakBefore("break.control", std::nullopt);
        SetBreakAfter("break.control", std::nullopt);
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
    SetBreakBefore("break.control", true);

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
    SetBreakBefore("break.control", false);

    const Mode mode = PhpMode();
    CHECK(Formatted(mode, "<?php\nif ($x) {\n}    else {\n}\n") == "<?php\nif ($x) {\n} else {\n}\n");

    const std::string alreadyBroken = "<?php\nif ($x) {\n}\nelse {\n}\n";
    CHECK(Formatted(mode, alreadyBroken) == alreadyBroken);
}

TEST_CASE("break-before is idempotent", "[FormatBreak]") {
    const BreakRulesGuard guard;
    SetBreakBefore("break.control", true);

    const Mode        mode  = PhpMode();
    const std::string once  = Formatted(mode, "<?php\nif ($x) {\n} else {\n}\n");
    const std::string twice = Formatted(mode, once);
    CHECK(once == twice);
}

TEST_CASE("break-before leaves a comment in the gap where it is and breaks after it", "[FormatBreak]") {
    // Only the whitespace run touching the keyword is rewritten, so inserting
    // a newline can never displace the comment.
    const BreakRulesGuard guard;
    SetBreakBefore("break.control", true);

    const Mode mode = PhpMode();
    CHECK(Formatted(mode, "<?php\nif ($x) {\n} /* done */ else {\n}\n") ==
          "<?php\nif ($x) {\n} /* done */\nelse {\n}\n");
}

TEST_CASE("break-before false never joins a gap that already spans lines", "[FormatBreak]") {
    // The hazard this protects against: joining "} // done" and "else" would
    // comment the keyword out. Same call collapse-simple already makes.
    const BreakRulesGuard guard;
    SetBreakBefore("break.control", false);

    const Mode        mode   = PhpMode();
    const std::string source = "<?php\nif ($x) {\n} // done\nelse {\n}\n";
    CHECK(Formatted(mode, source) == source);
}

TEST_CASE("An unconfigured break.control capture contributes nothing", "[FormatBreak]") {
    const Mode        mode   = PhpMode();
    const std::string source = "<?php\nif ($x) {\n} else {\n}\n";
    CHECK(Formatted(mode, source) == source);
}
