//
// On-disk file attributes a save must carry across, and the two policy
// questions that decide how a save writes at all.
//
// The atomic-write pattern used throughout this codebase (write a full
// sibling .ned-tmp, then rename over the target) replaces the target's
// *inode*, not its contents -- so everything the filesystem hangs off that
// inode is silently discarded unless it's explicitly reproduced: the mode
// bits (an executable script losing its +x on the first save is the
// reported symptom this module exists for), extended attributes (and
// therefore POSIX ACLs, which live in the system.posix_acl_access xattr),
// the symlink a path may have been reached through, and any additional
// hard links to the same content.
//
// Three of those four are fixed by capturing before the write and applying
// to the temp file before the rename. The fourth -- hard links -- can't
// be: a rename always produces a new inode, so a multiply-linked file has
// to be written *in place* to stay linked, trading this pattern's crash
// atomicity for link preservation. ShouldWriteInPlace is that decision;
// Buffer::SaveToFile is the caller that acts on it (and the one that pairs
// it with Editor/Backup.h's pre-save version, which is what makes the
// trade acceptable).
//
// Linux-specific (listxattr/getxattr/setxattr), the same platform
// assumption Text/MappedFile.cpp already makes.
//

#ifndef NED_TEXT_FILEPRESERVATION_H
#define NED_TEXT_FILEPRESERVATION_H

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace ned::text {

// What CaptureFileAttributes read off a file about to be replaced.
// existed == false means there was nothing there (a brand-new file), in
// which case ApplyFileAttributes is a no-op and the umask-default
// permissions the temp file was created with are exactly right.
struct PreservedFileAttributes {
    bool                   existed       = false;
    std::filesystem::perms permissions   = std::filesystem::perms::none;
    std::uintmax_t         hardLinkCount = 1;
    // name -> value, in the user.* and system.* namespaces only. security.*
    // and trusted.* are deliberately skipped: setting either needs
    // privileges an editor won't have, so collecting them would only ever
    // produce failures to swallow.
    std::vector<std::pair<std::string, std::string>> extendedAttributes;
};

// The file a save of `path` should actually write. When path is a symlink
// and FollowSymlinksOnSave() is on, this is the link's resolved target
// (chains and dangling links included -- a dangling link resolves to the
// not-yet-existing file it names, which is what the save then creates);
// otherwise it's path unchanged.
//
// Callers must derive their temp-file path from this result rather than
// from the original: a symlink can point across a filesystem boundary, and
// std::filesystem::rename is only atomic -- only *works*, in fact -- when
// both paths sit on the same filesystem, which only a sibling of the real
// target guarantees.
[[nodiscard]] std::filesystem::path ResolveSaveTarget(const std::filesystem::path& path);

// Reads path's current attributes, to be reapplied after it's replaced.
// Never throws: an unreadable/nonexistent path reports existed == false,
// which downstream treats as "brand-new file, nothing to preserve".
[[nodiscard]] PreservedFileAttributes CaptureFileAttributes(const std::filesystem::path& path);

// Reproduces captured attributes onto a freshly written temp file, before
// it's renamed into place. Best-effort and never throws -- a target
// filesystem that rejects xattrs entirely must not turn a working save
// into a failed one.
//
// Order is load-bearing: permissions first, xattrs second. Writing
// system.posix_acl_access rewrites the mode's group bits as a side effect,
// so applying the mode afterwards would clobber the ACL it just restored.
void ApplyFileAttributes(const std::filesystem::path& path, const PreservedFileAttributes& attributes);

// Whether a new file could be created alongside `target` -- i.e. whether
// the sibling-temp-then-rename path is available for it at all. False for a
// writable file inside a directory this process can't write to, which is a
// real situation (a read-only source tree with one file made writable, a
// directory owned by someone else) and the *only* reason a save is allowed
// to fall back to writing in place.
//
// Deliberately narrower than "the temp file failed to open": that also
// happens when the disk is full or something already occupies the temp
// path, and falling back to an in-place truncate there would destroy the
// original file the atomic path exists to protect.
[[nodiscard]] bool CanCreateSiblingFile(const std::filesystem::path& target);

// Whether a save must write in place (truncate the existing inode) rather
// than temp-then-rename. True only for an existing, multiply-linked file
// while PreserveHardLinksOnSave() is on -- every other file takes the
// atomic path unchanged.
[[nodiscard]] bool ShouldWriteInPlace(const PreservedFileAttributes& attributes);

// Mutex-guarded process-wide settings (the pattern used throughout;
// they live here rather than in Editor/ for the same reason
// SetAsyncLoadThreshold does -- Text/ is the consumer and must not depend
// on Editor/). Janet surface: ned/set-follow-symlinks-on-save,
// ned/set-preserve-hard-links-on-save.
void               SetFollowSymlinksOnSave(bool enabled);
[[nodiscard]] bool FollowSymlinksOnSave(); // default true
void               SetPreserveHardLinksOnSave(bool enabled);
[[nodiscard]] bool PreserveHardLinksOnSave(); // default true

} // namespace ned::text

#endif // NED_TEXT_FILEPRESERVATION_H
