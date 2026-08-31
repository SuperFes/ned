#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include "Text/Buffer.h"
#include "Text/BinaryDetect.h"
#include "Text/BufferList.h"

using ned::text::BinaryFileError;
using ned::text::Buffer;
using ned::text::HugeFileDiskSpaceCheckEnabled;
using ned::text::HugeFileMinFreeSpaceMultiplier;
using ned::text::SetHugeFileDiskSpaceCheckEnabled;
using ned::text::SetHugeFileMinFreeSpaceMultiplier;

namespace {
std::filesystem::path WriteTempFile(const std::string& name, std::string_view content) {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / name;
    std::ofstream                file(path, std::ios::binary);
    file << content;
    return path;
}

#if defined(__linux__)
// VmRSS in kB, per proc(5) -- same technique PieceTableTest.cpp's own
// [memory] test uses (duplicated here rather than shared: each test binary
// translation unit is independent, and this is a three-line helper).
std::size_t CurrentRssKb() {
    std::ifstream status("/proc/self/status");
    std::string   line;
    while (std::getline(status, line)) {
        if (line.starts_with("VmRSS:")) {
            return static_cast<std::size_t>(std::stoul(line.substr(line.find_first_of("0123456789"))));
        }
    }
    return 0;
}
#endif
} // namespace

TEST_CASE("Buffer::FromHugeFile opens content correctly and reports IsHuge", "[Buffer][HugeFile]") {
    const std::filesystem::path path = WriteTempFile("ned_buffer_huge_basic.txt", "hello, huge world\nsecond line\n");

    Buffer buffer = Buffer::FromHugeFile(path);

    REQUIRE(buffer.Content().IsHuge());
    REQUIRE(buffer.Text() == "hello, huge world\nsecond line\n");
    REQUIRE(buffer.Size() == 30);
    REQUIRE_FALSE(buffer.Modified());
    REQUIRE(buffer.Path().has_value());
    REQUIRE(*buffer.Path() == path);

    std::filesystem::remove(path);
}

TEST_CASE("Buffer::FromHugeFile throws for a missing file", "[Buffer][HugeFile]") {
    // Same ordering/behavior as Buffer::FromFile's own precedent: LooksBinary
    // is checked first and treats an unreadable path as "binary" (its own
    // documented contract -- "nothing useful to do with it either way"), so
    // a missing file surfaces as BinaryFileError here too, never reaching
    // PieceTable::FromFile/MappedFileError at all. BinaryFileError is-a
    // std::runtime_error (matching FromFile's own "throws for a missing
    // file" test), so this is the accurate expectation, not a looser one.
    REQUIRE_THROWS_AS(Buffer::FromHugeFile(std::filesystem::temp_directory_path() / "ned_buffer_huge_missing.txt"),
                      BinaryFileError);
}

TEST_CASE("Buffer::FromHugeFile refuses a binary file, mirroring FromFile", "[Buffer][HugeFile]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_buffer_huge_binary.bin";
    {
        std::ofstream file(path, std::ios::binary);
        file.put('a');
        file.put('\0');
        file.put('b');
    }

    REQUIRE_THROWS_AS(Buffer::FromHugeFile(path), BinaryFileError);

    Buffer allowed = Buffer::FromHugeFile(path, /*allowBinary=*/true);
    REQUIRE(allowed.Size() == 3);

    std::filesystem::remove(path);
}

TEST_CASE("Buffer::FromHugeFile refuses a CRLF file rather than silently keeping raw CR bytes", "[Buffer][HugeFile]") {
    const std::filesystem::path path = WriteTempFile("ned_buffer_huge_crlf.txt", "line one\r\nline two\r\n");

    REQUIRE_THROWS_AS(Buffer::FromHugeFile(path), std::runtime_error);

    // The same file still opens fine via the normal loader -- this is a
    // "this path doesn't support it yet" refusal, not a real file problem.
    Buffer normal = Buffer::FromFile(path);
    REQUIRE(normal.Text() == "line one\nline two\n");

    std::filesystem::remove(path);
}

