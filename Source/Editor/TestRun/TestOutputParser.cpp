#include "TestOutputParser.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <unordered_map>
#include <unordered_set>

#include <nlohmann/json.hpp>

namespace ned::editor::testrun {

namespace {

    std::vector<std::string_view> SplitLines(std::string_view text) {
        std::vector<std::string_view> lines;
        std::size_t                   start = 0;
        while (start <= text.size()) {
            const std::size_t end  = text.find('\n', start);
            std::string_view  line = end == std::string_view::npos ? text.substr(start) : text.substr(start, end - start);
            if (!line.empty() && line.back() == '\r') {
                line.remove_suffix(1);
            }
            lines.push_back(line);
            if (end == std::string_view::npos) {
                break;
            }
            start = end + 1;
        }
        return lines;
    }

    std::string_view Trim(std::string_view s) {
        while (!s.empty() && (std::isspace(static_cast<unsigned char>(s.front())) != 0)) {
            s.remove_prefix(1);
        }
        while (!s.empty() && (std::isspace(static_cast<unsigned char>(s.back())) != 0)) {
            s.remove_suffix(1);
        }
        return s;
    }

    // True when the trimmed line is a run of at least minRun of c and nothing
    // else -- Catch2's dash/dot/equals rule lines.
    bool IsRuleOf(std::string_view line, char c, std::size_t minRun) {
        const std::string_view t = Trim(line);
        if (t.size() < minRun) {
            return false;
        }
        return std::ranges::all_of(t, [c](char ch) { return ch == c; });
    }

    std::optional<std::size_t> ParseSize(std::string_view s) {
        s                    = Trim(s);
        std::size_t value    = 0;
        const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
        if (ec != std::errc() || ptr != s.data() + s.size()) {
            return std::nullopt;
        }
        return value;
    }

    std::optional<double> ParseDouble(std::string_view s) {
        s                    = Trim(s);
        double value         = 0.0;
        const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
        if (ec != std::errc() || ptr != s.data() + s.size()) {
            return std::nullopt;
        }
        return value;
    }

    // Splits a "path:123" tail (optionally followed by ": rest") out of s.
    // Returns {path, line, rest-after-second-colon-or-empty}. The path may
    // itself contain colons -- the *last* ":<digits>" pair wins, matching
    // BufferView's own result-line regex posture.
    struct FileLineRest {
        std::string_view path;
        std::size_t      line = 0;
        std::string_view rest;
    };

    std::optional<FileLineRest> SplitFileLine(std::string_view s) {
        // Scan colon positions right-to-left looking for ":<digits>" ending at
        // a boundary (end of string, or a following ':' / whitespace).
        for (std::size_t colon = s.rfind(':'); colon != std::string_view::npos && colon > 0;
             colon             = s.rfind(':', colon - 1)) {
            std::size_t digitsEnd = colon + 1;
            while (digitsEnd < s.size() && (std::isdigit(static_cast<unsigned char>(s[digitsEnd])) != 0)) {
                ++digitsEnd;
            }
            if (digitsEnd == colon + 1) {
                continue; // no digits after this colon
            }
            std::string_view rest;
            if (digitsEnd < s.size()) {
                if (s[digitsEnd] == ':') {
                    rest = Trim(s.substr(digitsEnd + 1));
                }
                else {
                    continue; // digits ran into non-colon text -- not a line number
                }
            }
            const auto lineNumber = ParseSize(s.substr(colon + 1, digitsEnd - colon - 1));
            if (!lineNumber || *lineNumber == 0) {
                continue;
            }
            return FileLineRest{.path = Trim(s.substr(0, colon)), .line = *lineNumber, .rest = rest};
        }
        return std::nullopt;
    }

