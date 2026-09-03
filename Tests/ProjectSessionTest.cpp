#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "Editor/Dap/DapManager.h"
#include "Editor/ProjectSession.h"
#include "UI/EventLoop.h"

using ned::editor::ActiveProjectSessionRoot;
using ned::editor::FindProjectMarkerRoot;
using ned::editor::HasProjectMarker;
using ned::editor::LoadActiveProjectSession;
using ned::editor::LoadProjectSessionFile;
using ned::editor::ProjectSessionData;
using ned::editor::ProjectSessionFromJson;
using ned::editor::ProjectSessionPath;
using ned::editor::ProjectSessionToJson;
using ned::editor::ResetProjectSessionForTesting;
using ned::editor::SaveActiveProjectSession;
using ned::editor::SaveProjectSessionFile;
using ned::editor::SessionsDirectory;
using ned::editor::SetActiveProjectSessionRoot;
using ned::editor::SetSessionRestoreEnabled;
using ned::editor::WindowLayoutNode;

namespace {

// Mirrors InitFileTest.cpp's own EnvVarGuard exactly (see ScratchPadTest.cpp
// for the same duplication precedent).
class EnvVarGuard {
  public:
    EnvVarGuard(const char* name, const char* value) : name_(name) {
        if (const char* existing = std::getenv(name)) {
            hadPrevious_ = true;
            previous_    = existing;
        }
        if (value) {
            setenv(name, value, 1);
        }
        else {
            unsetenv(name);
        }
    }

    ~EnvVarGuard() {
        if (hadPrevious_) {
            setenv(name_.c_str(), previous_.c_str(), 1);
        }
        else {
            unsetenv(name_.c_str());
        }
    }

    EnvVarGuard(const EnvVarGuard&)            = delete;
    EnvVarGuard& operator=(const EnvVarGuard&) = delete;

  private:
    std::string name_;
    bool        hadPrevious_ = false;
    std::string previous_;
};

// Same guard shape SessionTest.cpp's SessionStateGuard established for the
// sibling process-wide store.
struct ProjectSessionGuard {
    ProjectSessionGuard() {
        ResetProjectSessionForTesting();
    }
    ~ProjectSessionGuard() {
        ResetProjectSessionForTesting();
    }
};

std::filesystem::path FreshTestDir(const std::string& name) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

ProjectSessionData SampleData() {
    ProjectSessionData data;
    data.openFiles      = {"/project/a.cpp", "/project/b.h"};
    data.activeFile     = "/project/a.cpp";
    data.sidebarVisible = false;
    data.sidebarWidth   = 42;
    data.breakpoints    = {{"/project/a.cpp", {{.line = 3, .condition = "x > 1"}, {.line = 17, .logMessage = "hit"}}}};
    data.watches        = {"x + y", "count"};

    // A two-leaf SplitRight, post-order (children before parent -- see
    // WindowLayoutNode's own doc comment): index 0 = a.cpp, index 1 = b.h,
    // index 2 = the split, which is therefore the root (the last element).
    WindowLayoutNode leafA;
    leafA.kind = WindowLayoutNode::Kind::Leaf;
    leafA.file = "/project/a.cpp";
    WindowLayoutNode leafB;
    leafB.kind = WindowLayoutNode::Kind::Leaf;
    leafB.file = "/project/b.h";
    WindowLayoutNode split;
    split.kind   = WindowLayoutNode::Kind::SplitRight;
    split.first  = 0;
    split.second = 1;
    data.windowLayout    = {leafA, leafB, split};
    data.focusedPanePath = {1}; // b.h (the split's "second") had focus

    return data;
}

} // namespace

TEST_CASE("ProjectSessionData JSON round-trips", "[ProjectSession]") {
    const ProjectSessionData data = SampleData();

    const auto loaded = ProjectSessionFromJson(ProjectSessionToJson(data, "/project"));
    REQUIRE(loaded.has_value());
    REQUIRE(*loaded == data);
}

TEST_CASE("ProjectSessionData JSON round-trips absent optional fields", "[ProjectSession]") {
    const ProjectSessionData data; // everything empty/absent

    const auto loaded = ProjectSessionFromJson(ProjectSessionToJson(data, "/project"));
    REQUIRE(loaded.has_value());
    REQUIRE(*loaded == data);
    REQUIRE_FALSE(loaded->activeFile.has_value());
    REQUIRE_FALSE(loaded->sidebarVisible.has_value());
    REQUIRE_FALSE(loaded->sidebarWidth.has_value());
}

