#include "BufferList.h"

#include <algorithm>

namespace ned::text {

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

Buffer& BufferList::OpenFile(const std::filesystem::path& path) {
    Buffer loaded = Buffer::FromFile(path); // throws on failure

    loaded.Rename(UniqueName(loaded.Name()));

    buffers_.push_back(std::make_unique<Buffer>(std::move(loaded)));
    return *buffers_.back();
}

Buffer& BufferList::OpenOrCreateFile(const std::filesystem::path& path) {
    if (std::filesystem::exists(path)) {
        return OpenFile(path);
    }

    Buffer created = Buffer::NewFile(path);
    created.Rename(UniqueName(created.Name()));

    buffers_.push_back(std::make_unique<Buffer>(std::move(created)));
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