    void CountFromResults(TestRunOutcome& outcome) {
        for (const TestResult& result : outcome.results) {
            switch (result.status) {
                case TestResult::Status::Passed:
                    ++outcome.passed;
                    break;
                case TestResult::Status::Failed:
                    ++outcome.failed;
                    break;
                case TestResult::Status::Skipped:
                    ++outcome.skipped;
                    break;
            }
        }
    }

} // namespace

TestRunOutcome ParseCtest(std::string_view output) {
    TestRunOutcome                               outcome{.format = "ctest"};
    std::unordered_map<std::string, std::size_t> resultIndexByName;
    bool                                         inFailedTrailer = false;

    for (const std::string_view rawLine : SplitLines(output)) {
        const std::string_view line = Trim(rawLine);

        // "2/4 Test #2: FailingTest ......................***Failed    0.00 sec"
        const std::size_t testMark = line.find(" Test #");
        if (testMark != std::string_view::npos && !line.empty() && (std::isdigit(static_cast<unsigned char>(line.front())) != 0)) {
            const std::size_t colon = line.find(':', testMark);
            if (colon != std::string_view::npos) {
                std::string_view  rest    = Trim(line.substr(colon + 1));
                const std::size_t nameEnd = rest.find(' ');
                if (nameEnd != std::string_view::npos) {
                    TestResult result;
                    result.name = std::string(rest.substr(0, nameEnd));
                    rest        = rest.substr(nameEnd);
                    // Skip the dot padding; the status text follows it
                    // immediately ("***Failed") or after spaces ("Passed").
                    std::size_t statusStart = rest.find_first_not_of(" .");
                    if (statusStart != std::string_view::npos) {
                        std::string_view status = rest.substr(statusStart);
                        // Trailing "   0.00 sec" -> duration.
                        if (status.ends_with("sec")) {
                            const std::string_view withoutSec = Trim(status.substr(0, status.rfind("sec")));
                            const std::size_t      numStart   = withoutSec.find_last_of(' ');
                            const auto             seconds =
                                ParseDouble(numStart == std::string_view::npos ? withoutSec : withoutSec.substr(numStart));
                            if (seconds) {
                                result.durationMs = *seconds * 1000.0;
                                status            = Trim(numStart == std::string_view::npos ? std::string_view{}
                                                                                            : withoutSec.substr(0, numStart));
                            }
                        }
                        if (status.starts_with("***")) {
                            status.remove_prefix(3);
                            if (status.starts_with("Skipped") || status.starts_with("Not Run")) {
                                result.status = TestResult::Status::Skipped;
                            }
                            else {
                                result.status  = TestResult::Status::Failed;
                                result.message = std::string(Trim(status)); // "Failed", "Timeout", "Exception: SegFault"
                            }
                        }
                        else if (status.starts_with("Passed")) {
                            result.status = TestResult::Status::Passed;
                        }
                        else {
                            continue; // dots but no recognizable status -- not a test line after all
                        }
                        resultIndexByName[result.name] = outcome.results.size();
                        outcome.results.push_back(std::move(result));
                        outcome.parsedOk = true;
                    }
                }
            }
            continue;
        }

        if (line.starts_with("The following tests FAILED")) {
            inFailedTrailer = true;
            continue;
        }
        if (inFailedTrailer) {
            // "  2 - FailingTest (Failed)" -- upgrade the bare "Failed"
            // message with ctest's own parenthesized reason.
            const std::size_t dash = line.find(" - ");
            if (!line.empty() && (std::isdigit(static_cast<unsigned char>(line.front())) != 0) && dash != std::string_view::npos) {
                std::string_view  nameAndReason = line.substr(dash + 3);
                const std::size_t paren         = nameAndReason.rfind(" (");
                if (paren != std::string_view::npos && nameAndReason.ends_with(")")) {
                    const std::string name(Trim(nameAndReason.substr(0, paren)));
                    const auto        it = resultIndexByName.find(name);
                    if (it != resultIndexByName.end()) {
                        outcome.results[it->second].message =
                            std::string(nameAndReason.substr(paren + 2, nameAndReason.size() - paren - 3));
                    }
                }
            }
            else if (line.empty()) {
                inFailedTrailer = false;
            }
            continue;
        }

        if (line.find("% tests passed,") != std::string_view::npos) {
            outcome.parsedOk = true; // counts still come from the per-test lines -- see header comment
        }
    }

    CountFromResults(outcome);
    return outcome;
}

TestRunOutcome ParseCatch2(std::string_view output) {
    TestRunOutcome                      outcome{.format = "catch2", .failuresOnly = true};
    const std::vector<std::string_view> lines = SplitLines(output);
    std::unordered_set<std::string>     seenNames; // SECTION variants re-print the same test case name

    std::size_t i = 0;
    while (i < lines.size()) {
        if (!IsRuleOf(lines[i], '-', 70)) {
            // Summary shapes, outside any block.
            const std::string_view t = Trim(lines[i]);
            if (t.starts_with("test cases:")) {
                // "test cases: 3 | 1 passed | 2 failed [| 1 skipped]"
                std::string_view  rest  = t.substr(std::string_view("test cases:").size());
                std::size_t       start = 0;
                const std::string restStr(rest);
                while (start < restStr.size()) {
                    std::size_t       end     = restStr.find('|', start);
                    std::string_view  segment = Trim(std::string_view(restStr).substr(
                        start, end == std::string::npos ? std::string::npos : end - start));
                    const std::size_t space   = segment.find(' ');
                    if (space != std::string_view::npos) {
                        const auto             count = ParseSize(segment.substr(0, space));
                        const std::string_view label = Trim(segment.substr(space));
                        if (count) {
                            if (label.starts_with("passed")) {
                                outcome.passed = *count;
                            }
                            else if (label.starts_with("failed")) {
                                outcome.failed = *count;
                            }
                            else if (label.starts_with("skipped")) {
                                outcome.skipped = *count;
                            }
                        }
                    }
                    if (end == std::string::npos) {
                        break;
                    }
                    start = end + 1;
                }
                outcome.parsedOk = true;
            }
            else if (t.starts_with("All tests passed")) {
                // "All tests passed (5 assertions in 3 test cases)"
                const std::size_t in = t.rfind(" in ");
                if (in != std::string_view::npos) {
                    std::string_view  tail  = t.substr(in + 4);
                    const std::size_t space = tail.find(' ');
                    if (const auto count = ParseSize(tail.substr(0, space))) {
                        outcome.passed = *count;
                    }
                }
                outcome.parsedOk = true;
            }
            ++i;
            continue;
        }

        // Failure block: dashes / name (+ section lines) / dashes / file:line / dots.
        std::size_t j = i + 1;
        while (j < lines.size() && Trim(lines[j]).empty()) {
            ++j;
        }
        if (j >= lines.size()) {
            break;
        }
        TestResult result;
        result.name = std::string(Trim(lines[j]));
        ++j;
        while (j < lines.size() && !IsRuleOf(lines[j], '-', 70)) {
            ++j; // SECTION path lines -- the test case name alone is what gutter matching needs
        }
        if (j >= lines.size()) {
            break;
        }
        ++j; // past the closing dash rule
        if (j < lines.size()) {
            if (const auto loc = SplitFileLine(Trim(lines[j])); loc && loc->rest.empty()) {
                result.file = std::string(loc->path);
                result.line = loc->line;
                ++j;
            }
        }
        if (j < lines.size() && IsRuleOf(lines[j], '.', 70)) {
            ++j;
        }

        // Block body up to the next block/summary rule: assertion lines. A
        // block with neither a FAILED nor a SKIPPED line (a passing block
        // under -s) is dropped rather than misreported.
        result.status   = TestResult::Status::Failed;
        bool sawSkip    = false;
        bool sawOutcome = false;
        while (j < lines.size() && !IsRuleOf(lines[j], '-', 70) && !IsRuleOf(lines[j], '=', 70)) {
            const std::string_view t   = Trim(lines[j]);
            const auto             loc = SplitFileLine(t);
            if (loc && (loc->rest.starts_with("FAILED") || loc->rest.starts_with("SKIPPED"))) {
                sawOutcome = true;
                if (loc->rest.starts_with("SKIPPED")) {
                    sawSkip = true;
                }
                if (result.message.empty()) {
                    // Join the assertion's own following lines ("CHECK( a == 2 )",
                    // "with expansion:", "1 == 2") into one line.
                    std::string message;
                    for (std::size_t k = j + 1; k < lines.size() && message.size() < 160; ++k) {
                        const std::string_view detail = Trim(lines[k]);
                        if (detail.empty()) {
                            break;
                        }
                        if (!message.empty()) {
                            message += ' ';
                        }
                        message += std::string(detail);
                    }
                    result.message = std::move(message);
                }
            }
            ++j;
        }
        if (sawSkip) {
            result.status = TestResult::Status::Skipped;
        }
        if (sawOutcome && seenNames.insert(result.name).second) {
            outcome.results.push_back(std::move(result));
            outcome.parsedOk = true;
        }
        i = j;
    }

    // The summary's failed/skipped counts are authoritative when present
    // (they count test cases; blocks can be missed on truncated output) --
    // otherwise fall back to what was actually parsed.
    if (outcome.failed == 0 && outcome.skipped == 0) {
        for (const TestResult& result : outcome.results) {
            if (result.status == TestResult::Status::Skipped) {
                ++outcome.skipped;
            }
            else {
                ++outcome.failed;
            }
        }
    }
    return outcome;
}

namespace {

