#include "BufferList.h"

#include <algorithm>
#include <system_error>

#include "BinaryDetect.h"

namespace ned::text {

namespace {
    // Comfortably above any real source file, comfortably below "blocking
    // the UI to load this is actually felt" -- see the large-file-async-
    // load follow-up plan for the reasoning; not exposed as a Janet/config
    // setting yet, the same "hardcoded C++ for now" scope call TabWidth's
    // own default originally was before it grew a setter.
    constexpr std::uintmax_t kAsyncLoadThreshold = 16 * 1024 * 1024;
} // namespace

std::string BufferList::UniqueName(const std::string& base) const {
    if (!Find(base)) {
        return base;
    }

    for (std::size_t n = 2;; ++n) {
        std::string candidate = base + "<" + std::to_string(n) + ">";
        if (!Find(candidate)) {
            return candidate;
        }
    }
}

Buffer& BufferList::CreateBuffer(std::string name) {
    buffers_.push_back(std::make_unique<Buffer>(UniqueName(name)));
    return *buffers_.back();
}

Buffer& BufferList::OpenFile(const std::filesystem::path& path, bool allowBinary) {
    // Checked here, ahead of the size check below, so a large binary file
    // never even gets considered for the async path -- Buffer::FromFile
    // makes this same check for anyone calling it directly, but the async
    // branch below bypasses FromFile entirely, so it needs its own check.
    if (!allowBinary && LooksBinary(path)) {
        throw BinaryFileError("ned: refusing to open binary file as text: " + path.string());
    }

    if (asyncFileOpener_) {
        std::error_code      ec;
        const std::uintmax_t size = std::filesystem::file_size(path, ec);
        if (!ec && size > kAsyncLoadThreshold) {
            Buffer placeholder = Buffer::NewFile(path);
            placeholder.Rename(UniqueName(placeholder.Name()));
            placeholder.MarkLoading();

            buffers_.push_back(std::make_unique<Buffer>(std::move(placeholder)));
            Buffer& ref = *buffers_.back();
            asyncFileOpener_(ref, path);
            if (onFileOpened_) {
                onFileOpened_(ref);
            }
            return ref;
        }
    }

    Buffer loaded = Buffer::FromFile(path, allowBinary); // throws on failure

    loaded.Rename(UniqueName(loaded.Name()));

    buffers_.push_back(std::make_unique<Buffer>(std::move(loaded)));
    if (onFileOpened_) {
        onFileOpened_(*buffers_.back());
    }
    return *buffers_.back();
}

void BufferList::SetAsyncFileOpener(std::function<void(Buffer&, const std::filesystem::path&)> hook) {
    asyncFileOpener_ = std::move(hook);
}

void BufferList::SetOnFileOpened(std::function<void(Buffer&)> hook) {
    onFileOpened_ = std::move(hook);
}

Buffer& BufferList::OpenOrCreateFile(const std::filesystem::path& path, bool allowBinary) {
    if (std::filesystem::exists(path)) {
        return OpenFile(path, allowBinary);
    }

    Buffer created = Buffer::NewFile(path);
    created.Rename(UniqueName(created.Name()));

    buffers_.push_back(std::make_unique<Buffer>(std::move(created)));
    if (onFileOpened_) {
        onFileOpened_(*buffers_.back());
    }
    return *buffers_.back();
}

Buffer* BufferList::Find(const std::string& name) {
    for (auto& buffer : buffers_) {
        if (buffer->Name() == name) {
            return buffer.get();
        }
    }
    return nullptr;
}

const Buffer* BufferList::Find(const std::string& name) const {
    for (const auto& buffer : buffers_) {
        if (buffer->Name() == name) {
            return buffer.get();
        }
    }
    return nullptr;
}

Buffer* BufferList::FindByPath(const std::filesystem::path& path) {
    const std::filesystem::path absolute = std::filesystem::absolute(path);
    for (auto& buffer : buffers_) {
        if (buffer->Path() && std::filesystem::absolute(*buffer->Path()) == absolute) {
            return buffer.get();
        }
    }
    return nullptr;
}

const Buffer* BufferList::FindByPath(const std::filesystem::path& path) const {
    return const_cast<BufferList*>(this)->FindByPath(path);
}

bool BufferList::Close(const std::string& name) {
    const auto it = std::find_if(buffers_.begin(), buffers_.end(), [&name](const std::unique_ptr<Buffer>& buffer) {
        return buffer->Name() == name;
    });

    if (it == buffers_.end()) {
        return false;
    }

    if (previewBuffer_ == it->get()) {
        previewBuffer_ = nullptr;
    }

    buffers_.erase(it);
    return true;
}

Buffer* BufferList::PreviewBuffer() const {
    if (previewBuffer_ != nullptr && previewBuffer_->Modified()) {
        previewBuffer_ = nullptr;
    }
    return previewBuffer_;
}

void BufferList::SetPreviewBuffer(Buffer* buffer) {
    previewBuffer_ = buffer;
}

std::size_t BufferList::Count() const {
    return buffers_.size();
}

const std::vector<std::unique_ptr<Buffer>>& BufferList::Buffers() const {
    return buffers_;
}

std::vector<std::string> CompleteFilePath(std::string_view prefix) {
    const std::size_t           lastSlash       = prefix.find_last_of('/');
    const std::string           directoryPrefix = (lastSlash == std::string_view::npos) ? std::string()
                                                                                        : std::string(prefix.substr(0, lastSlash + 1));
    const std::string           filenamePrefix  = std::string(prefix.substr(lastSlash == std::string_view::npos ? 0 : lastSlash + 1));
    const std::filesystem::path directory       = directoryPrefix.empty() ? std::filesystem::path(".") : std::filesystem::path(directoryPrefix);

    std::vector<std::string> matches;
    try {
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            const std::string name = entry.path().filename().string();
            if (name.compare(0, filenamePrefix.size(), filenamePrefix) != 0) {
                continue;
            }

            std::string candidate = directoryPrefix + name;
            if (entry.is_directory()) {
                candidate += '/';
            }
            matches.push_back(std::move(candidate));
        }
    }
    catch (const std::filesystem::filesystem_error&) {
        return {}; // directory doesn't exist, no permission, etc. -- no candidates
    }

    std::sort(matches.begin(), matches.end());
    return matches;
}

std::vector<std::string> CompleteBufferNames(const BufferList& bufferList, std::string_view prefix) {
    std::vector<std::string> matches;
    for (const auto& buffer : bufferList.Buffers()) {
        const std::string& name = buffer->Name();
        if (name.size() >= prefix.size() && std::string_view(name).substr(0, prefix.size()) == prefix) {
            matches.push_back(name);
        }
    }
    std::sort(matches.begin(), matches.end());
    return matches;
}

} // namespace ned::text
