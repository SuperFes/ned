#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <string>

#include "Editor/Coverage/CoverageConfig.h"

using ned::editor::coverage::ClearCoverageReport;
using ned::editor::coverage::CoverageFile;
using ned::editor::coverage::CoverageReportGeneration;
using ned::editor::coverage::CurrentCoverageReport;
using ned::editor::coverage::LoadCoverageReport;
using ned::editor::coverage::SetCoverageFile;

TEST_CASE("CoverageFile round-trips and clears on empty", "[Coverage]") {
    SetCoverageFile("");
    REQUIRE_FALSE(CoverageFile().has_value());

    SetCoverageFile("/tmp/coverage-config-test.info");
    REQUIRE(CoverageFile() == "/tmp/coverage-config-test.info");

    SetCoverageFile("");
    REQUIRE_FALSE(CoverageFile().has_value());
}

TEST_CASE("LoadCoverageReport throws when no file is configured", "[Coverage]") {
    SetCoverageFile("");
    REQUIRE_THROWS_AS(LoadCoverageReport(), std::runtime_error);
}

TEST_CASE("LoadCoverageReport throws when the configured file can't be opened", "[Coverage]") {
    SetCoverageFile("/nonexistent/path/coverage-config-test-missing.info");
    REQUIRE_THROWS_AS(LoadCoverageReport(), std::runtime_error);
}

TEST_CASE("LoadCoverageReport parses the configured file and bumps the generation", "[Coverage]") {
    const std::string path = "/tmp/coverage-config-test-real.info";
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << "SF:src/foo.cpp\nDA:1,2\nend_of_record\n";
    }
    SetCoverageFile(path);

    const std::size_t before = CoverageReportGeneration();
    LoadCoverageReport();
    REQUIRE(CoverageReportGeneration() > before);
    const auto report = CurrentCoverageReport();
    REQUIRE(report.size() == 1);
    REQUIRE(report[0].path == "src/foo.cpp");
}

TEST_CASE("ClearCoverageReport empties the report and bumps the generation", "[Coverage]") {
    const std::string path = "/tmp/coverage-config-test-clear.info";
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << "SF:src/foo.cpp\nDA:1,2\nend_of_record\n";
    }
    SetCoverageFile(path);
    LoadCoverageReport();
    REQUIRE_FALSE(CurrentCoverageReport().empty());

    const std::size_t before = CoverageReportGeneration();
    ClearCoverageReport();
    REQUIRE(CoverageReportGeneration() > before);
    REQUIRE(CurrentCoverageReport().empty());
}