    // pytest names a class-scoped test "TestThings::test_method_fails" in node
    // ids but "TestThings.test_method_fails" in FAILURES headers -- compare with
    // "::" collapsed to "." so both spellings agree.
    std::string PytestCanonicalName(std::string_view name) {
        std::string canonical;
        canonical.reserve(name.size());
        for (std::size_t i = 0; i < name.size(); ++i) {
            if (name[i] == ':' && i + 1 < name.size() && name[i + 1] == ':') {
                canonical += '.';
                ++i;
            }
            else {
                canonical += name[i];
            }
        }
        return canonical;
    }

} // namespace

TestRunOutcome ParsePytest(std::string_view output) {
    TestRunOutcome outcome{.format = "pytest"};

    struct FailureDetail {
        std::string file;
        std::size_t line = 0;
        std::string message;
    };
    std::unordered_map<std::string, FailureDetail> detailByCanonicalName;
    std::unordered_map<std::string, std::size_t>   resultIndexByCanonicalName;

    enum class Section { None,
                         Failures,
                         ShortSummary };
    Section       section = Section::None;
    std::string   currentFailureName;
    FailureDetail currentDetail;
    bool          sawVerboseLines = false;

    const auto commitCurrentFailure = [&] {
        if (!currentFailureName.empty()) {
            detailByCanonicalName[PytestCanonicalName(currentFailureName)] = std::move(currentDetail);
        }
        currentFailureName.clear();
        currentDetail = {};
    };

    for (const std::string_view rawLine : SplitLines(output)) {
        const std::string_view line = Trim(rawLine);

        if (line.starts_with('=') && line.ends_with('=') && line.size() > 8) {
            commitCurrentFailure();
            if (line.find(" test session starts ") != std::string_view::npos) {
                outcome.parsedOk = true;
                section          = Section::None;
            }
            else if (line.find(" FAILURES ") != std::string_view::npos || line.find(" ERRORS ") != std::string_view::npos) {
                section = Section::Failures;
            }
            else if (line.find(" short test summary info ") != std::string_view::npos) {
                section = Section::ShortSummary;
            }
            else {
                // Possibly the final "=== 3 failed, 3 passed, 1 skipped in 0.03s ===".
                const std::string_view inner = Trim(line.substr(line.find_first_not_of('='),
                                                                line.find_last_not_of('=') + 1 - line.find_first_not_of('=')));
                if (inner.find(" in ") != std::string_view::npos) {
                    std::size_t pos = 0;
                    while (pos < inner.size()) {
                        while (pos < inner.size() && (std::isdigit(static_cast<unsigned char>(inner[pos])) == 0)) {
                            ++pos;
                        }
                        std::size_t numEnd = pos;
                        while (numEnd < inner.size() && (std::isdigit(static_cast<unsigned char>(inner[numEnd])) != 0)) {
                            ++numEnd;
                        }
                        if (numEnd >= inner.size() || inner[numEnd] != ' ') {
                            pos = numEnd + 1;
                            continue;
                        }
                        const auto       count = ParseSize(inner.substr(pos, numEnd - pos));
                        std::string_view word  = inner.substr(numEnd + 1);
                        word                   = word.substr(0, word.find_first_of(" ,"));
                        if (count) {
                            if (word == "passed") {
                                outcome.passed = *count;
                            }
                            else if (word == "failed" || word == "error" || word == "errors") {
                                outcome.failed += *count;
                            }
                            else if (word == "skipped" || word == "xfailed") {
                                outcome.skipped += *count;
                            }
                            else if (word == "xpassed") {
                                outcome.passed += *count;
                            }
                        }
                        pos = numEnd + 1;
                    }
                    outcome.parsedOk = true;
                    section          = Section::None;
                }
            }
            continue;
        }

        if (section == Section::Failures) {
            if (line.size() > 4 && line.starts_with("__") && line.ends_with("__")) {
                commitCurrentFailure();
                const std::size_t nameStart = line.find_first_not_of('_');
                const std::size_t nameEnd   = line.find_last_not_of('_');
                if (nameStart != std::string_view::npos && nameEnd > nameStart) {
                    currentFailureName = std::string(Trim(line.substr(nameStart, nameEnd + 1 - nameStart)));
                }
                continue;
            }
            if (!currentFailureName.empty()) {
                if (line.starts_with("E ")) {
                    if (currentDetail.message.empty()) {
                        currentDetail.message = std::string(Trim(line.substr(1)));
                    }
                }
                else if (const auto loc = SplitFileLine(line); loc && !loc->rest.empty() && loc->rest.find(' ') == std::string_view::npos) {
                    // "test_sample.py:7: AssertionError" -- the last such
                    // frame before the next header is the assertion site.
                    currentDetail.file = std::string(loc->path);
                    currentDetail.line = loc->line;
                }
            }
            continue;
        }

        // Verbose per-test line: "file::node STATUS [(reason)] [ NN%]".
        const std::size_t firstSpace = line.find(' ');
        if (firstSpace != std::string_view::npos) {
            const std::string_view first     = line.substr(0, firstSpace);
            std::string_view       second    = Trim(line.substr(firstSpace));
            const std::size_t      secondEnd = second.find(' ');
            const std::string_view status    = second.substr(0, secondEnd);

            if (section != Section::ShortSummary && first.find("::") != std::string_view::npos) {
                std::optional<TestResult::Status> mapped;
                if (status == "PASSED" || status == "XPASS") {
                    mapped = TestResult::Status::Passed;
                }
                else if (status == "FAILED" || status == "ERROR") {
                    mapped = TestResult::Status::Failed;
                }
                else if (status == "SKIPPED" || status == "XFAIL") {
                    mapped = TestResult::Status::Skipped;
                }
                if (mapped) {
                    TestResult        result;
                    const std::size_t sep = first.find("::");
                    result.file           = std::string(first.substr(0, sep));
                    result.name           = std::string(first.substr(sep + 2));
                    result.status         = *mapped;
                    if (secondEnd != std::string_view::npos) {
                        const std::string_view reason = Trim(second.substr(secondEnd));
                        if (reason.starts_with('(')) {
                            const std::size_t close = reason.find(')');
                            if (close != std::string_view::npos) {
                                result.message = std::string(reason.substr(1, close - 1));
                            }
                        }
                    }
                    resultIndexByCanonicalName[PytestCanonicalName(result.name)] = outcome.results.size();
                    outcome.results.push_back(std::move(result));
                    sawVerboseLines  = true;
                    outcome.parsedOk = true;
                    continue;
                }
            }

            // Short-summary line: "FAILED file::node - message".
            if ((first == "FAILED" || first == "ERROR") && second.find("::") != std::string_view::npos) {
                std::string_view  nodeId = second;
                std::string       message;
                const std::size_t dash = second.find(" - ");
                if (dash != std::string_view::npos) {
                    nodeId  = Trim(second.substr(0, dash));
                    message = std::string(Trim(second.substr(dash + 3)));
                }
                const std::size_t sep       = nodeId.find("::");
                const std::string canonical = PytestCanonicalName(nodeId.substr(sep + 2));
                if (const auto it = resultIndexByCanonicalName.find(canonical); it != resultIndexByCanonicalName.end()) {
                    if (outcome.results[it->second].message.empty()) {
                        outcome.results[it->second].message = std::move(message);
                    }
                }
                else {
                    TestResult result;
                    result.file                           = std::string(nodeId.substr(0, sep));
                    result.name                           = std::string(nodeId.substr(sep + 2));
                    result.status                         = TestResult::Status::Failed;
                    result.message                        = std::move(message);
                    resultIndexByCanonicalName[canonical] = outcome.results.size();
                    outcome.results.push_back(std::move(result));
                }
                outcome.parsedOk = true;
            }
        }
    }
    commitCurrentFailure();

    for (TestResult& result : outcome.results) {
        const auto it = detailByCanonicalName.find(PytestCanonicalName(result.name));
        if (it != detailByCanonicalName.end()) {
            if (it->second.line != 0) {
                result.file = it->second.file;
                result.line = it->second.line;
            }
            if (result.message.empty()) {
                result.message = it->second.message;
            }
        }
    }

    outcome.failuresOnly = !sawVerboseLines;
    if (sawVerboseLines) {
        outcome.passed = outcome.failed = outcome.skipped = 0;
        CountFromResults(outcome);
    }
    return outcome;
}

TestRunOutcome ParseGoTestJson(std::string_view output) {
    TestRunOutcome outcome{.format = "go-json"};

    struct PendingOutput {
        std::string accumulated;
    };
    std::unordered_map<std::string, PendingOutput>                   outputByKey; // key = package + '\n' + test
    std::unordered_map<std::string, std::unordered_set<std::string>> packagesByTestName;

    struct RawResult {
        std::string package;
        TestResult  result;
    };
    std::vector<RawResult> rawResults;

    for (const std::string_view line : SplitLines(output)) {
        const std::string_view t = Trim(line);
        if (t.empty() || t.front() != '{') {
            continue; // build errors and other non-JSON noise
        }
        const nlohmann::json event = nlohmann::json::parse(t, nullptr, /*allow_exceptions=*/false);
        if (event.is_discarded() || !event.is_object() || !event.contains("Action")) {
            continue;
        }
        outcome.parsedOk         = true;
        const std::string action = event.value("Action", "");
        const std::string test   = event.value("Test", "");
        if (test.empty()) {
            continue; // package-level event
        }
        const std::string package = event.value("Package", "");
        const std::string key     = package + '\n' + test;

        if (action == "output") {
            outputByKey[key].accumulated += event.value("Output", "");
            continue;
        }
        if (action != "pass" && action != "fail" && action != "skip") {
            continue;
        }

        TestResult result;
        result.name       = test;
        result.status     = action == "pass"   ? TestResult::Status::Passed
                            : action == "fail" ? TestResult::Status::Failed
                                               : TestResult::Status::Skipped;
        result.durationMs = event.value("Elapsed", 0.0) * 1000.0;

        // First "foo_test.go:12: message" frame in the test's own output --
        // basename-only, all go gives us.
        const auto it = outputByKey.find(key);
        if (it != outputByKey.end()) {
            for (const std::string_view outputLine : SplitLines(it->second.accumulated)) {
                const std::string_view frame = Trim(outputLine);
                const auto             loc   = SplitFileLine(frame);
                if (loc && loc->path.find(' ') == std::string_view::npos && loc->path.ends_with(".go")) {
                    result.file    = std::string(loc->path);
                    result.line    = loc->line;
                    result.message = std::string(loc->rest);
                    break;
                }
            }
        }

        packagesByTestName[result.name].insert(package);
        rawResults.push_back(RawResult{.package = package, .result = std::move(result)});
    }

    for (RawResult& raw : rawResults) {
        // The same test name in several packages of one module is real
        // (TestMain-style names especially) -- disambiguate only then, so
        // the common single-package case keeps clean names.
        if (packagesByTestName[raw.result.name].size() > 1 && !raw.package.empty()) {
            const std::size_t lastSlash = raw.package.rfind('/');
            raw.result.name             = raw.package.substr(lastSlash == std::string::npos ? 0 : lastSlash + 1) + "." + raw.result.name;
        }
        outcome.results.push_back(std::move(raw.result));
    }

    CountFromResults(outcome);
    return outcome;
}

TestRunOutcome ParseCargoTest(std::string_view output) {
    TestRunOutcome                               outcome{.format = "cargo"};
    std::unordered_map<std::string, std::size_t> resultIndexByName;

    const std::vector<std::string_view> lines = SplitLines(output);

    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string_view line = Trim(lines[i]);

        if (line.starts_with("running ") && (line.ends_with("tests") || line.ends_with("test"))) {
            outcome.parsedOk = true;
            continue;
        }

        // "test tests::it_fails ... FAILED" (doc-tests: "test src/lib.rs - add (line 3) ... ok")
        if (line.starts_with("test ")) {
            const std::size_t ellipsis = line.rfind(" ... ");
            if (ellipsis != std::string_view::npos) {
                const std::string_view name       = Trim(line.substr(5, ellipsis - 5));
                std::string_view       statusPart = Trim(line.substr(ellipsis + 5));
                statusPart                        = statusPart.substr(0, statusPart.find_first_of(" ,"));
                std::optional<TestResult::Status> mapped;
                if (statusPart == "ok") {
                    mapped = TestResult::Status::Passed;
                }
                else if (statusPart == "FAILED") {
                    mapped = TestResult::Status::Failed;
                }
                else if (statusPart == "ignored") {
                    mapped = TestResult::Status::Skipped;
                }
                if (mapped && !name.empty()) {
                    TestResult result;
                    result.name                    = std::string(name);
                    result.status                  = *mapped;
                    resultIndexByName[result.name] = outcome.results.size();
                    outcome.results.push_back(std::move(result));
                    outcome.parsedOk = true;
                }
            }
            continue;
        }

        // "---- tests::it_fails stdout ----" panic detail block.
        if (line.starts_with("---- ") && line.ends_with(" stdout ----")) {
            const std::string name(Trim(line.substr(5, line.size() - 5 - std::string_view(" stdout ----").size())));
            const auto        it = resultIndexByName.find(name);
            if (it == resultIndexByName.end()) {
                continue;
            }
            TestResult& result = outcome.results[it->second];
            for (std::size_t j = i + 1; j < lines.size(); ++j) {
                const std::string_view detail = Trim(lines[j]);
                if (detail.starts_with("---- ") || detail.starts_with("failures:")) {
                    break;
                }
                const std::size_t panicked = detail.find("panicked at ");
                if (panicked == std::string_view::npos) {
                    continue;
                }
                std::string_view after = detail.substr(panicked + std::string_view("panicked at ").size());
                // The panic location is "file:line:col" -- SplitFileLine's
                // rightmost-pair rule lands on the column, so re-split its
                // path half to peel the real line number out.
                const auto panicLocation = [](std::string_view text) -> std::optional<FileLineRest> {
                    const auto outer = SplitFileLine(text);
                    if (!outer) {
                        return std::nullopt;
                    }
                    if (const auto inner = SplitFileLine(outer->path); inner && inner->rest.empty()) {
                        return inner;
                    }
                    return outer;
                };
                if (after.starts_with('\'')) {
                    // Pre-1.73: "panicked at 'message', src/lib.rs:11:21"
                    const std::size_t closeQuote = after.rfind("', ");
                    if (closeQuote != std::string_view::npos) {
                        result.message = std::string(after.substr(1, closeQuote - 1));
                        if (const auto loc = panicLocation(after.substr(closeQuote + 3))) {
                            result.file = std::string(loc->path);
                            result.line = loc->line;
                        }
                    }
                }
                else {
                    // 1.73+: "panicked at src/lib.rs:11:21:" with the message on following lines.
                    if (const auto loc = panicLocation(after)) {
                        result.file = std::string(loc->path);
                        result.line = loc->line;
                    }
                    for (std::size_t k = j + 1; k < lines.size(); ++k) {
                        const std::string_view messageLine = Trim(lines[k]);
                        if (messageLine.empty() || messageLine.starts_with("note: run with")) {
                            break;
                        }
                        result.message = std::string(messageLine);
                        break;
                    }
                }
                break;
            }
            continue;
        }

        // "test result: FAILED. 1 passed; 2 failed; 1 ignored; ..." -- one
        // per test binary, summed.
        if (line.starts_with("test result:")) {
            std::size_t pos = line.find('.');
            if (pos != std::string_view::npos) {
                std::string_view  rest  = line.substr(pos + 1);
                std::size_t       start = 0;
                const std::string restStr(rest);
                while (start < restStr.size()) {
                    std::size_t       end     = restStr.find(';', start);
                    std::string_view  segment = Trim(std::string_view(restStr).substr(
                        start, end == std::string::npos ? std::string::npos : end - start));
                    const std::size_t space   = segment.find(' ');
                    if (space != std::string_view::npos) {
                        const auto             count = ParseSize(segment.substr(0, space));
                        const std::string_view label = Trim(segment.substr(space));
                        if (count) {
                            if (label.starts_with("passed")) {
                                outcome.passed += *count;
                            }
                            else if (label.starts_with("failed")) {
                                outcome.failed += *count;
                            }
                            else if (label.starts_with("ignored")) {
                                outcome.skipped += *count;
                            }
                        }
                    }
                    if (end == std::string::npos) {
                        break;
                    }
                    start = end + 1;
                }
                outcome.parsedOk = true;
            }
        }
    }

