#include "FilePreservation.h"

#include <mutex>
#include <string_view>
#include <system_error>
#include <vector>

#include <sys/xattr.h>
#include <unistd.h>

namespace ned::text {

namespace {

    std::mutex& FollowSymlinksMutex() {
        static std::mutex mutex;
        return mutex;
    }

    bool& FollowSymlinksStorage() {
        static bool enabled = true;
        return enabled;
    }

    std::mutex& PreserveHardLinksMutex() {
        static std::mutex mutex;
        return mutex;
    }

    bool& PreserveHardLinksStorage() {
        static bool enabled = true;
        return enabled;
    }

    // A namespace we can realistically restore as an unprivileged process --
    // see PreservedFileAttributes::extendedAttributes' own comment.
    bool IsRestorableXattr(std::string_view name) {
        return name.starts_with("user.") || name.starts_with("system.");
    }

    // listxattr/getxattr both report a required buffer size when handed a
    // zero-length one, but the file can change between that call and the
    // read -- hence the retry rather than trusting the first answer.
    // Returns false on any error; every caller treats that as "nothing to
    // preserve here", never as a failure worth propagating.
    bool ReadXattrNames(const std::string& path, std::vector<std::string>& names) {
        for (int attempt = 0; attempt < 2; ++attempt) {
            const ssize_t needed = ::listxattr(path.c_str(), nullptr, 0);
            if (needed <= 0) {
                return needed == 0; // 0 == no xattrs at all, which is success
            }

            std::vector<char> buffer(static_cast<std::size_t>(needed));
            const ssize_t     written = ::listxattr(path.c_str(), buffer.data(), buffer.size());
            if (written < 0) {
                continue; // grew between the two calls -- ask again
            }

            // The result is a run of NUL-terminated names, not a delimited list.
            std::size_t start = 0;
            for (std::size_t i = 0; i < static_cast<std::size_t>(written); ++i) {
                if (buffer[i] == '\0') {
                    if (i > start) {
                        names.emplace_back(buffer.data() + start, i - start);
                    }
                    start = i + 1;
                }
            }
            return true;
        }
        return false;
    }

    bool ReadXattrValue(const std::string& path, const std::string& name, std::string& value) {
        for (int attempt = 0; attempt < 2; ++attempt) {
            const ssize_t needed = ::getxattr(path.c_str(), name.c_str(), nullptr, 0);
            if (needed < 0) {
                return false;
            }
            if (needed == 0) {
                value.clear();
                return true;
            }

            std::string   buffer(static_cast<std::size_t>(needed), '\0');
            const ssize_t written = ::getxattr(path.c_str(), name.c_str(), buffer.data(), buffer.size());
            if (written < 0) {
                continue;
            }
            buffer.resize(static_cast<std::size_t>(written));
            value = std::move(buffer);
            return true;
        }
        return false;
    }

} // namespace

void SetFollowSymlinksOnSave(bool enabled) {
    const std::lock_guard<std::mutex> lock(FollowSymlinksMutex());
    FollowSymlinksStorage() = enabled;
}

bool FollowSymlinksOnSave() {
    const std::lock_guard<std::mutex> lock(FollowSymlinksMutex());
    return FollowSymlinksStorage();
}

void SetPreserveHardLinksOnSave(bool enabled) {
    const std::lock_guard<std::mutex> lock(PreserveHardLinksMutex());
    PreserveHardLinksStorage() = enabled;
}

bool PreserveHardLinksOnSave() {
    const std::lock_guard<std::mutex> lock(PreserveHardLinksMutex());
    return PreserveHardLinksStorage();
}

std::filesystem::path ResolveSaveTarget(const std::filesystem::path& path) {
    if (!FollowSymlinksOnSave()) {
        return path;
    }

    std::error_code ec;
    if (!std::filesystem::is_symlink(std::filesystem::symlink_status(path, ec)) || ec) {
        return path;
    }

    // Hopped by hand rather than via weakly_canonical, which resolves only
    // as far as components that *exist* -- and a dangling symlink doesn't,
    // as far as exists() is concerned, so it would hand back the link
    // itself and the save would replace the link instead of creating the
    // file it names. Each hop is resolved against its own link's directory,
    // since a link target may be relative.
    constexpr int         kMaxSymlinkHops = 40; // SYMLOOP_MAX's usual value
    std::filesystem::path current         = path;
    for (int hop = 0; hop < kMaxSymlinkHops; ++hop) {
        if (!std::filesystem::is_symlink(std::filesystem::symlink_status(current, ec)) || ec) {
            break;
        }

        const std::filesystem::path linkTarget = std::filesystem::read_symlink(current, ec);
        if (ec || linkTarget.empty()) {
            return path;
        }
        current = linkTarget.is_absolute() ? linkTarget : current.parent_path() / linkTarget;
    }

    // Now that no symlink remains in the final component, normalize the
    // rest (".."/"." and any symlinked parent directory) as far as the
    // filesystem goes -- a still-nonexistent target just keeps its
    // lexically normalized form, which is exactly what the save creates.
    const std::filesystem::path resolved = std::filesystem::weakly_canonical(current, ec);
    if (ec || resolved.empty()) {
        return current.lexically_normal();
    }
    return resolved;
}

PreservedFileAttributes CaptureFileAttributes(const std::filesystem::path& path) {
    PreservedFileAttributes attributes;

    std::error_code                    ec;
    const std::filesystem::file_status status = std::filesystem::status(path, ec);
    if (ec || !std::filesystem::exists(status)) {
        return attributes;
    }

    attributes.existed     = true;
    attributes.permissions = status.permissions();

    const std::uintmax_t links = std::filesystem::hard_link_count(path, ec);
    attributes.hardLinkCount   = ec ? 1 : links;

    const std::string        native = path.string();
    std::vector<std::string> names;
    if (ReadXattrNames(native, names)) {
        for (const std::string& name : names) {
            if (!IsRestorableXattr(name)) {
                continue;
            }
            std::string value;
            if (ReadXattrValue(native, name, value)) {
                attributes.extendedAttributes.emplace_back(name, std::move(value));
            }
        }
    }

    return attributes;
}

void ApplyFileAttributes(const std::filesystem::path& path, const PreservedFileAttributes& attributes) {
    if (!attributes.existed) {
        return;
    }

    std::error_code ec;
    std::filesystem::permissions(path, attributes.permissions, std::filesystem::perm_options::replace, ec);

    // See the header: xattrs strictly after permissions, since restoring an
    // ACL rewrites the mode.
    const std::string native = path.string();
    for (const auto& [name, value] : attributes.extendedAttributes) {
        ::setxattr(native.c_str(), name.c_str(), value.data(), value.size(), 0);
    }
}

bool CanCreateSiblingFile(const std::filesystem::path& target) {
    std::filesystem::path directory = target.parent_path();
    if (directory.empty()) {
        directory = ".";
    }
    return ::access(directory.c_str(), W_OK | X_OK) == 0;
}

bool ShouldWriteInPlace(const PreservedFileAttributes& attributes) {
    return attributes.existed && attributes.hardLinkCount > 1 && PreserveHardLinksOnSave();
}

} // namespace ned::text
