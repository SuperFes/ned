// Text/SavePlan.h's executor, exercised without a Buffer -- which is the
// point of the extraction: the write path's own decisions (atomic rename vs.
// in-place truncate, attribute carry-across, failure reporting) are now
// reachable from a plain plan value, so they can be tested directly instead
// of only through Buffer::SaveToFile.
//
// The snapshot-independence case below is the load-bearing one for what the
// plan exists to enable: the executor writes plan.snapshot and nothing else,
// so the storage it was cloned from may move on freely.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

#include "Text/RopeStorage.h"
#include "Text/SavePlan.h"

using ned::text::CaptureFileAttributes;
using ned::text::LineEnding;
using ned::text::Rope;
using ned::text::RopeStorage;
using ned::text::SavePlan;

namespace {

// Suffixed by pid so a parallel ctest run never collides on a shared /tmp.
std::filesystem::path TempPath(const std::string& name) {
    return std::filesystem::temp_directory_path() / ("ned_save_plan_" + name + "_" + std::to_string(::getpid()));
}

void WriteFile(const std::filesystem::path& path, std::string_view content) {
    std::ofstream file(path, std::ios::binary);
    file << content;
}

std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

SavePlan PlanFor(const std::filesystem::path& target, std::string_view content) {
    SavePlan plan;
    plan.target                 = target;
    plan.snapshot               = std::make_unique<RopeStorage>(Rope(content));
    plan.attributes             = CaptureFileAttributes(target);
    plan.lineEnding             = LineEnding::LF;
    plan.trimTrailingWhitespace = false;
    plan.ensureFinalNewline     = false;
    return plan;
}

} // namespace

TEST_CASE("ExecuteSavePlan writes a new file", "[SavePlan]") {
    const std::filesystem::path target = TempPath("new.txt");
    std::filesystem::remove(target);

    ExecuteSavePlan(PlanFor(target, "hello\nworld\n"));

    REQUIRE(ReadFile(target) == "hello\nworld\n");
    std::filesystem::remove(target);
}

TEST_CASE("ExecuteSavePlan writes the snapshot, not later storage state", "[SavePlan]") {
    const std::filesystem::path target = TempPath("snapshot.txt");
    std::filesystem::remove(target);

    RopeStorage live{Rope("original")};
    SavePlan    plan = PlanFor(target, "");
    plan.snapshot    = live.Clone();

    // Stands in for an edit landing while the write is in flight: the plan
    // holds an independent clone, so the file must still get "original".
    live = RopeStorage{Rope("edited after the snapshot was taken")};

    ExecuteSavePlan(plan);

    REQUIRE(ReadFile(target) == "original");
    std::filesystem::remove(target);
}

TEST_CASE("ExecuteSavePlan applies the save-time content transforms", "[SavePlan]") {
    const std::filesystem::path target = TempPath("transforms.txt");
    std::filesystem::remove(target);

    SavePlan plan               = PlanFor(target, "a line with trailing spaces   \nlast");
    plan.trimTrailingWhitespace = true;
    plan.ensureFinalNewline     = true;
    plan.lineEnding             = LineEnding::CRLF;

    ExecuteSavePlan(plan);

    REQUIRE(ReadFile(target) == "a line with trailing spaces\r\nlast\r\n");
    std::filesystem::remove(target);
}

TEST_CASE("ExecuteSavePlan preserves mode bits across the rename", "[SavePlan]") {
    const std::filesystem::path target = TempPath("mode.sh");
    WriteFile(target, "#!/bin/sh\n");
    std::filesystem::permissions(target, std::filesystem::perms::owner_all | std::filesystem::perms::group_read |
                                             std::filesystem::perms::group_exec);
    const std::filesystem::perms before = std::filesystem::status(target).permissions();

    ExecuteSavePlan(PlanFor(target, "#!/bin/sh\necho hi\n"));

    REQUIRE(std::filesystem::status(target).permissions() == before);
    REQUIRE(ReadFile(target) == "#!/bin/sh\necho hi\n");
    std::filesystem::remove(target);
}

TEST_CASE("ExecuteSavePlan writes a hard-linked file in place", "[SavePlan]") {
    const std::filesystem::path target = TempPath("linked.txt");
    const std::filesystem::path other  = TempPath("linked_other.txt");
    std::filesystem::remove(target);
    std::filesystem::remove(other);
    WriteFile(target, "before\n");
    std::filesystem::create_hard_link(target, other);

    ExecuteSavePlan(PlanFor(target, "after\n"));

    // The whole point of the in-place mode: a rename would have given the
    // second link the stale content and left it pointing at a dead inode.
    REQUIRE(ReadFile(target) == "after\n");
    REQUIRE(ReadFile(other) == "after\n");
    REQUIRE(std::filesystem::hard_link_count(target) == 2);

    std::filesystem::remove(target);
    std::filesystem::remove(other);
}

TEST_CASE("ExecuteSavePlan throws when the target directory does not exist", "[SavePlan]") {
    const std::filesystem::path target = TempPath("missing_dir") / "nested" / "file.txt";
    REQUIRE_THROWS_AS(ExecuteSavePlan(PlanFor(target, "content")), std::runtime_error);
}
