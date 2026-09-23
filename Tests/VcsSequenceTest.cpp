#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include <unistd.h>

#include "Editor/Vcs/Sequence.h"

using ned::editor::vcs::PlanSequenceContinue;
using ned::editor::vcs::SequenceContinueBlockedMessage;
using ned::editor::vcs::SequenceFileProbe;
using ned::editor::vcs::SequenceLabel;
using ned::editor::vcs::SequenceState;
using ned::editor::vcs::StatusEntry;

namespace {

SequenceFileProbe ProbeOf(std::set<std::filesystem::path> unsaved, std::set<std::filesystem::path> withMarkers) {
    return {
        .hasUnsavedBuffer   = [unsaved](const std::filesystem::path& path) { return unsaved.contains(path); },
        .hasConflictMarkers = [withMarkers](const std::filesystem::path& path) { return withMarkers.contains(path); },
    };
}

} // namespace

TEST_CASE("SequenceLabel names known kinds and shows progress only when the provider has it", "[VcsSequence]") {
    REQUIRE(SequenceLabel({.kind = "rebase", .step = 3, .total = 7}) == "Rebasing 3/7");
    REQUIRE(SequenceLabel({.kind = "merge"}) == "Merging");
    REQUIRE(SequenceLabel({.kind = "cherry-pick"}) == "Cherry-picking");
    REQUIRE(SequenceLabel({.kind = "revert"}) == "Reverting");
    REQUIRE(SequenceLabel({.kind = "am", .step = 1, .total = 2}) == "Applying patches 1/2");
    REQUIRE(SequenceLabel({.kind = "graft"}) == "graft");
    REQUIRE(SequenceLabel({}).empty());
}

TEST_CASE("PlanSequenceContinue sorts unmerged files by what still blocks them", "[VcsSequence]") {
    const std::filesystem::path    root = "/repo";
    const std::vector<StatusEntry> status{
        {.state = "UU", .path = "unsaved.cpp"},
        {.state = "UU", .path = "marked.cpp"},
        {.state = "AA", .path = "resolved.cpp"},
        {.state = "M ", .path = "already-staged.cpp"},
        {.state = " M", .path = "ordinary.cpp"},
    };
    // An unsaved buffer wins even when the on-disk file still has markers.
    const SequenceFileProbe probe = ProbeOf({"/repo/unsaved.cpp"}, {"/repo/unsaved.cpp", "/repo/marked.cpp"});

    const auto plan = PlanSequenceContinue(status, root, probe, /*autoStage=*/false);
    REQUIRE(plan.unsaved == std::vector<std::filesystem::path>{"/repo/unsaved.cpp"});
    REQUIRE(plan.conflicted == std::vector<std::filesystem::path>{"/repo/marked.cpp"});
    REQUIRE(plan.toStage == std::vector<std::filesystem::path>{"/repo/resolved.cpp"});
    REQUIRE_FALSE(plan.Ready());
    REQUIRE(SequenceContinueBlockedMessage(plan, root) == "Save first: unsaved.cpp");
}

TEST_CASE("PlanSequenceContinue blocks on markers before it blocks on staging", "[VcsSequence]") {
    const std::vector<StatusEntry> status{{.state = "UU", .path = "a.cpp"}, {.state = "UU", .path = "b.cpp"}};
    const auto                     plan = PlanSequenceContinue(status, "/repo", ProbeOf({}, {"/repo/a.cpp"}), /*autoStage=*/true);
    REQUIRE_FALSE(plan.Ready());
    REQUIRE(SequenceContinueBlockedMessage(plan, "/repo") == "Still conflicted: a.cpp");
}

TEST_CASE("PlanSequenceContinue stages resolved files only when auto-stage is on", "[VcsSequence]") {
    const std::vector<StatusEntry> status{{.state = "UU", .path = "src/a.cpp"}, {.state = "UD", .path = "gone.cpp"}};

    const auto manual = PlanSequenceContinue(status, "/repo", ProbeOf({}, {}), /*autoStage=*/false);
    REQUIRE_FALSE(manual.Ready());
    REQUIRE(SequenceContinueBlockedMessage(manual, "/repo") ==
            "Resolved but unstaged: src/a.cpp, gone.cpp -- stage them, or (ned/set-vcs-sequence-auto-stage true)");

    const auto automatic = PlanSequenceContinue(status, "/repo", ProbeOf({}, {}), /*autoStage=*/true);
    REQUIRE(automatic.Ready());
    REQUIRE(automatic.toStage.size() == 2);
    REQUIRE(SequenceContinueBlockedMessage(automatic, "/repo").empty());
}

TEST_CASE("PlanSequenceContinue is ready with nothing unmerged left", "[VcsSequence]") {
    const std::vector<StatusEntry> status{{.state = "M ", .path = "a.cpp"}};
    const auto                     plan = PlanSequenceContinue(status, "/repo", ProbeOf({}, {}), /*autoStage=*/false);
    REQUIRE(plan.Ready());
    REQUIRE(plan.toStage.empty());
}

TEST_CASE("FileHasConflictMarkers reads the file on disk and treats a missing file as clean", "[VcsSequence]") {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / ("ned-vcs-sequence-test-" + std::to_string(::getpid()));
    std::filesystem::create_directories(dir);
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::filesystem::remove_all(path);
        }
    } cleanup{dir};

    std::ofstream(dir / "conflicted.txt") << "<<<<<<< ours\na\n=======\nb\n>>>>>>> theirs\n";
    std::ofstream(dir / "clean.txt") << "a\n";
    REQUIRE(ned::editor::vcs::FileHasConflictMarkers(dir / "conflicted.txt"));
    REQUIRE_FALSE(ned::editor::vcs::FileHasConflictMarkers(dir / "clean.txt"));
    REQUIRE_FALSE(ned::editor::vcs::FileHasConflictMarkers(dir / "missing.txt"));
}

TEST_CASE("Sequence auto-stage defaults off and round-trips", "[VcsSequence]") {
    REQUIRE_FALSE(ned::editor::vcs::SequenceAutoStageEnabled());
    ned::editor::vcs::SetSequenceAutoStage(true);
    REQUIRE(ned::editor::vcs::SequenceAutoStageEnabled());
    ned::editor::vcs::SetSequenceAutoStage(false);
    REQUIRE_FALSE(ned::editor::vcs::SequenceAutoStageEnabled());
}
