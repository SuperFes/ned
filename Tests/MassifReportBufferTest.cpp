#include <catch2/catch_test_macros.hpp>

#include <string>

#include "Editor/MassifOutputParser.h"
#include "Editor/MassifReportBuffer.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"

using ned::editor::MassifProfile;
using ned::editor::MassifReportBufferName;
using ned::editor::MassifSnapshot;
using ned::editor::RebuildMassifReportBuffer;
using ned::text::Buffer;
using ned::text::BufferList;

namespace {

MassifProfile SampleProfile() {
    MassifProfile profile;
    profile.cmd      = "./a.out";
    profile.desc     = "--time-unit=B";
    profile.timeUnit = "B";
    profile.snapshots = {
        MassifSnapshot{.index = 0, .time = 0, .heapBytes = 0, .heapExtraBytes = 0, .stacksBytes = 0},
        MassifSnapshot{.index = 1, .time = 1024, .heapBytes = 1000, .heapExtraBytes = 24, .stacksBytes = 0},
        MassifSnapshot{.index      = 2,
                        .time       = 40960,
                        .heapBytes  = 40000,
                        .heapExtraBytes = 960,
                        .stacksBytes    = 0,
                        .isDetailed = true,
                        .isPeak     = true},
    };
    return profile;
}

} // namespace

TEST_CASE("RebuildMassifReportBuffer writes a read-only header, sparkline, and per-snapshot table",
          "[MassifReportBuffer]") {
    BufferList bufferList;
    Buffer&    buffer = RebuildMassifReportBuffer(bufferList, SampleProfile(), "/tmp/massif.out.123");

    REQUIRE(buffer.ReadOnly());
    REQUIRE(buffer.Name() == MassifReportBufferName());

    const std::string text = buffer.Text();
    REQUIRE(text.find("Massif profile: /tmp/massif.out.123") == 0);
    REQUIRE(text.find("Command: ./a.out") != std::string::npos);
    REQUIRE(text.find("Options: --time-unit=B") != std::string::npos);
    REQUIRE(text.find("Time unit: B") != std::string::npos);
    REQUIRE(text.find("Snapshots: 3") != std::string::npos);
    REQUIRE(text.find("Peak: 40960 B (snapshot 2, time 40960)") != std::string::npos);
    REQUIRE(text.find("Heap usage (mem_heap_B) over time:") != std::string::npos);
    REQUIRE(text.find("Total usage (heap + extra + stacks) over time:") != std::string::npos);
    REQUIRE(text.find("[   2] time=40960") != std::string::npos);
    REQUIRE(text.find("[peak]") != std::string::npos);
    REQUIRE(text.find("[detailed]") == std::string::npos); // peak wins over detailed on the same line

    REQUIRE(buffer.Point() == 0);
}

TEST_CASE("RebuildMassifReportBuffer refreshes the same buffer in place", "[MassifReportBuffer]") {
    BufferList bufferList;
    Buffer&    first = RebuildMassifReportBuffer(bufferList, SampleProfile(), "/tmp/one.out");

    MassifProfile second;
    second.cmd = "./other";
    Buffer& refreshed = RebuildMassifReportBuffer(bufferList, second, "/tmp/two.out");

    REQUIRE(&first == &refreshed);
    REQUIRE(refreshed.Text().find("Massif profile: /tmp/two.out") == 0);
    REQUIRE(refreshed.Text().find("Snapshots: 0") != std::string::npos);
    REQUIRE(refreshed.Text().find("(no snapshots found)") != std::string::npos);
}

TEST_CASE("RebuildMassifReportBuffer with no snapshots still produces a valid report", "[MassifReportBuffer]") {
    BufferList    bufferList;
    MassifProfile empty;
    Buffer&       buffer = RebuildMassifReportBuffer(bufferList, empty, "/tmp/empty.out");

    REQUIRE(buffer.ReadOnly());
    REQUIRE(buffer.Text().find("Peak:") == std::string::npos); // no peak snapshot -- no peak line at all
    REQUIRE(buffer.Text().find("(no snapshots found)") != std::string::npos);
}