TEST_CASE("ProjectSessionFromJson tolerates malformed input", "[ProjectSession]") {
    REQUIRE_FALSE(ProjectSessionFromJson("not json").has_value());
    // Parseable but odd shapes degrade per-field, not wholesale.
    const auto partial = ProjectSessionFromJson(
        R"({"version":1,"openFiles":["/a.cpp",7],)"
        R"("breakpoints":{"/a.cpp":[1,"x",{"line":5,"condition":"n > 1"},{"noLine":9}],"/bad":"nope"},)"
        R"("watches":["ok",3,"also-ok"]})");
    REQUIRE(partial.has_value());
    REQUIRE(partial->openFiles == std::vector<std::filesystem::path>{"/a.cpp"});
    REQUIRE(partial->breakpoints ==
            std::map<std::string, std::vector<ned::editor::BreakpointState>>{
                {"/a.cpp", {{.line = 1}, {.line = 5, .condition = "n > 1"}}}});
    REQUIRE(partial->watches == std::vector<std::string>{"ok", "also-ok"});
}

TEST_CASE("ProjectSessionFromJson discards a whole malformed windowLayout rather than a partial tree",
          "[ProjectSession]") {
    // Unlike breakpoints (one bad entry skipped, the rest kept), a
    // windowLayout's indices are only meaningful relative to a fully-intact
    // vector -- session-persistence-window-layout follow-up's own doc
    // comment on WindowLayoutNode explains why. Here index 1's "second"
    // points forward to index 2, which never precedes it -- session-restore
    // must fall back to no persisted layout instead of building a corrupt
    // tree.
    const auto loaded = ProjectSessionFromJson(
        R"({"version":1,"openFiles":[],"breakpoints":{},)"
        R"("windowLayout":[{"kind":"leaf","file":"/a.cpp"},)"
        R"({"kind":"right","first":0,"second":2},)"
        R"({"kind":"leaf","file":"/b.h"}]})");
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->windowLayout.empty());
}

TEST_CASE("HasProjectMarker and FindProjectMarkerRoot recognize VCS and .ned markers", "[ProjectSession]") {
    const std::filesystem::path root = FreshTestDir("ned_project_session_markers");
    std::filesystem::create_directories(root / "src" / "deep");

    REQUIRE_FALSE(HasProjectMarker(root));
    REQUIRE_FALSE(FindProjectMarkerRoot(root / "src" / "deep").has_value());

    std::filesystem::create_directories(root / ".git");
    REQUIRE(HasProjectMarker(root));
    REQUIRE(FindProjectMarkerRoot(root / "src" / "deep") == std::filesystem::absolute(root));
    REQUIRE(FindProjectMarkerRoot(root) == std::filesystem::absolute(root));

    // A .ned directory is a marker in its own right, VCS or not.
    const std::filesystem::path nedOnly = FreshTestDir("ned_project_session_nedonly");
    std::filesystem::create_directories(nedOnly / ".ned");
    REQUIRE(HasProjectMarker(nedOnly));
}

TEST_CASE("ProjectSessionPath prefers an existing .ned directory, else hashed XDG state", "[ProjectSession]") {
    EnvVarGuard xdg("XDG_STATE_HOME", "/tmp/ned-xdg-test-state");

    const std::filesystem::path root    = FreshTestDir("ned_project_session_path");
    const std::filesystem::path xdgPath = ProjectSessionPath(root);
    REQUIRE(xdgPath.parent_path() == std::filesystem::path("/tmp/ned-xdg-test-state/ned/sessions"));
    REQUIRE(xdgPath.extension() == ".json");
    // Stable: the same root always hashes to the same file.
    REQUIRE(ProjectSessionPath(root) == xdgPath);

    std::filesystem::create_directories(root / ".ned");
    REQUIRE(ProjectSessionPath(root) == root / ".ned" / "session.json");
}

TEST_CASE("Project session file save/load round-trips, including via a .ned directory", "[ProjectSession]") {
    const std::filesystem::path root = FreshTestDir("ned_project_session_file");
    std::filesystem::create_directories(root / ".ned");

    const ProjectSessionData data = SampleData();
    SaveProjectSessionFile(data, root, ProjectSessionPath(root));
    REQUIRE(std::filesystem::exists(root / ".ned" / "session.json"));

    const auto loaded = LoadProjectSessionFile(ProjectSessionPath(root));
    REQUIRE(loaded.has_value());
    REQUIRE(*loaded == data);

    REQUIRE_FALSE(LoadProjectSessionFile(root / "missing.json").has_value());
}

