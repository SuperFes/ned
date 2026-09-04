#include <catch2/catch_test_macros.hpp>

#include "Editor/MassifOutputParser.h"

using ned::editor::MassifProfile;
using ned::editor::MassifSnapshot;
using ned::editor::ParseMassifOutput;

TEST_CASE("Ordinary non-massif output yields no snapshots", "[MassifOutputParser]") {
    const MassifProfile profile = ParseMassifOutput("All tests passed.\n[tests: 12 passed]\n");
    CHECK(profile.snapshots.empty());
}

TEST_CASE("Empty input yields no snapshots", "[MassifOutputParser]") {
    CHECK(ParseMassifOutput("").snapshots.empty());
}

TEST_CASE("Header fields are parsed", "[MassifOutputParser]") {
    const std::string output = "desc: --detailed-freq=1\n"
                                "cmd: ./a.out --flag\n"
                                "time_unit: i\n"
                                "#-----------\n"
                                "snapshot=0\n"
                                "#-----------\n"
                                "time=0\n"
                                "mem_heap_B=0\n"
                                "mem_heap_extra_B=0\n"
                                "mem_stacks_B=0\n"
                                "heap_tree=empty\n";
    const MassifProfile profile = ParseMassifOutput(output);
    CHECK(profile.desc == "--detailed-freq=1");
    CHECK(profile.cmd == "./a.out --flag");
    CHECK(profile.timeUnit == "i");
    REQUIRE(profile.snapshots.size() == 1);
}

TEST_CASE("A run of empty snapshots plus one detailed and one peak snapshot", "[MassifOutputParser]") {
    const std::string output = "desc: --time-unit=B\n"
                                "cmd: ./prog\n"
                                "time_unit: B\n"
                                "#-----------\n"
                                "snapshot=0\n"
                                "#-----------\n"
                                "time=0\n"
                                "mem_heap_B=0\n"
                                "mem_heap_extra_B=0\n"
                                "mem_stacks_B=0\n"
                                "heap_tree=empty\n"
                                "#-----------\n"
                                "snapshot=1\n"
                                "#-----------\n"
                                "time=1024\n"
                                "mem_heap_B=1000\n"
                                "mem_heap_extra_B=24\n"
                                "mem_stacks_B=0\n"
                                "heap_tree=empty\n"
                                "#-----------\n"
                                "snapshot=2\n"
                                "#-----------\n"
                                "time=20480\n"
                                "mem_heap_B=20000\n"
                                "mem_heap_extra_B=480\n"
                                "mem_stacks_B=0\n"
                                "heap_tree=detailed\n"
                                "n1: 20480 (heap allocation functions) malloc/new/new[], --alloc-fns, etc.\n"
                                " n1: 20480 0x1091C4: main (a.c:10)\n"
                                "#-----------\n"
                                "snapshot=3\n"
                                "#-----------\n"
                                "time=40960\n"
                                "mem_heap_B=40000\n"
                                "mem_heap_extra_B=960\n"
                                "mem_stacks_B=0\n"
                                "heap_tree=peak\n"
                                "n1: 40960 (heap allocation functions) malloc/new/new[], --alloc-fns, etc.\n"
                                " n1: 40960 0x1091C4: main (a.c:12)\n";

    const MassifProfile profile = ParseMassifOutput(output);
    REQUIRE(profile.snapshots.size() == 4);

    CHECK(profile.snapshots[0].index == 0);
    CHECK(profile.snapshots[0].time == 0);
    CHECK(profile.snapshots[0].TotalBytes() == 0);
    CHECK_FALSE(profile.snapshots[0].isDetailed);
    CHECK_FALSE(profile.snapshots[0].isPeak);

    CHECK(profile.snapshots[1].index == 1);
    CHECK(profile.snapshots[1].time == 1024);
    CHECK(profile.snapshots[1].heapBytes == 1000);
    CHECK(profile.snapshots[1].heapExtraBytes == 24);
    CHECK(profile.snapshots[1].TotalBytes() == 1024);

    CHECK(profile.snapshots[2].index == 2);
    CHECK(profile.snapshots[2].isDetailed);
    CHECK_FALSE(profile.snapshots[2].isPeak);
    CHECK(profile.snapshots[2].TotalBytes() == 20480);

    CHECK(profile.snapshots[3].index == 3);
    CHECK(profile.snapshots[3].isDetailed);
    CHECK(profile.snapshots[3].isPeak);
    CHECK(profile.snapshots[3].TotalBytes() == 40960);
}

TEST_CASE("mem_stacks_B is captured when --stacks=yes was used", "[MassifOutputParser]") {
    const std::string output = "#-----------\n"
                                "snapshot=0\n"
                                "#-----------\n"
                                "time=0\n"
                                "mem_heap_B=100\n"
                                "mem_heap_extra_B=10\n"
                                "mem_stacks_B=50\n"
                                "heap_tree=empty\n";
    const MassifProfile profile = ParseMassifOutput(output);
    REQUIRE(profile.snapshots.size() == 1);
    CHECK(profile.snapshots[0].stacksBytes == 50);
    CHECK(profile.snapshots[0].TotalBytes() == 160);
}

TEST_CASE("A snapshot missing heap_tree at EOF is still committed", "[MassifOutputParser]") {
    const std::string output = "#-----------\n"
                                "snapshot=0\n"
                                "#-----------\n"
                                "time=5\n"
                                "mem_heap_B=99\n";
    const MassifProfile profile = ParseMassifOutput(output);
    REQUIRE(profile.snapshots.size() == 1);
    CHECK(profile.snapshots[0].time == 5);
    CHECK(profile.snapshots[0].heapBytes == 99);
    CHECK_FALSE(profile.snapshots[0].isDetailed);
}

TEST_CASE("A snapshot missing heap_tree before the next snapshot marker is still committed", "[MassifOutputParser]") {
    const std::string output = "#-----------\n"
                                "snapshot=0\n"
                                "#-----------\n"
                                "time=5\n"
                                "mem_heap_B=99\n"
                                "#-----------\n"
                                "snapshot=1\n"
                                "#-----------\n"
                                "time=10\n"
                                "mem_heap_B=200\n"
                                "mem_heap_extra_B=0\n"
                                "mem_stacks_B=0\n"
                                "heap_tree=empty\n";
    const MassifProfile profile = ParseMassifOutput(output);
    REQUIRE(profile.snapshots.size() == 2);
    CHECK(profile.snapshots[0].heapBytes == 99);
    CHECK(profile.snapshots[1].heapBytes == 200);
}
