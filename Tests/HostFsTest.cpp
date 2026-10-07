#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <stdlib.h>
#include <sys/stat.h>

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
    // A FIFO with no writer would block the read forever.
    REQUIRE(::mkfifo((dir.path / "fifo").c_str(), 0600) == 0);
    CHECK_THROWS_WITH(host::ReadPath(dir.path / "fifo"), ContainsSubstring("is not a regular file"));
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

TEST_CASE("A host listing names a directory's entries and what each is", "[Host][HostFs]") {
    Dir dir;
    Write(dir.path / "a.txt", "a");
    fs::create_directory(dir.path / "sub");
    fs::create_symlink(dir.path / "sub", dir.path / "to-sub");
    fs::create_symlink(dir.path / "nowhere", dir.path / "dangling");

    std::vector<host::DirEntry> entries = host::ListPath(dir.path);
    std::ranges::sort(entries, {}, &host::DirEntry::name);
    CHECK(entries == std::vector<host::DirEntry>{{.name = "a.txt", .kind = host::FileKind::File, .link = false},
                                                 {.name = "dangling", .kind = host::FileKind::Missing, .link = true},
                                                 {.name = "sub", .kind = host::FileKind::Directory, .link = false},
                                                 {.name = "to-sub", .kind = host::FileKind::Directory, .link = true}});
    CHECK(host::ListPath(dir.path / "sub").empty());

    CHECK(CodeOf([&] { (void)host::ListPath(dir.path / "missing"); }) == fs_error::kNotFound);
    CHECK(CodeOf([&] { (void)host::ListPath(dir.path / "a.txt"); }) == fs_error::kFailed);
}

TEST_CASE("A host realpath resolves symlinks and a relative path against the working directory", "[Host][HostFs]") {
    Dir dir;
    fs::create_directory(dir.path / "sub");
    fs::create_symlink(dir.path / "sub", dir.path / "to-sub");
    const fs::path real = fs::canonical(dir.path);

    CHECK(host::RealPath(dir.path / "to-sub" / ".." / "to-sub") == real / "sub");
    CHECK(host::RealPath(".") == fs::current_path());
    CHECK(CodeOf([&] { (void)host::RealPath(dir.path / "missing"); }) == fs_error::kNotFound);
}

TEST_CASE("A host mkdir makes the directory and its missing parents", "[Host][HostFs]") {
    Dir dir;
    const host::FileStat made = host::MakeDirectory(dir.path / "a" / "b");
    CHECK(made.kind == host::FileKind::Directory);
    CHECK(fs::is_directory(dir.path / "a" / "b"));
    CHECK(host::MakeDirectory(dir.path / "a").kind == host::FileKind::Directory);

    Write(dir.path / "file", "x");
    CHECK(CodeOf([&] { (void)host::MakeDirectory(dir.path / "file"); }) == fs_error::kFailed);
}

TEST_CASE("A host rename moves a path and refuses to overwrite one", "[Host][HostFs]") {
    Dir dir;
    Write(dir.path / "a.txt", "a");
    Write(dir.path / "b.txt", "b");
    fs::create_symlink(dir.path / "nowhere", dir.path / "dangling");

    host::RenamePath({.from = dir.path / "a.txt", .to = dir.path / "c.txt"});
    CHECK(Read(dir.path / "c.txt") == "a");
    CHECK_FALSE(fs::exists(dir.path / "a.txt"));

    CHECK(CodeOf([&] { host::RenamePath({.from = dir.path / "c.txt", .to = dir.path / "b.txt"}); }) == fs_error::kFailed);
    CHECK(Read(dir.path / "b.txt") == "b");
    CHECK(CodeOf([&] { host::RenamePath({.from = dir.path / "missing", .to = dir.path / "d.txt"}); }) == fs_error::kNotFound);

    host::RenamePath({.from = dir.path / "dangling", .to = dir.path / "moved"});
    CHECK(fs::is_symlink(dir.path / "moved"));
}

TEST_CASE("A host remove deletes a file, a directory tree and a dangling link", "[Host][HostFs]") {
    Dir dir;
    fs::create_directories(dir.path / "tree" / "sub");
    Write(dir.path / "tree" / "sub" / "f", "x");
    Write(dir.path / "file", "x");
    fs::create_symlink(dir.path / "nowhere", dir.path / "dangling");

    host::RemovePath(dir.path / "tree");
    host::RemovePath(dir.path / "file");
    host::RemovePath(dir.path / "dangling");
    CHECK(fs::is_empty(dir.path));

    CHECK(CodeOf([&] { host::RemovePath(dir.path / "missing"); }) == fs_error::kNotFound);
}
