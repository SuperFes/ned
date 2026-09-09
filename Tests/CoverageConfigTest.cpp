#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <string>

#include "Editor/Coverage/Config.h"

using ned::editor::coverage::ClearCoverageReport;
using ned::editor::coverage::File;
using ned::editor::coverage::ReportGeneration;
using ned::editor::coverage::CurrentCoverageReport;
using ned::editor::coverage::LoadCoverageReport;
using ned::editor::coverage::SetFile;

TEST_CASE("File round-trips and clears on empty", "[Coverage]") {
    SetFile("");
    REQUIRE_FALSE(File().has_value());

    SetFile("/tmp/coverage-config-test.info");
    REQUIRE(File() == "/tmp/coverage-config-test.info");

    SetFile("");
    REQUIRE_FALSE(File().has_value());
}

TEST_CASE("LoadCoverageReport throws when no file is configured", "[Coverage]") {
    SetFile("");
    REQUIRE_THROWS_AS(LoadCoverageReport(), std::runtime_error);
}

TEST_CASE("LoadCoverageReport throws when the configured file can't be opened", "[Coverage]") {
    SetFile("/nonexistent/path/coverage-config-test-missing.info");
    REQUIRE_THROWS_AS(LoadCoverageReport(), std::runtime_error);
}

TEST_CASE("LoadCoverageReport parses the configured file and bumps the generation", "[Coverage]") {
    const std::string path = "/tmp/coverage-config-test-real.info";
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << "SF:src/foo.cpp\nDA:1,2\nend_of_record\n";
    }
    SetFile(path);

    const std::size_t before = ReportGeneration();
    LoadCoverageReport();
    REQUIRE(ReportGeneration() > before);
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
    SetFile(path);
    LoadCoverageReport();
    REQUIRE_FALSE(CurrentCoverageReport().empty());

    const std::size_t before = ReportGeneration();
    ClearCoverageReport();
    REQUIRE(ReportGeneration() > before);
    REQUIRE(CurrentCoverageReport().empty());
}
