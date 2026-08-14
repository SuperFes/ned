#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include "Editor/ScratchPad.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"

using ned::editor::AutoSaveScratchBuffers;
using ned::editor::CompleteScratchNames;
using ned::editor::IsValidScratchName;
using ned::editor::ListScratchNames;
using ned::editor::ScratchAutoSaveEnabled;
using ned::editor::ScratchDirectory;
using ned::editor::ScratchPathForName;
using ned::editor::SetScratchAutoSaveEnabled;

namespace {

// Mirrors InitFileTest.cpp's own EnvVarGuard exactly -- saves/restores an
// environment variable's previous state (including "was unset") around a
// test, so these tests don't leak XDG_DATA_HOME/HOME overrides into anything
// else running in this process.
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

// ScratchAutoSaveEnabled() is process-wide state (see ScratchPad.h's own doc
// comment); every test that changes it must restore the default for the next
// test, the same TabWidthGuard-style precedent BufferViewTest.cpp already
// established for TabWidth's own process-wide setting.
struct AutoSaveGuard {
    ~AutoSaveGuard() {
        SetScratchAutoSaveEnabled(true);
    }
};

} // namespace

TEST_CASE("ScratchDirectory prefers XDG_DATA_HOME when set", "[ScratchPad]") {
    EnvVarGuard xdg("XDG_DATA_HOME", "/tmp/ned-xdg-test-data");
    EnvVarGuard home("HOME", "/tmp/ned-xdg-test-home");

    REQUIRE(ScratchDirectory() == std::filesystem::path("/tmp/ned-xdg-test-data/ned/scratches"));
}

TEST_CASE("ScratchDirectory falls back to HOME/.local/share when XDG_DATA_HOME is unset", "[ScratchPad]") {
    EnvVarGuard xdg("XDG_DATA_HOME", nullptr);
    EnvVarGuard home("HOME", "/tmp/ned-xdg-test-home");

    REQUIRE(ScratchDirectory() == std::filesystem::path("/tmp/ned-xdg-test-home/.local/share/ned/scratches"));
}

TEST_CASE("ScratchDirectory throws when neither XDG_DATA_HOME nor HOME is set", "[ScratchPad]") {
    EnvVarGuard xdg("XDG_DATA_HOME", nullptr);
    EnvVarGuard home("HOME", nullptr);

    REQUIRE_THROWS_AS(ScratchDirectory(), std::runtime_error);
}

TEST_CASE("IsValidScratchName accepts an ordinary name", "[ScratchPad]") {
    REQUIRE(IsValidScratchName("todo"));
    REQUIRE(IsValidScratchName("todo-2026"));
}

TEST_CASE("IsValidScratchName rejects an empty name", "[ScratchPad]") {
    REQUIRE_FALSE(IsValidScratchName(""));
}

TEST_CASE("IsValidScratchName rejects a name containing a path separator", "[ScratchPad]") {
    REQUIRE_FALSE(IsValidScratchName("../escape"));
    REQUIRE_FALSE(IsValidScratchName("sub/dir"));
}

TEST_CASE("ScratchPathForName builds a .txt path under ScratchDirectory", "[ScratchPad]") {
    EnvVarGuard xdg("XDG_DATA_HOME", "/tmp/ned-xdg-test-data");
    EnvVarGuard home("HOME", nullptr);

    REQUIRE(ScratchPathForName("todo") == std::filesystem::path("/tmp/ned-xdg-test-data/ned/scratches/todo.txt"));
}

TEST_CASE("ScratchPathForName throws for an invalid name", "[ScratchPad]") {
    REQUIRE_THROWS_AS(ScratchPathForName(""), std::invalid_argument);
    REQUIRE_THROWS_AS(ScratchPathForName("a/b"), std::invalid_argument);
}

TEST_CASE("ListScratchNames returns an empty list when the directory doesn't exist yet", "[ScratchPad]") {
    const std::filesystem::path dataDir = std::filesystem::temp_directory_path() / "ned_scratchpad_test_missing_dir";
    std::filesystem::remove_all(dataDir);

    EnvVarGuard xdg("XDG_DATA_HOME", dataDir.c_str());
    EnvVarGuard home("HOME", nullptr);

    REQUIRE(ListScratchNames().empty());
}