TEST_CASE("Buffer::FromHugeFile strips a leading UTF-8 BOM, mirroring FromFile", "[Buffer][HugeFile]") {
    const std::string           withBom = "\xEF\xBB\xBFhello";
    const std::filesystem::path path    = WriteTempFile("ned_buffer_huge_bom.txt", withBom);

    Buffer buffer = Buffer::FromHugeFile(path);
    REQUIRE(buffer.Text() == "hello");

    std::filesystem::remove(path);
}

TEST_CASE("Buffer::FromHugeFile supports insert/delete/undo/redo like a normal buffer", "[Buffer][HugeFile]") {
    const std::filesystem::path path = WriteTempFile("ned_buffer_huge_edit.txt", "hello world");

    Buffer buffer = Buffer::FromHugeFile(path);

    buffer.SetPoint(5);
    buffer.InsertAtPoint(",");
    REQUIRE(buffer.Text() == "hello, world");
    REQUIRE(buffer.Modified());

    buffer.SetPoint(buffer.Size());
    buffer.InsertAtPoint("!");
    REQUIRE(buffer.Text() == "hello, world!");

    buffer.SetPoint(0);
    buffer.DeleteForwardAtPoint();
    REQUIRE(buffer.Text() == "ello, world!");

    REQUIRE(buffer.CanUndo());
    buffer.Undo();
    buffer.Undo();
    REQUIRE(buffer.Text() == "hello, world");
    REQUIRE(buffer.CanRedo());
    buffer.Redo();
    REQUIRE(buffer.Text() == "hello, world!");

    std::filesystem::remove(path);
}

TEST_CASE("Buffer::FromHugeFile SaveToFile round-trips edited content", "[Buffer][HugeFile]") {
    const std::filesystem::path path = WriteTempFile("ned_buffer_huge_save.txt", "original content\nsecond line\n");

    Buffer buffer = Buffer::FromHugeFile(path);
    buffer.SetPoint(buffer.Size());
    buffer.InsertAtPoint("third line\n");
    buffer.Save();

    REQUIRE_FALSE(buffer.Modified());

    std::ifstream saved(path, std::ios::binary);
    const std::string savedContent((std::istreambuf_iterator<char>(saved)), std::istreambuf_iterator<char>());
    REQUIRE(savedContent == "original content\nsecond line\nthird line\n");

    std::filesystem::remove(path);
}

TEST_CASE("Buffer::FromHugeFile handles a real multi-MB file: edit at start/middle/end, save, byte-for-byte", "[Buffer][HugeFile]") {
    // The user explicitly said large temp test files are fine here --
    // exercises PieceTable's chunking/BuildBalancedSpan across many leaves,
    // not just a handful, and the streaming save path against a real
    // multi-leaf tree.
    constexpr std::size_t kLineCount = 200000; // ~2.7 MB
    std::string           reference;
    reference.reserve(kLineCount * 14);
    for (std::size_t i = 0; i < kLineCount; ++i) {
        reference += "line number " + std::to_string(i) + "\n";
    }

    const std::filesystem::path path = WriteTempFile("ned_buffer_huge_multimb.txt", reference);

    Buffer buffer = Buffer::FromHugeFile(path);
    REQUIRE(buffer.Text() == reference);

    // Edit at the very start.
    buffer.SetPoint(0);
    buffer.InsertAtPoint(">>>START>>>\n");
    reference.insert(0, ">>>START>>>\n");

    // Edit somewhere in the middle.
    const std::size_t middle = buffer.Size() / 2;
    buffer.SetPoint(middle);
    buffer.InsertAtPoint("<<<MIDDLE<<<\n");
    reference.insert(middle, "<<<MIDDLE<<<\n");

    // Edit at the very end.
    buffer.SetPoint(buffer.Size());
    buffer.InsertAtPoint("<<<END<<<\n");
    reference += "<<<END<<<\n";

    // Delete a chunk out of the middle-ish region too, to exercise Erased()
    // against a multi-leaf tree, not just Inserted().
    const std::size_t deleteAt       = buffer.Size() / 3;
    const std::string expectedDeleted = reference.substr(deleteAt, 50);
    const std::string deleted         = buffer.DeleteRange(deleteAt, 50);
    reference.erase(deleteAt, 50);
    REQUIRE(deleted == expectedDeleted);

    REQUIRE(buffer.Text() == reference);

    buffer.Save();
    std::ifstream saved(path, std::ios::binary);
    const std::string savedContent((std::istreambuf_iterator<char>(saved)), std::istreambuf_iterator<char>());
    REQUIRE(savedContent == reference);

    std::filesystem::remove(path);
}

