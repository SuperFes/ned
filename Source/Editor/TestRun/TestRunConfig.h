//
// Structured test-runner integration: the project's test-command
// configuration plus the pluggable-parser registry. Mutex-guarded static
// state, mirroring Tasks/TaskConfig.h's exact shape -- same "you configure
// the tool, we shell out to it" trust boundary, same "re-registering
// overwrites, empty clears" convention. Unlike TaskConfig's name-keyed
// table there is one test command per project, not many -- running "the
// tests" is a single well-known action the way "run task <name>" isn't.
//
// The parser registry is what makes an unsupported framework a first-class
// citizen: ned/register-test-parser (EditorBindings.cpp) wraps a Janet
// function into a TestParserFn here, and TestRunner resolves the configured
// format against this registry *before* the built-in table, so a registered
// parser may deliberately shadow a built-in name (a user escape hatch when
// a built-in mis-parses their tool's output).
//

#ifndef NED_EDITOR_TESTRUN_TESTRUNCONFIG_H
#define NED_EDITOR_TESTRUN_TESTRUNCONFIG_H

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "TestResult.h"

namespace ned::editor::testrun {

struct TestCommandConfig {
    std::vector<std::string> argv;
    std::string              format; // a BuiltInTestFormats() name or a registered parser's
};

// Empty argv clears the registration.
void                                           SetTestCommand(std::vector<std::string> argv, std::string format);
[[nodiscard]] std::optional<TestCommandConfig> TestCommand();

// argv template for single-test runs (run-test-at-point / rerun-failed) --
// each element may contain "{test}" and "{file}" placeholders, substituted
// verbatim per element (never through a shell, so a test name containing
// spaces/brackets/quotes is safe by construction). Empty clears.
void                                                  SetTestFilterCommand(std::vector<std::string> argvTemplate);
[[nodiscard]] std::optional<std::vector<std::string>> TestFilterCommand();

// The pure substitution TestFilterCommand's template goes through --
// separate and exported so it's unit-testable without a runner.
[[nodiscard]] std::vector<std::string> SubstituteFilterTemplate(const std::vector<std::string>& argvTemplate,
                                                                const std::string& testName, const std::string& file);

// When set, TestRunner parses this file's contents after the run exits
// instead of the accumulated stdout/stderr -- how file-writing formats
// (JUnit XML via `pytest --junitxml`/`phpunit --log-junit`) are consumed.
// Empty clears.
void                                     SetTestResultsFile(std::string path);
[[nodiscard]] std::optional<std::string> TestResultsFile();

// A registered parser is invoked by TestRunner on the main thread only
// (TaskProcess already marshals its exit callback there) -- what makes a
// Janet-backed fn legal here at all; see JanetVcsProvider.h for the
// threading rule this inherits. An empty fn clears the name.
using TestParserFn = std::function<TestRunOutcome(const std::string& output)>;
void                                      RegisterTestParser(const std::string& name, TestParserFn fn);
[[nodiscard]] std::optional<TestParserFn> RegisteredTestParser(const std::string& name);

} // namespace ned::editor::testrun

#endif // NED_EDITOR_TESTRUN_TESTRUNCONFIG_H