TEST_CASE("ListScratchNames lists .txt file stems, sorted, ignoring other files", "[ScratchPad]") {
    const std::filesystem::path dataDir = std::filesystem::temp_directory_path() / "ned_scratchpad_test_list";
    std::filesystem::remove_all(dataDir);
    const std::filesystem::path scratchDir = dataDir / "ned" / "scratches";
    std::filesystem::create_directories(scratchDir);
    {
        std::ofstream(scratchDir / "zebra.txt") << "z";
    }
    {
        std::ofstream(scratchDir / "apple.txt") << "a";
    }
    {
        std::ofstream(scratchDir / "ignored.md") << "not a scratch";
    }
    std::filesystem::create_directory(scratchDir / "ignored-subdir");

    EnvVarGuard xdg("XDG_DATA_HOME", dataDir.c_str());
    EnvVarGuard home("HOME", nullptr);

    REQUIRE(ListScratchNames() == std::vector<std::string>{"apple", "zebra"});

    std::filesystem::remove_all(dataDir);
}

TEST_CASE("CompleteScratchNames prefix-filters existing scratches", "[ScratchPad]") {
    const std::filesystem::path dataDir = std::filesystem::temp_directory_path() / "ned_scratchpad_test_complete";
    std::filesystem::remove_all(dataDir);
    const std::filesystem::path scratchDir = dataDir / "ned" / "scratches";
    std::filesystem::create_directories(scratchDir);
    {
        std::ofstream(scratchDir / "todo-work.txt") << "x";
    }
    {
        std::ofstream(scratchDir / "todo-home.txt") << "x";
    }
    {
        std::ofstream(scratchDir / "recipe.txt") << "x";
    }

    EnvVarGuard xdg("XDG_DATA_HOME", dataDir.c_str());
    EnvVarGuard home("HOME", nullptr);

    REQUIRE(CompleteScratchNames("todo") == std::vector<std::string>{"todo-home", "todo-work"});
    REQUIRE(CompleteScratchNames("rec") == std::vector<std::string>{"recipe"});
    REQUIRE(CompleteScratchNames("nope").empty());

    std::filesystem::remove_all(dataDir);
}

TEST_CASE("ScratchAutoSaveEnabled defaults to true and round-trips through SetScratchAutoSaveEnabled",
          "[ScratchPad]") {
    const AutoSaveGuard guard;

    REQUIRE(ScratchAutoSaveEnabled());

    SetScratchAutoSaveEnabled(false);
    REQUIRE_FALSE(ScratchAutoSaveEnabled());

    SetScratchAutoSaveEnabled(true);
    REQUIRE(ScratchAutoSaveEnabled());
}

TEST_CASE("AutoSaveScratchBuffers saves a modified buffer whose path is directly in the scratch directory",
          "[ScratchPad]") {
    const AutoSaveGuard         guard;
    const std::filesystem::path dataDir = std::filesystem::temp_directory_path() / "ned_scratchpad_test_autosave";
    std::filesystem::remove_all(dataDir);

    EnvVarGuard xdg("XDG_DATA_HOME", dataDir.c_str());
    EnvVarGuard home("HOME", nullptr);

    ned::text::BufferList bufferList;
    ned::text::Buffer&    scratch = bufferList.OpenOrCreateFile(ScratchPathForName("todo"));
    scratch.InsertAtPoint("buy milk");
    REQUIRE(scratch.Modified());

    AutoSaveScratchBuffers(bufferList);

    REQUIRE_FALSE(scratch.Modified());
    REQUIRE(std::filesystem::exists(ScratchPathForName("todo")));
    {
        std::ifstream in(ScratchPathForName("todo"));
        std::string   content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        REQUIRE(content == "buy milk");
    }

    std::filesystem::remove_all(dataDir);
}

TEST_CASE("AutoSaveScratchBuffers leaves an unmodified scratch buffer alone", "[ScratchPad]") {
    const AutoSaveGuard         guard;
    const std::filesystem::path dataDir = std::filesystem::temp_directory_path() / "ned_scratchpad_test_unmodified";
    std::filesystem::remove_all(dataDir);
    const std::filesystem::path scratchDir = dataDir / "ned" / "scratches";
    std::filesystem::create_directories(scratchDir);
    {
        std::ofstream(scratchDir / "todo.txt") << "original";
    }

    EnvVarGuard xdg("XDG_DATA_HOME", dataDir.c_str());
    EnvVarGuard home("HOME", nullptr);

    ned::text::BufferList bufferList;
    bufferList.OpenOrCreateFile(scratchDir / "todo.txt"); // opened, never edited -- not Modified()

    AutoSaveScratchBuffers(bufferList); // must not throw or touch the file

    std::ifstream in(scratchDir / "todo.txt");
    std::string   content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    REQUIRE(content == "original");

    std::filesystem::remove_all(dataDir);
}