#if defined(__linux__)
TEST_CASE("Buffer::FromHugeFile SaveToFile does not materialize the whole document", "[Buffer][HugeFile][memory]") {
    // The entire point of streaming save (StreamingSaveWriter, Buffer.cpp):
    // Storage_->ToString() must never run for a huge buffer's save. Prove
    // it the same way PieceTableTest.cpp's own [memory] test proves
    // FromFile's residency claim -- measured RSS growth, not "it's fast" or
    // "it's correct" alone. 220 MiB, comfortably past PieceTableTest.cpp's
    // 200 MiB open-time benchmark, so a regression back to whole-string
    // save would be unambiguous (a spurious ~220 MiB RSS spike), not
    // something noise could hide.
    constexpr std::size_t kFileSize = 220 * 1024 * 1024;

    std::string chunk(1024, 'x');
    for (std::size_t i = 0; i < chunk.size(); ++i) {
        chunk[i] = static_cast<char>('a' + (i % 26));
    }
    chunk.back() = '\n';

    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_buffer_huge_save_rss.txt";
    {
        std::ofstream file(path, std::ios::binary);
        for (std::size_t written = 0; written < kFileSize; written += chunk.size()) {
            file.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        }
    }

    Buffer buffer = Buffer::FromHugeFile(path);
    REQUIRE(buffer.Content().IsHuge());

    // A small edit -- exactly the "open a huge file, tweak one thing, save"
    // workflow this whole feature exists for.
    buffer.SetPoint(0);
    buffer.InsertAtPoint(">>> edited >>>\n");

    const std::size_t rssBeforeKb = CurrentRssKb();
    buffer.SaveToFile(path, /*ensureFinalNewline=*/true, /*trimTrailingWhitespace=*/true);
    const std::size_t rssAfterKb = CurrentRssKb();

    const std::size_t growthKb = rssAfterKb > rssBeforeKb ? rssAfterKb - rssBeforeKb : 0;
    // Generous margin (< 50 MiB) against a file whose whole-string
    // materialization would be ~220 MiB -- the failure mode this guards
    // against is "the whole file ended up resident during save", which
    // would blow well past this bound, not a tight budget on incidental
    // allocation (the ~256 KiB output buffer, temporary strings, etc.).
    // Measured in practice: ~128 KiB.
    REQUIRE(growthKb < kFileSize / 1024 / 4);

    // Correctness, not just memory: the edit actually landed on disk.
    std::ifstream      saved(path, std::ios::binary);
    std::string         firstLine;
    std::getline(saved, firstLine);
    REQUIRE(firstLine == ">>> edited >>>");

    std::filesystem::remove(path);
}
#endif

namespace {
struct DiskSpaceSettingsGuard {
    ~DiskSpaceSettingsGuard() {
        SetHugeFileMinFreeSpaceMultiplier(2.0);
        SetHugeFileDiskSpaceCheckEnabled(true);
    }
};
} // namespace

TEST_CASE("Buffer::FromHugeFile downgrades to read-only when free space is insufficient", "[Buffer][HugeFile][DiskSpace]") {
    DiskSpaceSettingsGuard guard;
    const std::filesystem::path path = WriteTempFile("ned_buffer_huge_diskspace_open.txt", "some content\n");

    // An absurd multiplier makes "insufficient" true regardless of the real
    // machine's actual free space -- the same technique BufferListTest.cpp
    // already uses for threshold settings (temporarily set, restored by the
    // guard above).
    SetHugeFileMinFreeSpaceMultiplier(1e18);

    Buffer buffer = Buffer::FromHugeFile(path);

    REQUIRE(buffer.ReadOnly());
    REQUIRE(buffer.ReadOnlyReason().has_value());
    REQUIRE(buffer.ReadOnlyReason()->find("free disk space") != std::string::npos);
    REQUIRE(buffer.Text() == "some content\n"); // still fully readable

    REQUIRE_THROWS_WITH(buffer.InsertAtPoint("x"), Catch::Matchers::ContainsSubstring("free disk space"));

    std::filesystem::remove(path);
}

