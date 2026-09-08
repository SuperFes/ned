// file-attribute-preservation follow-up: the atomic save pattern used
// throughout this codebase replaces a file's inode, which silently
// discarded everything the filesystem hangs off it -- an executable script
// losing its +x on the first save being the symptom that started this.
// These cases pin what a save must now carry across (mode bits, xattrs and
// therefore ACLs, symlink identity, hard links) and the two toggles that
// govern the parts with a real trade-off behind them.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include <sys/xattr.h>
#include <unistd.h>

#include "Text/Buffer.h"
#include "Text/FilePreservation.h"

using ned::text::Buffer;
using ned::text::CaptureFileAttributes;
using ned::text::PreservedFileAttributes;
using ned::text::ResolveSaveTarget;

namespace {

// Process-wide settings (see FilePreservation.h); any test flipping one
// must leave both default-on for the next -- the FileWatchGuard pattern.
struct PreservationGuard {
    ~PreservationGuard() {
        ned::text::SetFollowSymlinksOnSave(true);
        ned::text::SetPreserveHardLinksOnSave(true);
    }
};

std::filesystem::path MakeTempDir(const char* name) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

void WriteFile(const std::filesystem::path& path, const std::string& content) {
    std::ofstream(path, std::ios::binary | std::ios::trunc) << content;
}

std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

std::filesystem::perms PermissionsOf(const std::filesystem::path& path) {
    return std::filesystem::status(path).permissions();
}

// Not every filesystem a temp directory can land on supports extended
// attributes (some tmpfs configurations don't). Probing once per case and
// skipping is the only honest option -- asserting would turn an
// environmental fact into a spurious failure.
bool SupportsXattrs(const std::filesystem::path& path) {
    const std::string native = path.string();
    if (::setxattr(native.c_str(), "user.ned_probe", "1", 1, 0) != 0) {
        return false;
    }
    ::removexattr(native.c_str(), "user.ned_probe");
    return true;
}

std::string ReadXattr(const std::filesystem::path& path, const char* name) {
    const std::string native = path.string();
    const ssize_t     size   = ::getxattr(native.c_str(), name, nullptr, 0);
    if (size <= 0) {
        return {};
    }
    std::string   value(static_cast<std::size_t>(size), '\0');
    const ssize_t written = ::getxattr(native.c_str(), name, value.data(), value.size());
    if (written < 0) {
        return {};
    }
    value.resize(static_cast<std::size_t>(written));
    return value;
}

Buffer OpenAndEdit(const std::filesystem::path& path, const std::string& appended) {
    Buffer buffer = Buffer::FromFile(path);
    buffer.SetPoint(buffer.Size());
    buffer.InsertAtPoint(appended);
    return buffer;
}

} // namespace

TEST_CASE("Saving preserves the executable bit", "[FilePreservation]") {
    const std::filesystem::path dir  = MakeTempDir("ned_preservation_exec");
    const std::filesystem::path file = dir / "test.sh";
    WriteFile(file, "#!/bin/sh\necho hi\n");
    std::filesystem::permissions(file, std::filesystem::perms::owner_all | std::filesystem::perms::group_read |
                                           std::filesystem::perms::group_exec | std::filesystem::perms::others_read |
                                           std::filesystem::perms::others_exec);

    const std::filesystem::perms before = PermissionsOf(file);

    Buffer buffer = OpenAndEdit(file, "echo more\n");
    buffer.Save();

    REQUIRE(PermissionsOf(file) == before);
    REQUIRE((PermissionsOf(file) & std::filesystem::perms::owner_exec) != std::filesystem::perms::none);
    REQUIRE(ReadFile(file) == "#!/bin/sh\necho hi\necho more\n");
}

