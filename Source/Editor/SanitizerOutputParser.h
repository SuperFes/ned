//
// Debugging-wishlist follow-up (ROADMAP.md's Maybelist): a generic
// "sanitizer crash/report output to a diagnostics buffer" parser, the same
// TestOutputParser.h/TestResultsBuffer.h shape (pure, hand-rolled line
// scanning pinned against real captured tool output -- no std::regex, this
// codebase's own TestRun/ precedent) applied to ASan/UBSan/TSan/MSan/LSan
// output instead of a test framework's. Pays for itself immediately: ned
// already builds and tests itself under -DNED_ENABLE_SANITIZERS=ON (see
// CLAUDE.md's Build section), so a sanitizer finding surfacing in ned's own
// ctest run gets no durable, clickable record today beyond scrolling
// "*test output*" by eye.
//
// Every sanitizer runtime shares one anchor line by design (print_summary=1
// default): "SUMMARY: <Tool>: <kind> [<file>:<line>[:<col>] in <symbol>]" --
// exactly one per report. That's what this parser keys on; a UBSan report
// additionally carries a richer "<file>:<line>[:<col>]: runtime error:
// <message>" line immediately above its SUMMARY line, which this parser
// prefers as the finding's message when the two share a file:line. A
// LeakSanitizer byte-count SUMMARY ("24 byte(s) leaked in 1 allocation(s).")
// carries no location at all -- file stays empty, still a valid finding.
//
// Scope, deliberately not chased further (TestOutputParser.h's own
// per-format-caveat precedent): only SUMMARY-anchored reports are found, so
// a UBSan run under -fsanitize-recover=all whose individual runtime-error
// lines print without ever reaching a matching SUMMARY (unusual --
// print_summary defaults on) would be missed; LeakSanitizer's individual
// "Direct leak of N byte(s) ... allocated from:" stack frames aren't walked
// for a per-leak location, only the one trailing count SUMMARY.
//

#ifndef NED_EDITOR_SANITIZEROUTPUTPARSER_H
#define NED_EDITOR_SANITIZEROUTPUTPARSER_H

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace ned::editor {

struct SanitizerFinding {
    std::string tool; // "AddressSanitizer", "UndefinedBehaviorSanitizer", "ThreadSanitizer", "MemorySanitizer", "LeakSanitizer", ...
                       // -- verbatim from the SUMMARY line, whatever the runtime itself named.
    std::string message; // The UBSan "runtime error: ..." text when paired with a matching
                          // preceding line sharing the same file:line, else the SUMMARY
                          // line's own kind text ("heap-buffer-overflow", "data race", a
                          // leak byte-count sentence, ...).
    std::string symbol; // Trailing "in <symbol>" from the SUMMARY line; empty when absent
                         // (a LeakSanitizer count summary has none).
    std::string file;   // Empty when the report carries no location at all.
    std::size_t line   = 0; // 1-based; 0 = unknown/absent
    std::size_t column = 0; // 1-based; 0 = unknown/absent

    [[nodiscard]] bool operator==(const SanitizerFinding&) const = default;
};

// Pure, testable, never throws: scans output for SUMMARY lines and returns
// one SanitizerFinding per match, in the order they appear. Output that
// carries no sanitizer report at all (an ordinary clean test run, unrelated
// tool output) yields an empty vector -- "unrecognized input is not an
// error", the same contract TestOutputParser.h's own per-format parsers
// follow.
[[nodiscard]] std::vector<SanitizerFinding> ParseSanitizerOutput(std::string_view output);

} // namespace ned::editor

#endif // NED_EDITOR_SANITIZEROUTPUTPARSER_H