    if (outcome.passed == 0 && outcome.failed == 0 && outcome.skipped == 0) {
        CountFromResults(outcome);
    }
    return outcome;
}

namespace {

    std::string DecodeXmlEntities(std::string_view text) {
        std::string decoded;
        decoded.reserve(text.size());
        std::size_t i = 0;
        while (i < text.size()) {
            if (text[i] != '&') {
                decoded += text[i++];
                continue;
            }
            const std::size_t semi = text.find(';', i);
            if (semi == std::string_view::npos || semi - i > 10) {
                decoded += text[i++];
                continue;
            }
            const std::string_view entity = text.substr(i + 1, semi - i - 1);
            if (entity == "lt") {
                decoded += '<';
            }
            else if (entity == "gt") {
                decoded += '>';
            }
            else if (entity == "amp") {
                decoded += '&';
            }
            else if (entity == "quot") {
                decoded += '"';
            }
            else if (entity == "apos") {
                decoded += '\'';
            }
            else if (entity.starts_with('#')) {
                unsigned long codepoint = 0;
                try {
                    codepoint = entity.starts_with("#x") || entity.starts_with("#X")
                                    ? std::stoul(std::string(entity.substr(2)), nullptr, 16)
                                    : std::stoul(std::string(entity.substr(1)), nullptr, 10);
                }
                catch (...) {
                    codepoint = 0;
                }
                if (codepoint > 0 && codepoint < 128) {
                    decoded += static_cast<char>(codepoint);
                }
                else if (codepoint >= 128) {
                    decoded += ' '; // non-ASCII numeric references are rare in test names; degrade to a space
                }
            }
            else {
                decoded += std::string(text.substr(i, semi - i + 1));
                i = semi + 1;
                continue;
            }
            i = semi + 1;
        }
        return decoded;
    }