TEST_CASE("Saving preserves setuid, setgid and sticky bits", "[FilePreservation]") {
    const std::filesystem::path dir  = MakeTempDir("ned_preservation_special");
    const std::filesystem::path file = dir / "special.txt";
    WriteFile(file, "content\n");

    const std::filesystem::perms special = std::filesystem::perms::owner_all | std::filesystem::perms::set_uid |
                                           std::filesystem::perms::set_gid | std::filesystem::perms::sticky_bit;
    std::error_code              ec;
    std::filesystem::permissions(file, special, std::filesystem::perm_options::replace, ec);
    // A filesystem mounted nosuid (or a restrictive kernel policy) can
    // refuse these outright -- there's nothing to test if they never landed.
    if (ec || (PermissionsOf(file) & std::filesystem::perms::set_uid) == std::filesystem::perms::none) {
        SUCCEED("filesystem does not support setuid bits here");
        return;
    }

    Buffer buffer = OpenAndEdit(file, "more\n");
    buffer.Save();

    REQUIRE(PermissionsOf(file) == special);
}

TEST_CASE("A brand-new file is created with ordinary default permissions", "[FilePreservation]") {
    const std::filesystem::path dir  = MakeTempDir("ned_preservation_new");
    const std::filesystem::path file = dir / "fresh.txt";

    Buffer buffer = Buffer::NewFile(file);
    buffer.InsertAtPoint("hello\n");
    buffer.Save();

    REQUIRE(std::filesystem::exists(file));
    // Nothing was preserved (there was nothing there) -- in particular the
    // save must not have invented an executable bit for a plain text file.
    REQUIRE((PermissionsOf(file) & std::filesystem::perms::owner_exec) == std::filesystem::perms::none);
    REQUIRE((PermissionsOf(file) & std::filesystem::perms::owner_write) != std::filesystem::perms::none);
}

TEST_CASE("Saving through a symlink updates the target, not the link", "[FilePreservation]") {
    const std::filesystem::path dir    = MakeTempDir("ned_preservation_symlink");
    const std::filesystem::path target = dir / "real.sh";
    const std::filesystem::path link   = dir / "link.sh";
    WriteFile(target, "original\n");
    std::filesystem::permissions(target, std::filesystem::perms::owner_all);
    std::filesystem::create_symlink(target, link);

    Buffer buffer = OpenAndEdit(link, "appended\n");
    buffer.Save();

    REQUIRE(std::filesystem::is_symlink(link));
    REQUIRE(std::filesystem::read_symlink(link) == target);
    REQUIRE(ReadFile(target) == "original\nappended\n");
    REQUIRE((PermissionsOf(target) & std::filesystem::perms::owner_exec) != std::filesystem::perms::none);
    // Path() keeps the identity the buffer was opened under, not the
    // resolved target -- see Buffer::SaveToFile's own doc comment.
    REQUIRE(buffer.Path() == link);
}

TEST_CASE("Saving through a symlink into another directory works", "[FilePreservation]") {
    // The temp file has to be sibling'd to the resolved target, not to the
    // link -- otherwise the rename crosses directories (and, for a link
    // across a filesystem boundary, fails outright).
    const std::filesystem::path dir       = MakeTempDir("ned_preservation_symlink_dir");
    const std::filesystem::path targetDir = dir / "elsewhere";
    std::filesystem::create_directories(targetDir);
    const std::filesystem::path target = targetDir / "real.txt";
    const std::filesystem::path link   = dir / "link.txt";
    WriteFile(target, "original\n");
    std::filesystem::create_symlink(target, link);

    Buffer buffer = OpenAndEdit(link, "appended\n");
    buffer.Save();

    REQUIRE(std::filesystem::is_symlink(link));
    REQUIRE(ReadFile(target) == "original\nappended\n");
    REQUIRE_FALSE(std::filesystem::exists(dir / "link.txt.ned-tmp"));
    REQUIRE_FALSE(std::filesystem::exists(targetDir / "real.txt.ned-tmp"));
}

