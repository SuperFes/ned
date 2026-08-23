#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>

#include "Editor/AutoMerge.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"

using namespace ned::editor;

namespace {

// AutoMergeEnabled is process-wide state (see AutoMerge.h); every test that
// flips it must leave it default-on for the next test, guaranteed via RAII
// -- mirrors AutoRevertTest.cpp's own AutoRevertGuard exactly.
struct AutoMergeGuard {
    ~AutoMergeGuard() {
        SetAutoMergeEnabled(true);
    }
};

std::filesystem::path WriteTempFile(const char* name, const char* content) {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / name;
    std::ofstream(path) << content;
    return path;
}

// Rewrites the file and bumps its timestamp explicitly, so the test never
// depends on filesystem mtime granularity being finer than the test's own
// runtime -- mirrors AutoRevertTest.cpp's own ExternalWrite exactly.
void ExternalWrite(const std::filesystem::path& path, const char* content) {
    std::ofstream(path, std::ios::trunc) << content;
    std::filesystem::last_write_time(path, std::filesystem::last_write_time(path) + std::chrono::seconds(2));
}

} // namespace

TEST_CASE("MergeExternalChanges cleanly combines a local edit with an independent external change",
          "[AutoMerge]") {
    const std::filesystem::path path = WriteTempFile("ned_automerge_test_clean.txt", "one\ntwo\nthree\n");

    ned::text::Buffer buffer = ned::text::Buffer::FromFile(path);
    buffer.SetPoint(buffer.Size());
    buffer.InsertAtPoint("four\n"); // local edit: append a line

    ExternalWrite(path, "one\nTWO\nthree\n"); // external edit: change a different line

    const std::size_t conflicts = buffer.MergeExternalChanges();
    REQUIRE(conflicts == 0);
    REQUIRE(buffer.Text() == "one\nTWO\nthree\nfour\n");
    REQUIRE(buffer.Modified()); // still unsaved -- disk itself was never written to
    REQUIRE_FALSE(buffer.ExternallyModified());

    buffer.Undo(); // one step undoes the whole merge
    REQUIRE(buffer.Text() == "one\ntwo\nthree\nfour\n");

    std::filesystem::remove(path);
}

TEST_CASE("MergeExternalChanges marks a genuine conflict and lands point at the first marker", "[AutoMerge]") {
    const std::filesystem::path path = WriteTempFile("ned_automerge_test_conflict.txt", "one\ntwo\nthree\n");

    ned::text::Buffer buffer = ned::text::Buffer::FromFile(path);
    buffer.SetPoint(4); // start of "two"
    buffer.DeleteRange(4, 3);
    buffer.InsertAtPoint("LOCAL"); // local edit: "two" -> "LOCAL"

    ExternalWrite(path, "one\nEXTERNAL\nthree\n"); // external edit: "two" -> "EXTERNAL"

    const std::size_t conflicts = buffer.MergeExternalChanges();
    REQUIRE(conflicts == 1);

    const std::string text          = buffer.Text();
    const std::size_t conflictStart = text.find("<<<<<<<");
    REQUIRE(conflictStart != std::string::npos);
    REQUIRE(text.find("LOCAL") != std::string::npos);
    REQUIRE(text.find("EXTERNAL") != std::string::npos);
    REQUIRE(buffer.Point() == conflictStart);
    REQUIRE(buffer.Modified());

    std::filesystem::remove(path);
}

TEST_CASE("AutoMergeBuffers merges modified-and-externally-modified buffers only, and honors the toggle",
          "[AutoMerge]") {
    const AutoMergeGuard        guard;
    const std::filesystem::path unmodifiedPath = WriteTempFile("ned_automerge_test_unmodified.txt", "clean\n");
    const std::filesystem::path mergePath      = WriteTempFile("ned_automerge_test_merge.txt", "one\ntwo\nthree\n");

    ned::text::BufferList bufferList;
    ned::text::Buffer&    unmodified = bufferList.OpenOrCreateFile(unmodifiedPath);
    ned::text::Buffer&    toMerge    = bufferList.OpenOrCreateFile(mergePath);
    toMerge.SetPoint(toMerge.Size());
    toMerge.InsertAtPoint("four\n");

    ExternalWrite(unmodifiedPath, "clean v2\n"); // AutoRevertBuffers' own job, not this one's
    ExternalWrite(mergePath, "one\nTWO\nthree\n");

    SetAutoMergeEnabled(false);
    REQUIRE(AutoMergeBuffers(bufferList).empty());
    REQUIRE(toMerge.Text() == "one\ntwo\nthree\nfour\n"); // untouched while disabled

    SetAutoMergeEnabled(true);
    const std::vector<AutoMergeResult> merged = AutoMergeBuffers(bufferList);
    REQUIRE(merged.size() == 1);
    REQUIRE(merged[0].name == toMerge.Name());
    REQUIRE(merged[0].conflictCount == 0);
    REQUIRE(toMerge.Text() == "one\nTWO\nthree\nfour\n");
    // The unmodified buffer is never touched here -- AutoRevertBuffers' own job.
    REQUIRE(unmodified.Text() == "clean\n");

    std::filesystem::remove(unmodifiedPath);
    std::filesystem::remove(mergePath);
}
