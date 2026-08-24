#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "Editor/TestRun/TestRunConfig.h"

using ned::editor::testrun::RegisteredTestParser;
using ned::editor::testrun::RegisterTestParser;
using ned::editor::testrun::SetTestCommand;
using ned::editor::testrun::SetTestFilterCommand;
using ned::editor::testrun::SetTestResultsFile;
using ned::editor::testrun::SubstituteFilterTemplate;
using ned::editor::testrun::TestCommand;
using ned::editor::testrun::TestFilterCommand;
using ned::editor::testrun::TestResultsFile;
using ned::editor::testrun::TestRunOutcome;

TEST_CASE("TestCommand is nullopt until configured; empty argv clears", "[TestRun]") {
    REQUIRE_FALSE(TestCommand().has_value());

    SetTestCommand({"ctest", "--test-dir", "build"}, "ctest");
    const auto config = TestCommand();
    REQUIRE(config.has_value());
    REQUIRE(config->argv == std::vector<std::string>{"ctest", "--test-dir", "build"});
    REQUIRE(config->format == "ctest");

    SetTestCommand({"pytest", "-v"}, "pytest"); // re-register overwrites
    REQUIRE(TestCommand()->format == "pytest");

    SetTestCommand({}, "");
    REQUIRE_FALSE(TestCommand().has_value());
}

TEST_CASE("TestFilterCommand round-trips and clears on empty", "[TestRun]") {
    REQUIRE_FALSE(TestFilterCommand().has_value());

    SetTestFilterCommand({"ctest", "-R", "^{test}$"});
    REQUIRE(TestFilterCommand().has_value());

    SetTestFilterCommand({});
    REQUIRE_FALSE(TestFilterCommand().has_value());
}

TEST_CASE("SubstituteFilterTemplate replaces placeholders per argv element", "[TestRun]") {
    const auto argv = SubstituteFilterTemplate({"pytest", "-v", "-k", "{test}", "{file}::{test}"},
                                               "test with spaces [1]", "tests/test_x.py");
    REQUIRE(argv == std::vector<std::string>{"pytest", "-v", "-k", "test with spaces [1]",
                                             "tests/test_x.py::test with spaces [1]"});
}

TEST_CASE("SubstituteFilterTemplate leaves placeholder-free elements untouched", "[TestRun]") {
    const auto argv = SubstituteFilterTemplate({"cargo", "test"}, "tests::it_fails", "");
    REQUIRE(argv == std::vector<std::string>{"cargo", "test"});
}

TEST_CASE("TestResultsFile round-trips and clears on empty", "[TestRun]") {
    REQUIRE_FALSE(TestResultsFile().has_value());

    SetTestResultsFile("/tmp/results.xml");
    REQUIRE(TestResultsFile() == "/tmp/results.xml");

    SetTestResultsFile("");
    REQUIRE_FALSE(TestResultsFile().has_value());
}

TEST_CASE("RegisterTestParser registers, overwrites, and clears on empty fn", "[TestRun]") {
    REQUIRE_FALSE(RegisteredTestParser("test-run-config-test-custom").has_value());

    RegisterTestParser("test-run-config-test-custom", [](const std::string&) {
        TestRunOutcome outcome;
        outcome.format   = "first";
        outcome.parsedOk = true;
        return outcome;
    });
    auto parser = RegisteredTestParser("test-run-config-test-custom");
    REQUIRE(parser.has_value());
    REQUIRE((*parser)("").format == "first");

    RegisterTestParser("test-run-config-test-custom", [](const std::string&) {
        TestRunOutcome outcome;
        outcome.format = "second";
        return outcome;
    });
    REQUIRE((*RegisteredTestParser("test-run-config-test-custom"))("").format == "second");

    RegisterTestParser("test-run-config-test-custom", {});
    REQUIRE_FALSE(RegisteredTestParser("test-run-config-test-custom").has_value());
}