    std::string StripCdata(std::string_view text) {
        std::string stripped;
        std::size_t i = 0;
        while (i < text.size()) {
            const std::size_t open = text.find("<![CDATA[", i);
            if (open == std::string_view::npos) {
                stripped += DecodeXmlEntities(text.substr(i));
                break;
            }
            stripped += DecodeXmlEntities(text.substr(i, open - i));
            const std::size_t close = text.find("]]>", open);
            if (close == std::string_view::npos) {
                stripped += std::string(text.substr(open + 9));
                break;
            }
            stripped += std::string(text.substr(open + 9, close - open - 9));
            i = close + 3;
        }
        return stripped;
    }

    using XmlAttributes = std::unordered_map<std::string, std::string>;

    // Parses the attributes of a tag whose '<name' has already been consumed;
    // tag is everything up to (not including) the closing '>'.
    XmlAttributes ParseXmlAttributes(std::string_view tag) {
        XmlAttributes attributes;
        std::size_t   i = 0;
        while (i < tag.size()) {
            while (i < tag.size() && (std::isspace(static_cast<unsigned char>(tag[i])) != 0)) {
                ++i;
            }
            const std::size_t equals = tag.find('=', i);
            if (equals == std::string_view::npos) {
                break;
            }
            const std::string name(Trim(tag.substr(i, equals - i)));
            std::size_t       valueStart = equals + 1;
            while (valueStart < tag.size() && (std::isspace(static_cast<unsigned char>(tag[valueStart])) != 0)) {
                ++valueStart;
            }
            if (valueStart >= tag.size() || (tag[valueStart] != '"' && tag[valueStart] != '\'')) {
                break;
            }
            const char        quote    = tag[valueStart];
            const std::size_t valueEnd = tag.find(quote, valueStart + 1);
            if (valueEnd == std::string_view::npos) {
                break;
            }
            attributes[name] = DecodeXmlEntities(tag.substr(valueStart + 1, valueEnd - valueStart - 1));
            i                = valueEnd + 1;
        }
        return attributes;
    }