TEST_CASE("A chain of symlinks resolves to the file at the end", "[FilePreservation]") {
    const std::filesystem::path dir    = MakeTempDir("ned_preservation_symlink_chain");
    const std::filesystem::path target = dir / "real.txt";
    const std::filesystem::path middle = dir / "middle.txt";
    const std::filesystem::path outer  = dir / "outer.txt";
    WriteFile(target, "original\n");
    std::filesystem::create_symlink(target, middle);
    std::filesystem::create_symlink(middle, outer);

    Buffer buffer = OpenAndEdit(outer, "appended\n");
    buffer.Save();

    REQUIRE(std::filesystem::is_symlink(outer));
    REQUIRE(std::filesystem::is_symlink(middle));
    REQUIRE(ReadFile(target) == "original\nappended\n");
}

TEST_CASE("Saving through a dangling symlink creates its target", "[FilePreservation]") {
    const std::filesystem::path dir    = MakeTempDir("ned_preservation_symlink_dangling");
    const std::filesystem::path target = dir / "missing.txt";
    const std::filesystem::path link   = dir / "link.txt";
    std::filesystem::create_symlink(target, link);

    REQUIRE(ResolveSaveTarget(link) == target);

    Buffer buffer = Buffer::NewFile(link);
    buffer.InsertAtPoint("created\n");
    buffer.Save();

    REQUIRE(std::filesystem::is_symlink(link));
    REQUIRE(std::filesystem::exists(target));
    REQUIRE(ReadFile(target) == "created\n");
}

TEST_CASE("set-follow-symlinks-on-save off replaces the link itself", "[FilePreservation]") {
    const PreservationGuard     guard;
    const std::filesystem::path dir    = MakeTempDir("ned_preservation_symlink_off");
    const std::filesystem::path target = dir / "real.txt";
    const std::filesystem::path link   = dir / "link.txt";
    WriteFile(target, "original\n");
    std::filesystem::create_symlink(target, link);

    ned::text::SetFollowSymlinksOnSave(false);

    Buffer buffer = OpenAndEdit(link, "appended\n");
    buffer.Save();

    REQUIRE_FALSE(std::filesystem::is_symlink(link));
    REQUIRE(ReadFile(link) == "original\nappended\n");
    REQUIRE(ReadFile(target) == "original\n");
}

TEST_CASE("Saving a hard-linked file keeps every link on the same content", "[FilePreservation]") {
    const std::filesystem::path dir   = MakeTempDir("ned_preservation_hardlink");
    const std::filesystem::path first = dir / "first.txt";
    const std::filesystem::path other = dir / "other.txt";
    WriteFile(first, "original\n");
    std::filesystem::create_hard_link(first, other);
    REQUIRE(std::filesystem::hard_link_count(first) == 2);

    Buffer buffer = OpenAndEdit(first, "appended\n");
    buffer.Save();

    REQUIRE(std::filesystem::hard_link_count(first) == 2);
    REQUIRE(std::filesystem::equivalent(first, other));
    REQUIRE(ReadFile(first) == "original\nappended\n");
    REQUIRE(ReadFile(other) == "original\nappended\n");
}

TEST_CASE("A hard-linked file's permissions survive the in-place write", "[FilePreservation]") {
    const std::filesystem::path dir   = MakeTempDir("ned_preservation_hardlink_perms");
    const std::filesystem::path first = dir / "first.sh";
    const std::filesystem::path other = dir / "other.sh";
    WriteFile(first, "original\n");
    std::filesystem::permissions(first, std::filesystem::perms::owner_all);
    std::filesystem::create_hard_link(first, other);

    Buffer buffer = OpenAndEdit(first, "appended\n");
    buffer.Save();

    REQUIRE((PermissionsOf(first) & std::filesystem::perms::owner_exec) != std::filesystem::perms::none);
}

