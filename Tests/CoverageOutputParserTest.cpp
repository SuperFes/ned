#include <catch2/catch_test_macros.hpp>

#include <string>

#include "Editor/Coverage/CoverageOutputParser.h"

using ned::editor::coverage::CoverageReport;
using ned::editor::coverage::FileCoverage;
using ned::editor::coverage::LineStatus;
using ned::editor::coverage::ParseLcovInfo;

namespace {

const LineStatus* StatusForLine(const FileCoverage& file, std::size_t line, LineStatus& out) {
    for (const auto& lineCoverage : file.lines) {
        if (lineCoverage.line == line) {
            out = lineCoverage.Status();
            return &out;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("ParseLcovInfo parses a single SF/DA record", "[Coverage]") {
    const std::string    info   = "TN:\n"
                                  "SF:/home/user/project/src/foo.cpp\n"
                                  "DA:3,5\n"
                                  "DA:4,0\n"
                                  "LF:2\n"
                                  "LH:1\n"
                                  "end_of_record\n";
    const CoverageReport report = ParseLcovInfo(info);
    REQUIRE(report.size() == 1);
    REQUIRE(report[0].path == "/home/user/project/src/foo.cpp");
    REQUIRE(report[0].lines.size() == 2);

    LineStatus status{};
    REQUIRE(StatusForLine(report[0], 2, status)); // DA:3 -> 0-based line 2
    REQUIRE(status == LineStatus::Covered);
    REQUIRE(StatusForLine(report[0], 3, status)); // DA:4 -> 0-based line 3
    REQUIRE(status == LineStatus::Uncovered);
}

TEST_CASE("ParseLcovInfo derives Partial from BRDA branch data", "[Coverage]") {
    const std::string    info   = "SF:src/foo.cpp\n"
                                  "DA:5,5\n"
                                  "BRDA:5,0,0,5\n"
                                  "BRDA:5,0,1,0\n"
                                  "end_of_record\n";
    const CoverageReport report = ParseLcovInfo(info);
    REQUIRE(report.size() == 1);
    LineStatus status{};
    REQUIRE(StatusForLine(report[0], 4, status));
    REQUIRE(status == LineStatus::Partial);
}

TEST_CASE("ParseLcovInfo treats BRDA '-' taken as not-taken, not covered", "[Coverage]") {
    const std::string    info   = "SF:src/foo.cpp\n"
                                  "DA:1,3\n"
                                  "BRDA:1,0,0,-\n"
                                  "BRDA:1,0,1,3\n"
                                  "end_of_record\n";
    const CoverageReport report = ParseLcovInfo(info);
    LineStatus           status{};
    REQUIRE(StatusForLine(report[0], 0, status));
    REQUIRE(status == LineStatus::Partial); // one of two branches never reached
}

TEST_CASE("ParseLcovInfo merges multiple SF: blocks for the same path", "[Coverage]") {
    const std::string    info   = "SF:src/foo.cpp\n"
                                  "DA:1,2\n"
                                  "end_of_record\n"
                                  "SF:src/foo.cpp\n"
                                  "DA:1,3\n"
                                  "DA:2,0\n"
                                  "end_of_record\n";
    const CoverageReport report = ParseLcovInfo(info);
    REQUIRE(report.size() == 1);
    REQUIRE(report[0].lines.size() == 2);
    LineStatus status{};
    REQUIRE(StatusForLine(report[0], 0, status));
    REQUIRE(status == LineStatus::Covered); // 2 + 3 == 5 hits, summed across both blocks
    for (const auto& line : report[0].lines) {
        if (line.line == 0) {
            REQUIRE(line.hitCount == 5);
        }
    }
}

TEST_CASE("ParseLcovInfo flushes a record with no trailing end_of_record", "[Coverage]") {
    const std::string    info   = "SF:src/foo.cpp\n"
                                  "DA:1,1\n";
    const CoverageReport report = ParseLcovInfo(info);
    REQUIRE(report.size() == 1);
    REQUIRE(report[0].lines.size() == 1);
}

TEST_CASE("ParseLcovInfo on unrecognized input yields an empty report", "[Coverage]") {
    REQUIRE(ParseLcovInfo("this is not lcov output at all\njust some text\n").empty());
    REQUIRE(ParseLcovInfo("").empty());
}

TEST_CASE("ParseLcovInfo ignores an SF: block with no DA: records", "[Coverage]") {
    const std::string info = "SF:src/empty.cpp\n"
                             "end_of_record\n";
    REQUIRE(ParseLcovInfo(info).empty());
}