    std::string FirstLineOf(std::string_view text) {
        const std::string_view line = Trim(text.substr(0, text.find('\n')));
        return std::string(line);
    }

} // namespace

TestRunOutcome ParseJUnitXml(std::string_view output) {
    TestRunOutcome outcome{.format = "junit-xml"};

    std::size_t i = 0;
    while ((i = output.find("<testcase", i)) != std::string_view::npos) {
        const std::size_t afterName = i + std::string_view("<testcase").size();
        // Guard against matching "<testcases" or similar.
        if (afterName < output.size() && (std::isspace(static_cast<unsigned char>(output[afterName])) == 0) &&
            output[afterName] != '>' && output[afterName] != '/') {
            i = afterName;
            continue;
        }
        std::size_t tagEnd      = afterName;
        char        quoteState  = '\0';
        bool        selfClosing = false;
        while (tagEnd < output.size()) {
            const char c = output[tagEnd];
            if (quoteState != '\0') {
                if (c == quoteState) {
                    quoteState = '\0';
                }
            }
            else if (c == '"' || c == '\'') {
                quoteState = c;
            }
            else if (c == '>') {
                selfClosing = tagEnd > afterName && output[tagEnd - 1] == '/';
                break;
            }
            ++tagEnd;
        }
        if (tagEnd >= output.size()) {
            break;
        }

        const XmlAttributes attributes =
            ParseXmlAttributes(output.substr(afterName, tagEnd - afterName - (selfClosing ? 1 : 0)));
        TestResult result;
        const auto nameIt      = attributes.find("name");
        const auto classNameIt = attributes.find("classname");
        if (nameIt == attributes.end()) {
            i = tagEnd + 1;
            continue;
        }
        result.name = classNameIt != attributes.end() && !classNameIt->second.empty()
                          ? classNameIt->second + "::" + nameIt->second
                          : nameIt->second;
        if (const auto fileIt = attributes.find("file"); fileIt != attributes.end()) {
            result.file = fileIt->second;
        }
        if (const auto lineIt = attributes.find("line"); lineIt != attributes.end()) {
            if (const auto line = ParseSize(lineIt->second)) {
                result.line = *line;
            }
        }
        if (const auto timeIt = attributes.find("time"); timeIt != attributes.end()) {
            if (const auto seconds = ParseDouble(timeIt->second)) {
                result.durationMs = *seconds * 1000.0;
            }
        }
        result.status = TestResult::Status::Passed;

        std::size_t next = tagEnd + 1;
        if (!selfClosing) {
            const std::size_t      closeTag = output.find("</testcase>", next);
            const std::string_view body =
                output.substr(next, closeTag == std::string_view::npos ? std::string_view::npos : closeTag - next);
            const auto childInfo = [&](std::string_view childName) -> std::optional<std::string> {
                const std::size_t child = body.find(childName);
                if (child == std::string_view::npos) {
                    return std::nullopt;
                }
                std::size_t childTagEnd = body.find('>', child);
                if (childTagEnd == std::string_view::npos) {
                    return std::string();
                }
                const bool          childSelfClosing = body[childTagEnd - 1] == '/';
                const XmlAttributes childAttributes  = ParseXmlAttributes(
                    body.substr(child + childName.size(), childTagEnd - child - childName.size() - (childSelfClosing ? 1 : 0)));
                if (const auto messageIt = childAttributes.find("message"); messageIt != childAttributes.end()) {
                    return FirstLineOf(messageIt->second);
                }
                if (childSelfClosing) {
                    return std::string();
                }
                const std::size_t childClose = body.find("</", childTagEnd);
                if (childClose == std::string_view::npos) {
                    return std::string();
                }
                return FirstLineOf(StripCdata(body.substr(childTagEnd + 1, childClose - childTagEnd - 1)));
            };
            if (const auto failure = childInfo("<failure")) {
                result.status  = TestResult::Status::Failed;
                result.message = *failure;
            }
            else if (const auto error = childInfo("<error")) {
                result.status  = TestResult::Status::Failed;
                result.message = *error;
            }
            else if (const auto skipped = childInfo("<skipped")) {
                result.status  = TestResult::Status::Skipped;
                result.message = *skipped;
            }
            if (closeTag != std::string_view::npos) {
                next = closeTag + std::string_view("</testcase>").size();
            }
        }

        outcome.results.push_back(std::move(result));
        outcome.parsedOk = true;
        i                = next;
    }

    CountFromResults(outcome);
    return outcome;
}