TEST_CASE("set-preserve-hard-links-on-save off takes the atomic path and breaks the link", "[FilePreservation]") {
    const PreservationGuard     guard;
    const std::filesystem::path dir   = MakeTempDir("ned_preservation_hardlink_off");
    const std::filesystem::path first = dir / "first.txt";
    const std::filesystem::path other = dir / "other.txt";
    WriteFile(first, "original\n");
    std::filesystem::create_hard_link(first, other);

    ned::text::SetPreserveHardLinksOnSave(false);

    Buffer buffer = OpenAndEdit(first, "appended\n");
    buffer.Save();

    REQUIRE_FALSE(std::filesystem::equivalent(first, other));
    REQUIRE(ReadFile(first) == "original\nappended\n");
    REQUIRE(ReadFile(other) == "original\n");
    // The mode still crosses the rename, even though the link didn't.
    REQUIRE(std::filesystem::hard_link_count(first) == 1);
}

TEST_CASE("Saving preserves extended attributes", "[FilePreservation]") {
    const std::filesystem::path dir  = MakeTempDir("ned_preservation_xattr");
    const std::filesystem::path file = dir / "tagged.txt";
    WriteFile(file, "original\n");

    if (!SupportsXattrs(file)) {
        SUCCEED("filesystem does not support extended attributes here");
        return;
    }

    const std::string native = file.string();
    REQUIRE(::setxattr(native.c_str(), "user.ned_test", "value", 5, 0) == 0);

    Buffer buffer = OpenAndEdit(file, "appended\n");
    buffer.Save();

    REQUIRE(ReadXattr(file, "user.ned_test") == "value");
}

TEST_CASE("CaptureFileAttributes reports a missing file as not existing", "[FilePreservation]") {
    const std::filesystem::path dir = MakeTempDir("ned_preservation_capture");

    const PreservedFileAttributes missing = CaptureFileAttributes(dir / "nope.txt");
    REQUIRE_FALSE(missing.existed);

    const std::filesystem::path file = dir / "here.txt";
    WriteFile(file, "x\n");
    const PreservedFileAttributes present = CaptureFileAttributes(file);
    REQUIRE(present.existed);
    REQUIRE(present.hardLinkCount == 1);
}

TEST_CASE("ResolveSaveTarget leaves an ordinary path alone", "[FilePreservation]") {
    const std::filesystem::path dir  = MakeTempDir("ned_preservation_resolve");
    const std::filesystem::path file = dir / "plain.txt";
    WriteFile(file, "x\n");

    REQUIRE(ResolveSaveTarget(file) == file);
    REQUIRE(ResolveSaveTarget(dir / "does_not_exist.txt") == dir / "does_not_exist.txt");
}

TEST_CASE("A writable file in an unwritable directory still saves, in place", "[FilePreservation]") {
    // The atomic path needs to create a sibling temp file, which this
    // directory won't allow -- the save falls back to rewriting the file's
    // own inode rather than refusing outright.
    const std::filesystem::path dir  = MakeTempDir("ned_preservation_ro_dir");
    const std::filesystem::path file = dir / "writable.txt";
    WriteFile(file, "original\n");

    // Running as root ignores directory permissions entirely, so there'd be
    // nothing to exercise.
    if (::geteuid() == 0) {
        SUCCEED("running as root; directory permissions are not enforced");
        return;
    }

    std::filesystem::permissions(dir, std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec,
                                 std::filesystem::perm_options::replace);
    REQUIRE_FALSE(ned::text::CanCreateSiblingFile(file));

    Buffer buffer = OpenAndEdit(file, "appended\n");
    buffer.Save();

    REQUIRE(ReadFile(file) == "original\nappended\n");

    // Restored so the directory can be cleaned up by the next run.
    std::filesystem::permissions(dir, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace);
}

TEST_CASE("CanCreateSiblingFile is true for an ordinary writable directory", "[FilePreservation]") {
    const std::filesystem::path dir = MakeTempDir("ned_preservation_sibling");
    REQUIRE(ned::text::CanCreateSiblingFile(dir / "anything.txt"));
}
