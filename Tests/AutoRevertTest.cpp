#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>

#include "Editor/AutoRevert.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"

using namespace ned::editor;

namespace {

// AutoRevertEnabled is process-wide state (see AutoRevert.h); every test
// that flips it must leave it default-on for the next test, guaranteed via
// RAII -- the FormatCommandGuard pattern.
struct AutoRevertGuard {
    ~AutoRevertGuard() {
        SetAutoRevertEnabled(true);
    }
};

std::filesystem::path WriteTempFile(const char* name, const char* content) {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / name;
    std::ofstream(path) << content;
    return path;
}

// Rewrites the file and bumps its timestamp explicitly, so the test never
// depends on filesystem mtime granularity being finer than the test's own
// runtime.
void ExternalWrite(const std::filesystem::path& path, const char* content) {
    std::ofstream(path, std::ios::trunc) << content;
    std::filesystem::last_write_time(path, std::filesystem::last_write_time(path) + std::chrono::seconds(2));
}

} // namespace

TEST_CASE("ExternallyModified tracks the on-disk timestamp across load and save", "[AutoRevert]") {
    const std::filesystem::path path = WriteTempFile("ned_autorevert_test_stamp.txt", "original\n");

    ned::text::Buffer buffer = ned::text::Buffer::FromFile(path);
    REQUIRE_FALSE(buffer.ExternallyModified());

    ExternalWrite(path, "changed outside\n");
    REQUIRE(buffer.ExternallyModified());

    // Saving brings buffer and disk back into agreement.
    buffer.Save();
    REQUIRE_FALSE(buffer.ExternallyModified());

    std::filesystem::remove(path);
}

TEST_CASE("ExternallyModified is false for a pathless buffer and a still-missing NewFile", "[AutoRevert]") {
    ned::text::Buffer scratch{"scratch"};
    REQUIRE_FALSE(scratch.ExternallyModified());

    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_autorevert_test_newfile.txt";
    std::filesystem::remove(path);
    ned::text::Buffer newFile = ned::text::Buffer::NewFile(path);
    REQUIRE_FALSE(newFile.ExternallyModified());

    // A file appearing underneath a NewFile buffer counts as external.
    std::ofstream(path) << "someone else made this\n";
    REQUIRE(newFile.ExternallyModified());

    std::filesystem::remove(path);
}

TEST_CASE("Revert reloads disk content, clamps point, clears Modified, and is undoable", "[AutoRevert]") {
    const std::filesystem::path path = WriteTempFile("ned_autorevert_test_revert.txt", "one two three\n");

    ned::text::Buffer buffer = ned::text::Buffer::FromFile(path);
    buffer.SetPoint(buffer.Size());
    buffer.InsertAtPoint("local edit");
    REQUIRE(buffer.Modified());

    ExternalWrite(path, "new\n");
    buffer.Revert();

    REQUIRE(buffer.Text() == "new\n");
    REQUIRE_FALSE(buffer.Modified());
    REQUIRE_FALSE(buffer.ExternallyModified());
    REQUIRE(buffer.Point() <= buffer.Size()); // clamped into the new content

    buffer.Undo();
    REQUIRE(buffer.Text() == "one two three\nlocal edit");

    std::filesystem::remove(path);
}

TEST_CASE("AutoRevertBuffers reverts unmodified buffers only, and honors the toggle", "[AutoRevert]") {
    const AutoRevertGuard       guard;
    const std::filesystem::path cleanPath  = WriteTempFile("ned_autorevert_test_clean.txt", "clean\n");
    const std::filesystem::path editedPath = WriteTempFile("ned_autorevert_test_edited.txt", "edited\n");

    ned::text::BufferList bufferList;
    ned::text::Buffer&    clean  = bufferList.OpenOrCreateFile(cleanPath);
    ned::text::Buffer&    edited = bufferList.OpenOrCreateFile(editedPath);
    edited.SetPoint(0);
    edited.InsertAtPoint("local ");

    ExternalWrite(cleanPath, "clean v2\n");
    ExternalWrite(editedPath, "edited v2\n");

    SetAutoRevertEnabled(false);
    REQUIRE(AutoRevertBuffers(bufferList).empty());
    REQUIRE(clean.Text() == "clean\n");

    SetAutoRevertEnabled(true);
    const std::vector<std::string> reverted = AutoRevertBuffers(bufferList);
    REQUIRE(reverted == std::vector<std::string>{clean.Name()});
    REQUIRE(clean.Text() == "clean v2\n");
    // The locally-edited buffer is never touched -- the save-time
    // confirmation owns that conflict.
    REQUIRE(edited.Text() == "local edited\n");
    REQUIRE(edited.Modified());

    std::filesystem::remove(cleanPath);
    std::filesystem::remove(editedPath);
}