TEST_CASE("Active project session load/save no-op without a root or when disabled", "[ProjectSession]") {
    ProjectSessionGuard         guard;
    const std::filesystem::path root = FreshTestDir("ned_project_session_active");
    std::filesystem::create_directories(root / ".ned");

    // No root set: both directions are no-ops.
    REQUIRE_FALSE(LoadActiveProjectSession().has_value());
    SaveActiveProjectSession(SampleData());
    REQUIRE_FALSE(std::filesystem::exists(root / ".ned" / "session.json"));

    SetActiveProjectSessionRoot(root);
    SaveActiveProjectSession(SampleData());
    REQUIRE(std::filesystem::exists(root / ".ned" / "session.json"));
    const auto loaded = LoadActiveProjectSession();
    REQUIRE(loaded.has_value());
    REQUIRE(*loaded == SampleData());

    // Disabled: both directions off again, even with a root set.
    SetSessionRestoreEnabled(false);
    REQUIRE_FALSE(LoadActiveProjectSession().has_value());
    std::filesystem::remove(root / ".ned" / "session.json");
    SaveActiveProjectSession(SampleData());
    REQUIRE_FALSE(std::filesystem::exists(root / ".ned" / "session.json"));
}

TEST_CASE("SaveActiveProjectSession skips rewriting unchanged data", "[ProjectSession]") {
    ProjectSessionGuard         guard;
    const std::filesystem::path root = FreshTestDir("ned_project_session_dirty");
    std::filesystem::create_directories(root / ".ned");
    SetActiveProjectSessionRoot(root);

    SaveActiveProjectSession(SampleData());
    const std::filesystem::path file = root / ".ned" / "session.json";
    REQUIRE(std::filesystem::exists(file));

    // Delete the file behind the store's back: an identical save must be
    // skipped (proving the memo works), a changed one must write again.
    std::filesystem::remove(file);
    SaveActiveProjectSession(SampleData());
    REQUIRE_FALSE(std::filesystem::exists(file));

    ProjectSessionData changed = SampleData();
    changed.sidebarWidth       = 99;
    SaveActiveProjectSession(changed);
    REQUIRE(std::filesystem::exists(file));
}

TEST_CASE("DapManager exports and restores its breakpoint store", "[ProjectSession][Dap]") {
    ned::ui::EventLoop           eventLoop;
    ned::editor::dap::DapManager manager(eventLoop);

    const std::filesystem::path fileA = std::filesystem::current_path() / "session-bp-a.c";
    manager.ToggleBreakpoint(fileA, 12);
    manager.ToggleBreakpoint(fileA, 3);
    manager.SetBreakpointCondition(fileA, 3, "n > 5");
    manager.SetBreakpointLogMessage(fileA, 12, "hit 12");
    manager.SetBreakpointHitCondition(fileA, 12, "> 2");

    const auto exported = manager.AllBreakpoints();
    REQUIRE(exported.size() == 1);

    ned::editor::dap::DapManager restored(eventLoop);
    restored.RestoreBreakpoints(exported);
    REQUIRE(restored.BreakpointsForFile(fileA) == std::vector<std::size_t>{3, 12});
    // condition/logMessage/hitCondition round-trip too (session-persistence
    // round 2) -- verified/actualLine deliberately don't, but there's
    // nothing to observe there since a restored breakpoint never pushed to
    // a live adapter.
    const auto restoredDetails = restored.BreakpointsForKey(ned::editor::dap::DapManager::NormalizePathKey(fileA));
    REQUIRE(restoredDetails.size() == 2);
    REQUIRE(restoredDetails[0].line == 3);
    REQUIRE(restoredDetails[0].condition == "n > 5");
    REQUIRE(restoredDetails[1].line == 12);
    REQUIRE(restoredDetails[1].logMessage == "hit 12");
    REQUIRE(restoredDetails[1].hitCondition == "> 2");
    // Restored state composes with normal toggling.
    REQUIRE_FALSE(restored.ToggleBreakpoint(fileA, 12));
    REQUIRE(restored.BreakpointsForFile(fileA) == std::vector<std::size_t>{3});

    // Unsorted/duplicated/empty session-file data is normalized on the way in.
    ned::editor::dap::DapManager scrubbed(eventLoop);
    scrubbed.RestoreBreakpoints({{"/x.c", {{.line = 9}, {.line = 2}, {.line = 9}}}, {"/empty.c", {}}});
    REQUIRE(scrubbed.BreakpointsForFile("/x.c") == std::vector<std::size_t>{2, 9});
    REQUIRE(scrubbed.AllBreakpoints().size() == 1);
}

TEST_CASE("DapManager exports and restores its watch list", "[ProjectSession][Dap]") {
    ned::ui::EventLoop           eventLoop;
    ned::editor::dap::DapManager manager(eventLoop);
    manager.AddWatch("x");
    manager.AddWatch("y + 1");

    ned::editor::dap::DapManager restored(eventLoop);
    restored.RestoreWatches(manager.Watches());
    REQUIRE(restored.Watches() == std::vector<std::string>{"x", "y + 1"});
}
