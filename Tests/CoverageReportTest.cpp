#include <catch2/catch_test_macros.hpp>

#include "Editor/Coverage/CoverageReport.h"

using ned::editor::coverage::CoverageReport;
using ned::editor::coverage::FileCoverage;
using ned::editor::coverage::FindFileCoverage;
using ned::editor::coverage::LineCoverage;
using ned::editor::coverage::LineStatus;

TEST_CASE("LineCoverage::Status derives Covered/Partial/Uncovered", "[Coverage]") {
    REQUIRE(LineCoverage{.line = 0, .hitCount = 0}.Status() == LineStatus::Uncovered);
    REQUIRE(LineCoverage{.line = 0, .hitCount = 5}.Status() == LineStatus::Covered);
    REQUIRE(LineCoverage{.line = 0, .hitCount = 5, .branchesTotal = 2, .branchesTaken = 2}.Status() == LineStatus::Covered);
    REQUIRE(LineCoverage{.line = 0, .hitCount = 5, .branchesTotal = 2, .branchesTaken = 1}.Status() == LineStatus::Partial);
    // Executed, but every branch on the line unreached is still Uncovered's
    // own case (hitCount governs first) -- branchesTaken == 0 with
    // hitCount > 0 is Partial, not a separate state.
    REQUIRE(LineCoverage{.line = 0, .hitCount = 5, .branchesTotal = 2, .branchesTaken = 0}.Status() == LineStatus::Partial);
}

TEST_CASE("FindFileCoverage matches an absolute SF: path exactly", "[Coverage]") {
    CoverageReport report;
    report.push_back(FileCoverage{.path = "/project/src/foo.cpp", .lines = {}});
    const FileCoverage* match = FindFileCoverage(report, "/project/src/foo.cpp", "/project");
    REQUIRE(match != nullptr);
    REQUIRE(match->path == "/project/src/foo.cpp");
}

TEST_CASE("FindFileCoverage resolves a relative SF: path against projectRoot", "[Coverage]") {
    CoverageReport report;
    report.push_back(FileCoverage{.path = "src/foo.cpp", .lines = {}});
    const FileCoverage* match = FindFileCoverage(report, "/project/src/foo.cpp", "/project");
    REQUIRE(match != nullptr);
    REQUIRE(match->path == "src/foo.cpp");
}

TEST_CASE("FindFileCoverage falls back to a unique basename match", "[Coverage]") {
    CoverageReport report;
    report.push_back(FileCoverage{.path = "/build/obj/some/weird/path/foo.cpp", .lines = {}});
    const FileCoverage* match = FindFileCoverage(report, "/project/src/foo.cpp", "/project");
    REQUIRE(match != nullptr);
    REQUIRE(match->path == "/build/obj/some/weird/path/foo.cpp");
}

TEST_CASE("FindFileCoverage returns nullptr for an ambiguous basename", "[Coverage]") {
    CoverageReport report;
    report.push_back(FileCoverage{.path = "/a/foo.cpp", .lines = {}});
    report.push_back(FileCoverage{.path = "/b/foo.cpp", .lines = {}});
    REQUIRE(FindFileCoverage(report, "/project/src/foo.cpp", "/project") == nullptr);
}

TEST_CASE("FindFileCoverage returns nullptr when nothing matches at all", "[Coverage]") {
    CoverageReport report;
    report.push_back(FileCoverage{.path = "/other/bar.cpp", .lines = {}});
    REQUIRE(FindFileCoverage(report, "/project/src/foo.cpp", "/project") == nullptr);
}

TEST_CASE("FindFileCoverage on an empty report returns nullptr", "[Coverage]") {
    const CoverageReport report;
    REQUIRE(FindFileCoverage(report, "/project/src/foo.cpp", "/project") == nullptr);
}