TEST_CASE("AutoSaveScratchBuffers ignores a modified buffer outside the scratch directory", "[ScratchPad]") {
    const AutoSaveGuard         guard;
    const std::filesystem::path dataDir = std::filesystem::temp_directory_path() / "ned_scratchpad_test_outside";
    std::filesystem::remove_all(dataDir);
    const std::filesystem::path projectDir = dataDir / "project";
    std::filesystem::create_directories(projectDir);

    EnvVarGuard xdg("XDG_DATA_HOME", dataDir.c_str());
    EnvVarGuard home("HOME", nullptr);

    ned::text::BufferList bufferList;
    ned::text::Buffer&    projectFile = bufferList.OpenOrCreateFile(projectDir / "notes.txt");
    projectFile.InsertAtPoint("not a scratch");
    REQUIRE(projectFile.Modified());

    AutoSaveScratchBuffers(bufferList);

    REQUIRE(projectFile.Modified()); // untouched -- not under ScratchDirectory()
    REQUIRE_FALSE(std::filesystem::exists(projectDir / "notes.txt"));

    std::filesystem::remove_all(dataDir);
}

TEST_CASE("AutoSaveScratchBuffers is a no-op when disabled", "[ScratchPad]") {
    const AutoSaveGuard         guard;
    const std::filesystem::path dataDir = std::filesystem::temp_directory_path() / "ned_scratchpad_test_disabled";
    std::filesystem::remove_all(dataDir);

    EnvVarGuard xdg("XDG_DATA_HOME", dataDir.c_str());
    EnvVarGuard home("HOME", nullptr);

    SetScratchAutoSaveEnabled(false);

    ned::text::BufferList bufferList;
    ned::text::Buffer&    scratch = bufferList.OpenOrCreateFile(ScratchPathForName("todo"));
    scratch.InsertAtPoint("buy milk");

    AutoSaveScratchBuffers(bufferList);

    REQUIRE(scratch.Modified()); // untouched -- auto-save is disabled
    REQUIRE_FALSE(std::filesystem::exists(ScratchPathForName("todo")));

    std::filesystem::remove_all(dataDir);
}

TEST_CASE("AutoSaveScratchBuffers creates the scratch directory on disk if it doesn't exist yet", "[ScratchPad]") {
    const AutoSaveGuard         guard;
    const std::filesystem::path dataDir = std::filesystem::temp_directory_path() / "ned_scratchpad_test_mkdir";
    std::filesystem::remove_all(dataDir);

    EnvVarGuard xdg("XDG_DATA_HOME", dataDir.c_str());
    EnvVarGuard home("HOME", nullptr);

    ned::text::BufferList bufferList;

    REQUIRE_FALSE(std::filesystem::exists(ScratchDirectory()));
    AutoSaveScratchBuffers(bufferList); // no buffers to save, but should still create the directory
    REQUIRE(std::filesystem::is_directory(ScratchDirectory()));

    std::filesystem::remove_all(dataDir);
}

TEST_CASE("AutoSaveScratchBuffers swallows a per-buffer save failure rather than throwing", "[ScratchPad]") {
    const AutoSaveGuard         guard;
    const std::filesystem::path dataDir = std::filesystem::temp_directory_path() / "ned_scratchpad_test_savefail";
    std::filesystem::remove_all(dataDir);
    const std::filesystem::path scratchDir = dataDir / "ned" / "scratches";
    std::filesystem::create_directories(scratchDir);

    EnvVarGuard xdg("XDG_DATA_HOME", dataDir.c_str());
    EnvVarGuard home("HOME", nullptr);

    // OpenOrCreateFile first, while nothing exists at this path yet, so it
    // takes the Buffer::NewFile branch (no disk I/O at all -- see NewFile's
    // own doc comment). Only *after* the buffer exists does a directory get
    // put where the scratch file needs to go, so Buffer::SaveToFile's rename
    // of its temp file onto the final path fails with EISDIR -- exactly the
    // per-buffer failure AutoSaveScratchBuffers is documented to swallow.
    ned::text::BufferList bufferList;
    ned::text::Buffer&    opened = bufferList.OpenOrCreateFile(scratchDir / "todo.txt");
    opened.InsertAtPoint("buy milk");
    std::filesystem::create_directory(scratchDir / "todo.txt");

    AutoSaveScratchBuffers(bufferList); // must not throw

    REQUIRE(opened.Modified());                                      // save failed, so still modified
    REQUIRE(std::filesystem::is_directory(scratchDir / "todo.txt")); // untouched

    std::filesystem::remove_all(dataDir);
}