TEST_CASE("Buffer::FromHugeFile stays editable when free space is sufficient", "[Buffer][HugeFile][DiskSpace]") {
    DiskSpaceSettingsGuard guard;
    const std::filesystem::path path = WriteTempFile("ned_buffer_huge_diskspace_ok.txt", "some content\n");

    SetHugeFileMinFreeSpaceMultiplier(0.0); // trivially satisfied regardless of real free space

    Buffer buffer = Buffer::FromHugeFile(path);

    REQUIRE_FALSE(buffer.ReadOnly());
    REQUIRE_FALSE(buffer.ReadOnlyReason().has_value());

    std::filesystem::remove(path);
}

TEST_CASE("toggle-read-only's mechanism (SetReadOnly(false)) overrides the open-time downgrade", "[Buffer][HugeFile][DiskSpace]") {
    DiskSpaceSettingsGuard guard;
    const std::filesystem::path path = WriteTempFile("ned_buffer_huge_diskspace_override.txt", "hello");

    SetHugeFileMinFreeSpaceMultiplier(1e18);
    Buffer buffer = Buffer::FromHugeFile(path);
    REQUIRE(buffer.ReadOnly());

    // The real override path -- same call toggle-read-only (Commands.cpp) makes.
    buffer.SetReadOnly(false);
    REQUIRE_FALSE(buffer.ReadOnly());
    REQUIRE_FALSE(buffer.ReadOnlyReason().has_value()); // cleared, not left stale

    buffer.SetPoint(0);
    buffer.InsertAtPoint(">>>");
    REQUIRE(buffer.Text() == ">>>hello");

    std::filesystem::remove(path);
}

TEST_CASE("Buffer::SaveToFile refuses an unsafe huge save even after the open-time override", "[Buffer][HugeFile][DiskSpace]") {
    DiskSpaceSettingsGuard guard;
    const std::filesystem::path path = WriteTempFile("ned_buffer_huge_diskspace_save_backstop.txt", "hello");

    SetHugeFileMinFreeSpaceMultiplier(1e18);
    Buffer buffer = Buffer::FromHugeFile(path);
    buffer.SetReadOnly(false); // override the soft downgrade
    buffer.SetPoint(0);
    buffer.InsertAtPoint(">>>");

    const std::filesystem::path tempPath = path.string() + ".ned-tmp";
    std::filesystem::remove(tempPath); // just in case a prior run left one

    REQUIRE_THROWS_WITH(buffer.Save(), Catch::Matchers::ContainsSubstring("free disk space"));

    // No wasted I/O: the doomed write never even opened the temp file.
    REQUIRE_FALSE(std::filesystem::exists(tempPath));
    // The original file is untouched.
    std::ifstream original(path, std::ios::binary);
    const std::string originalContent((std::istreambuf_iterator<char>(original)), std::istreambuf_iterator<char>());
    REQUIRE(originalContent == "hello");

    std::filesystem::remove(path);
}

TEST_CASE("HugeFileDiskSpaceCheckEnabled(false) skips both the open-time and save-time checks", "[Buffer][HugeFile][DiskSpace]") {
    DiskSpaceSettingsGuard guard;
    const std::filesystem::path path = WriteTempFile("ned_buffer_huge_diskspace_disabled.txt", "hello");

    SetHugeFileMinFreeSpaceMultiplier(1e18); // would otherwise always fail
    SetHugeFileDiskSpaceCheckEnabled(false);

    Buffer buffer = Buffer::FromHugeFile(path);
    REQUIRE_FALSE(buffer.ReadOnly());

    buffer.SetPoint(0);
    buffer.InsertAtPoint(">>>");
    buffer.Save(); // must not throw

    std::filesystem::remove(path);
}
