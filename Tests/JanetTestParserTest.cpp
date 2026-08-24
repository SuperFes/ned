#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string>

#include "Editor/TestRun/TestRunConfig.h"
#include "Janet/EditorBindings.h"
#include "Janet/Environment.h"
#include "JanetTestSupport.h"

using ned::editor::testrun::RegisteredTestParser;
using ned::editor::testrun::RegisterTestParser;
using ned::editor::testrun::TestResult;
using ned::janet::Environment;
using ned::janet::InstallEditorBindings;

namespace {

struct ParserResetGuard {
    explicit ParserResetGuard(std::string name) : name_(std::move(name)) {
    }
    ~ParserResetGuard() {
        RegisterTestParser(name_, {});
    }
    std::string name_;
};

} // namespace

TEST_CASE("ned/register-test-parser wraps a Janet fn into a resolvable parser", "[JanetTestParser]") {
    ParserResetGuard guard("janet-test-parser-simple");
    Environment&     env = ned_tests::TestEnvironment();
    InstallEditorBindings(env);

    env.DoString(R"(
      (ned/register-test-parser "janet-test-parser-simple"
        (fn [output]
          (def results @[])
          (each line (string/split "\n" output)
            (when (string/has-prefix? "FAIL " line)
              (array/push results @{:name (string/slice line 5)
                                    :status :failed
                                    :file "tests/thing.txt"
                                    :line 12
                                    :message "it broke"}))
            (when (string/has-prefix? "PASS " line)
              (array/push results @{:name (string/slice line 5) :status :passed})))
          results))
    )");

    const auto parser = RegisteredTestParser("janet-test-parser-simple");
    REQUIRE(parser.has_value());

    const auto outcome = (*parser)("PASS test_one\nFAIL test_two\nnoise line\n");
    REQUIRE(outcome.parsedOk);
    REQUIRE_FALSE(outcome.failuresOnly);
    REQUIRE(outcome.results.size() == 2);
    REQUIRE(outcome.results[0].name == "test_one");
    REQUIRE(outcome.results[0].status == TestResult::Status::Passed);
    REQUIRE(outcome.results[1].name == "test_two");
    REQUIRE(outcome.results[1].status == TestResult::Status::Failed);
    REQUIRE(outcome.results[1].file == "tests/thing.txt");
    REQUIRE(outcome.results[1].line == 12);
    REQUIRE(outcome.results[1].message == "it broke");
    REQUIRE(outcome.passed == 1);
    REQUIRE(outcome.failed == 1);
}

TEST_CASE("A Janet parser may return the table form with failures-only metadata", "[JanetTestParser]") {
    ParserResetGuard guard("janet-test-parser-table");
    Environment&     env = ned_tests::TestEnvironment();
    InstallEditorBindings(env);

    env.DoString(R"(
      (ned/register-test-parser "janet-test-parser-table"
        (fn [output]
          @{:results [@{:name "only_failure" :status :failed}]
            :failures-only true
            :passed 41}))
    )");

    const auto outcome = (*RegisteredTestParser("janet-test-parser-table"))("");
    REQUIRE(outcome.failuresOnly);
    REQUIRE(outcome.passed == 41); // the fn's own count, not derived from the results list
    REQUIRE(outcome.failed == 1);
    REQUIRE(outcome.results.size() == 1);
}

TEST_CASE("Malformed Janet parser entries degrade instead of crashing", "[JanetTestParser]") {
    ParserResetGuard guard("janet-test-parser-malformed");
    Environment&     env = ned_tests::TestEnvironment();
    InstallEditorBindings(env);

    env.DoString(R"(
      (ned/register-test-parser "janet-test-parser-malformed"
        (fn [output]
          [@{:status :failed}                       # no :name -- skipped
           "not a table at all"                     # wrong type -- skipped
           @{:name "kept" :status :weird-keyword}   # unknown status -- defaults to failed
           @{:name "no-status"}]))                  # missing status -- defaults to failed
    )");

    const auto outcome = (*RegisteredTestParser("janet-test-parser-malformed"))("");
    REQUIRE(outcome.results.size() == 2);
    REQUIRE(outcome.results[0].name == "kept");
    REQUIRE(outcome.results[0].status == TestResult::Status::Failed);
    REQUIRE(outcome.results[1].name == "no-status");
    REQUIRE(outcome.results[1].status == TestResult::Status::Failed);
}

TEST_CASE("An erroring Janet parser surfaces as a thrown parse failure", "[JanetTestParser]") {
    ParserResetGuard guard("janet-test-parser-error");
    Environment&     env = ned_tests::TestEnvironment();
    InstallEditorBindings(env);

    env.DoString(R"(
      (ned/register-test-parser "janet-test-parser-error"
        (fn [output] (error "deliberate parser failure")))
    )");

    const auto parser = RegisteredTestParser("janet-test-parser-error");
    REQUIRE(parser.has_value());
    REQUIRE_THROWS_AS((*parser)("anything"), std::runtime_error);
}

TEST_CASE("Registering nil clears a Janet parser", "[JanetTestParser]") {
    Environment& env = ned_tests::TestEnvironment();
    InstallEditorBindings(env);

    env.DoString(R"((ned/register-test-parser "janet-test-parser-cleared" (fn [output] [])))");
    REQUIRE(RegisteredTestParser("janet-test-parser-cleared").has_value());

    env.DoString(R"((ned/register-test-parser "janet-test-parser-cleared" nil))");
    REQUIRE_FALSE(RegisteredTestParser("janet-test-parser-cleared").has_value());
}
