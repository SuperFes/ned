#include <catch2/catch_test_macros.hpp>

#include "Editor/Vcs/VcsRowStatus.h"

using ned::editor::vcs::ClassifyPorcelainStatus;
using ned::editor::vcs::PartitionVcsStatus;
using ned::editor::vcs::VcsRowStatus;
using ned::editor::vcs::VcsStatusEntry;

TEST_CASE("ClassifyPorcelainStatus buckets git's own two-letter status codes", "[VcsRowStatus]") {
    REQUIRE(ClassifyPorcelainStatus("??") == VcsRowStatus::Untracked);
    REQUIRE(ClassifyPorcelainStatus(" M") == VcsRowStatus::Modified);
    REQUIRE(ClassifyPorcelainStatus("M ") == VcsRowStatus::Modified);
    REQUIRE(ClassifyPorcelainStatus("A ") == VcsRowStatus::Added);
    REQUIRE(ClassifyPorcelainStatus(" D") == VcsRowStatus::Deleted);
    REQUIRE(ClassifyPorcelainStatus("D ") == VcsRowStatus::Deleted);
    // D beats M beats A when a letter with no dedicated bucket -- or both
    // columns set -- forces a priority pick.
    REQUIRE(ClassifyPorcelainStatus("AM") == VcsRowStatus::Modified);
    REQUIRE(ClassifyPorcelainStatus("MD") == VcsRowStatus::Deleted);
    // No dedicated bucket (rename) falls back to Modified.
    REQUIRE(ClassifyPorcelainStatus("R ") == VcsRowStatus::Modified);
}

TEST_CASE("PartitionVcsStatus splits staged/unstaged/untracked by porcelain column", "[VcsRowStatus]") {
    const auto sections = PartitionVcsStatus({
        {"M ", "staged_only.txt"    },
        {" M", "unstaged_only.txt"  },
        {"AM", "staged_and_edited.txt"},
        {"??", "untracked.txt"      },
    });

    REQUIRE(sections.staged.size() == 2);
    REQUIRE(sections.staged[0].path == "staged_only.txt");
    REQUIRE(sections.staged[1].path == "staged_and_edited.txt");

    REQUIRE(sections.unstaged.size() == 2);
    REQUIRE(sections.unstaged[0].path == "unstaged_only.txt");
    REQUIRE(sections.unstaged[1].path == "staged_and_edited.txt");

    REQUIRE(sections.untracked.size() == 1);
    REQUIRE(sections.untracked[0].path == "untracked.txt");
}

TEST_CASE("PartitionVcsStatus on an empty status list produces three empty sections", "[VcsRowStatus]") {
    const auto sections = PartitionVcsStatus({});
    REQUIRE(sections.staged.empty());
    REQUIRE(sections.unstaged.empty());
    REQUIRE(sections.untracked.empty());
}
