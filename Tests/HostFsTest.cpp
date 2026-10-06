#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <stdlib.h>

#include "Editor/Host/Fs.h"

using Catch::Matchers::ContainsSubstring;
namespace fs       = std::filesystem;
namespace host     = ned::editor::host;
namespace fs_error = ned::editor::host::fs_error;

namespace {

struct Dir {
    fs::path path;
    Dir() {
        std::string pattern = (fs::temp_directory_path() / "ned-host-fs-XXXXXX").string();
        REQUIRE(::mkdtemp(pattern.data()) != nullptr);
        path = pattern;
    }
    ~Dir() {
        fs::remove_all(path);
    }
    Dir(const Dir&)            = delete;
    Dir& operator=(const Dir&) = delete;
};

void Write(const fs::path& path, const std::string& bytes) {
    std::ofstream(path, std::ios::binary) << bytes;
}

std::string Read(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

int CodeOf(auto&& operation) {
    try {
        operation();
    }
    catch (const host::FsError& e) {
        return e.Code();
    }
    return 0;
}

} // namespace

TEST_CASE("A host stat says what is at a path", "[Host][HostFs]") {
    const Dir dir;
    Write(dir.path / "a.sh", "echo\n");
    fs::permissions(dir.path / "a.sh", fs::perms::owner_all | fs::perms::group_read);
    fs::create_symlink(dir.path / "a.sh", dir.path / "link");
    fs::create_symlink(dir.path / "gone", dir.path / "dangling");

    const host::FileStat file = host::StatPath(dir.path / "a.sh");
    CHECK(file.kind == host::FileKind::File);
    CHECK(file.size == 5);
    CHECK(file.mode == 0740);
    CHECK(file.timestamp == fs::last_write_time(dir.path / "a.sh"));
    CHECK(host::StatPath(dir.path / "link") == file);
    CHECK(host::StatPath(dir.path).kind == host::FileKind::Directory);
    CHECK(host::StatPath(dir.path / "dangling") == host::FileStat{});
    CHECK(host::StatPath(dir.path / "nothing") == host::FileStat{});
    CHECK(host::StatPath("/dev/null").kind == host::FileKind::Other);
}

TEST_CASE("A host read returns the bytes and the timestamp before them", "[Host][HostFs]") {
    const Dir         dir;
    const std::string bytes("\xFF\xFE\x00\r\n", 5);
    Write(dir.path / "f", bytes);

    const ned::text::FileContents contents = host::ReadPath(dir.path / "f");
    CHECK(contents.bytes == bytes);
    CHECK(contents.timestamp == fs::last_write_time(dir.path / "f"));
    CHECK(CodeOf([&] { (void)host::ReadPath(dir.path / "missing"); }) == fs_error::kNotFound);
    CHECK(CodeOf([&] { (void)host::ReadPath(dir.path); }) == fs_error::kFailed);
}

TEST_CASE("A host write replaces the file as a local save does", "[Host][HostFs]") {
    const Dir      dir;
    const fs::path real = dir.path / "real.sh";
    Write(real, "old\n");
    fs::permissions(real, fs::perms::owner_all);
    fs::create_symlink(real, dir.path / "link.sh");

    const host::FileStat written = host::WritePath({.path = dir.path / "link.sh", .data = "new\n", .expect = std::nullopt});
    CHECK(Read(real) == "new\n");
    CHECK(fs::is_symlink(dir.path / "link.sh"));
    CHECK(written.kind == host::FileKind::File);
    CHECK(written.size == 4);
    CHECK(written.mode == 0700);
    CHECK(written.timestamp == fs::last_write_time(real));
    CHECK_FALSE(fs::exists(dir.path / "real.sh.ned-tmp"));

    // A new file.
    CHECK(host::WritePath({.path = dir.path / "fresh", .data = "x", .expect = std::optional<fs::file_time_type>()}).size == 1);
    CHECK(Read(dir.path / "fresh") == "x");
    CHECK(CodeOf([&] { (void)host::WritePath({.path = dir.path / "no" / "dir", .data = "x", .expect = std::nullopt}); }) == fs_error::kFailed);
}

TEST_CASE("A host write refuses a file that changed since it was read", "[Host][HostFs]") {
    const Dir      dir;
    const fs::path file = dir.path / "f";
    Write(file, "seen\n");
    const auto seen = fs::last_write_time(file);

    fs::last_write_time(file, seen + std::chrono::seconds(5));
    CHECK(CodeOf([&] { (void)host::WritePath({.path = file, .data = "mine\n", .expect = seen}); }) == fs_error::kFileChanged);
    CHECK(Read(file) == "seen\n");
    // Expected absent, but there.
    CHECK(CodeOf([&] { (void)host::WritePath({.path = file, .data = "mine\n", .expect = std::optional<fs::file_time_type>()}); }) == fs_error::kFileChanged);
    // Expected present, but gone.
    CHECK_THROWS_WITH(host::WritePath({.path = dir.path / "gone", .data = "mine\n", .expect = seen}), ContainsSubstring("was removed since it was read"));
    CHECK_FALSE(fs::exists(dir.path / "gone"));

    CHECK(host::WritePath({.path = file, .data = "mine\n", .expect = seen + std::chrono::seconds(5)}).size == 5);
    CHECK(Read(file) == "mine\n");
}