TestRunOutcome ParsePhpUnit(std::string_view output) {
    TestRunOutcome outcome{.format = "phpunit", .failuresOnly = true};

    enum class BlockKind { None,
                           Failed,
                           Skipped,
                           Ignore }; // Ignore = risky/warning sections, not test outcomes
    BlockKind   currentKind = BlockKind::None;
    TestResult* current     = nullptr;
    std::size_t total       = 0;
    bool        sawTotals   = false;

    for (const std::string_view rawLine : SplitLines(output)) {
        const std::string_view line = Trim(rawLine);

        if (line.starts_with("PHPUnit ") && line.size() > 8 && (std::isdigit(static_cast<unsigned char>(line[8])) != 0)) {
            outcome.parsedOk = true;
            continue;
        }

        if (line.starts_with("There was ") || line.starts_with("There were ")) {
            current = nullptr;
            if (line.find("failure") != std::string_view::npos || line.find("error") != std::string_view::npos) {
                currentKind = BlockKind::Failed;
            }
            else if (line.find("skipped") != std::string_view::npos || line.find("incomplete") != std::string_view::npos) {
                currentKind = BlockKind::Skipped;
            }
            else {
                currentKind = BlockKind::Ignore;
            }
            continue;
        }

        // "1) Tests\FooTest::testBar" starts one block.
        if (!line.empty() && (std::isdigit(static_cast<unsigned char>(line.front())) != 0)) {
            const std::size_t paren = line.find(") ");
            if (paren != std::string_view::npos && paren < 6 && line.find("::") != std::string_view::npos &&
                currentKind != BlockKind::None) {
                current = nullptr;
                if (currentKind != BlockKind::Ignore) {
                    TestResult       result;
                    std::string_view name = Trim(line.substr(paren + 2));
                    // "Class::method with data set #0 (...)" -- strip the data-set
                    // suffix so every instance aggregates onto the one method.
                    if (const std::size_t dataSet = name.find(" with data set "); dataSet != std::string_view::npos) {
                        name = name.substr(0, dataSet);
                    }
                    result.name   = std::string(name);
                    result.status = currentKind == BlockKind::Failed ? TestResult::Status::Failed : TestResult::Status::Skipped;
                    outcome.results.push_back(std::move(result));
                    current          = &outcome.results.back();
                    outcome.parsedOk = true;
                }
                continue;
            }
        }

        // The "FAILURES!"/"OK"/"Tests:" summary tail ends whatever block was
        // open -- checked before block-content consumption so the counts are
        // never swallowed as a failure message.
        const bool isSummaryLine = line.starts_with("OK (") || line.starts_with("OK,") || line.starts_with("Tests: ") ||
                                   line == "FAILURES!" || line == "ERRORS!";
        if (isSummaryLine) {
            current     = nullptr;
            currentKind = BlockKind::None;
        }

        if (current != nullptr) {
            if (line.empty()) {
                continue;
            }
            // A bare "/path/File.php:42" trace frame ends the message; the
            // first frame is taken (PHPUnit 10+ prints the assertion frame
            // first; on 9.x this may be a deeper frame -- acceptable).
            const auto loc = SplitFileLine(line);
            if (loc && loc->rest.empty() && loc->path.ends_with(".php")) {
                if (current->line == 0) {
                    current->file = std::string(loc->path);
                    current->line = loc->line;
                }
                continue;
            }
            if (current->message.empty()) {
                current->message = std::string(line);
            }
            continue;
        }

        if (line.starts_with("OK (")) {
            // "OK (7 tests, 12 assertions)"
            const std::string_view inner = line.substr(4);
            if (const auto count = ParseSize(inner.substr(0, inner.find(' ')))) {
                outcome.passed = *count;
            }
            outcome.parsedOk = true;
            continue;
        }
        if (line.starts_with("Tests: ")) {
            // "Tests: 7, Assertions: 9, Failures: 2, Skipped: 1."
            std::string_view rest = line;
            if (rest.ends_with('.')) {
                rest.remove_suffix(1);
            }
            std::size_t       start = 0;
            const std::string restStr(rest);
            std::size_t       failed  = 0;
            std::size_t       skipped = 0;
            while (start < restStr.size()) {
                std::size_t       end     = restStr.find(',', start);
                std::string_view  segment = Trim(std::string_view(restStr).substr(
                    start, end == std::string::npos ? std::string::npos : end - start));
                const std::size_t colon   = segment.find(':');
                if (colon != std::string_view::npos) {
                    const std::string_view key   = Trim(segment.substr(0, colon));
                    const auto             count = ParseSize(segment.substr(colon + 1));
                    if (count) {
                        if (key == "Tests") {
                            total     = *count;
                            sawTotals = true;
                        }
                        else if (key == "Failures" || key == "Errors") {
                            failed += *count;
                        }
                        else if (key == "Skipped" || key == "Incomplete") {
                            skipped += *count;
                        }
                    }
                }
                if (end == std::string::npos) {
                    break;
                }
                start = end + 1;
            }
            if (sawTotals) {
                outcome.failed   = failed;
                outcome.skipped  = skipped;
                outcome.passed   = total >= failed + skipped ? total - failed - skipped : 0;
                outcome.parsedOk = true;
            }
        }
    }

    if (outcome.failed == 0 && outcome.skipped == 0 && !sawTotals) {
        for (const TestResult& result : outcome.results) {
            if (result.status == TestResult::Status::Skipped) {
                ++outcome.skipped;
            }
            else {
                ++outcome.failed;
            }
        }
    }
    return outcome;
}

std::optional<TestRunOutcome> ParseTestOutput(std::string_view format, std::string_view output) {
    if (format == "ctest") {
        return ParseCtest(output);
    }
    if (format == "catch2") {
        return ParseCatch2(output);
    }
    if (format == "pytest") {
        return ParsePytest(output);
    }
    if (format == "go-json") {
        return ParseGoTestJson(output);
    }
    if (format == "cargo") {
        return ParseCargoTest(output);
    }
    if (format == "junit-xml") {
        return ParseJUnitXml(output);
    }
    if (format == "phpunit") {
        return ParsePhpUnit(output);
    }
    return std::nullopt;
}

std::vector<std::string> BuiltInTestFormats() {
    return {"ctest", "catch2", "pytest", "go-json", "cargo", "junit-xml", "phpunit"};
}

} // namespace ned::editor::testrun
