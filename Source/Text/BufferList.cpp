#include "BufferList.h"

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <system_error>

#include "BinaryDetect.h"

namespace ned::text {

namespace {
    // Comfortably above any real source file, comfortably below "blocking
    // the UI to load this is actually felt" -- see the large-file-async-
    // load follow-up plan for the reasoning. The default for the
    // configurable process-wide value below (loose-ends follow-up: grew a
    // Janet setter, ned/set-async-load-threshold, exactly the way
    // TabWidth's own hardcoded default once did).
    constexpr std::uintmax_t kDefaultAsyncLoadThreshold = 16 * 1024 * 1024;

    // Mutex-guarded static state, mirroring editor::TabWidth's exact
    // pattern -- lives here rather than in Source/Editor/ because
    // BufferList (this layer) is the consumer and text must not depend on
    // editor.
    std::mutex& ThresholdMutex() {
        static std::mutex mutex;
        return mutex;
    }

    std::uintmax_t& ThresholdStorage() {
        static std::uintmax_t threshold = kDefaultAsyncLoadThreshold;
        return threshold;
    }

    // huge-file-editing follow-up: comfortably above anything the async
    // loader (still a full in-memory Rope by the time it finishes) should
    // ever be asked to handle -- see HugeFileThreshold()'s own header
    // comment.
    constexpr std::uintmax_t kDefaultHugeFileThreshold = 1024ull * 1024 * 1024;

    std::mutex& HugeThresholdMutex() {
        static std::mutex mutex;
        return mutex;
    }

    std::uintmax_t& HugeThresholdStorage() {
        static std::uintmax_t threshold = kDefaultHugeFileThreshold;
        return threshold;
    }

    // disk-space-safety follow-up: 2x covers the new .ned-tmp file's own
    // full allocation plus a copy-on-write filesystem's old-blocks-not-
    // yet-reclaimed behavior -- see Text/DiskSpace.h's own header comment.
    constexpr double kDefaultHugeFileMinFreeSpaceMultiplier = 2.0;

    std::mutex& SpaceMultiplierMutex() {
        static std::mutex mutex;
        return mutex;
    }

    double& SpaceMultiplierStorage() {
        static double multiplier = kDefaultHugeFileMinFreeSpaceMultiplier;
        return multiplier;
    }

    std::mutex& SpaceCheckEnabledMutex() {
        static std::mutex mutex;
        return mutex;
    }

    bool& SpaceCheckEnabledStorage() {
        static bool enabled = true;
        return enabled;
    }
} // namespace

void SetAsyncLoadThreshold(std::uintmax_t bytes) {
    const std::lock_guard<std::mutex> lock(ThresholdMutex());
    ThresholdStorage() = bytes;
}

std::uintmax_t AsyncLoadThreshold() {
    const std::lock_guard<std::mutex> lock(ThresholdMutex());
    return ThresholdStorage();
}

void SetHugeFileThreshold(std::uintmax_t bytes) {
    const std::lock_guard<std::mutex> lock(HugeThresholdMutex());
    HugeThresholdStorage() = bytes;
}

std::uintmax_t HugeFileThreshold() {
    const std::lock_guard<std::mutex> lock(HugeThresholdMutex());
    return HugeThresholdStorage();
}

void SetHugeFileMinFreeSpaceMultiplier(double multiplier) {
    const std::lock_guard<std::mutex> lock(SpaceMultiplierMutex());
    SpaceMultiplierStorage() = multiplier > 0.0 ? multiplier : 0.0;
}

double HugeFileMinFreeSpaceMultiplier() {
    const std::lock_guard<std::mutex> lock(SpaceMultiplierMutex());
    return SpaceMultiplierStorage();
}

void SetHugeFileDiskSpaceCheckEnabled(bool enabled) {
    const std::lock_guard<std::mutex> lock(SpaceCheckEnabledMutex());
    SpaceCheckEnabledStorage() = enabled;
}

bool HugeFileDiskSpaceCheckEnabled() {
    const std::lock_guard<std::mutex> lock(SpaceCheckEnabledMutex());
    return SpaceCheckEnabledStorage();
}

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
    // Dedupe-by-path -- see the header's own comment on this contract.
    if (Buffer* existing = FindByPath(path)) {
        return *existing;
    }

    // Checked here, ahead of the size check below, so a large binary file
    // never even gets considered for the async path -- Buffer::FromFile
    // makes this same check for anyone calling it directly, but the async
    // branch below bypasses FromFile entirely, so it needs its own check.
    //
    // binary-safety-guardrails follow-up: computed unconditionally (not
    // just when !allowBinary) so a placeholder handed to either async
    // opener hook below can carry the same LikelyBinary() signal
    // Buffer::FromFile/FromHugeFile's own synchronous paths already set
    // internally -- see Buffer::SetLikelyBinary's own doc comment.
    const bool likelyBinary = LooksBinary(path);
    if (!allowBinary && likelyBinary) {
        throw BinaryFileError("ned: refusing to open binary file as text: " + path.string());
    }

    // huge-file-editing follow-up: checked ahead of the async-loader branch
    // below -- a file clearing both thresholds always takes this one, since
    // Buffer::FromHugeFile never fully materializes the file (the async
    // loader still does, eventually).
    //
    // progressive-huge-file-load follow-up: with asyncHugeFileOpener_ set,
    // this returns immediately with an empty, IsLoading()-but-editable
    // placeholder (same shape as the async-loader branch below) that the
    // hook fills in progressively over time -- see Source/UI/
    // HugeFileLoader.h. With no hook set (every test that constructs a bare
    // BufferList), this falls back to the original synchronous
    // Buffer::FromHugeFile call: opening is already fast on its own
    // (PieceTable::FromFile's own O(file size) scan, not a full read), so
    // there's nothing to background-load without a hook to drive it.
    {
        std::error_code      ec;
        const std::uintmax_t size = std::filesystem::file_size(path, ec);
        if (!ec && size > HugeFileThreshold()) {
            if (asyncHugeFileOpener_) {
                Buffer placeholder = Buffer::NewFile(path);
                placeholder.Rename(UniqueName(placeholder.Name()));
                placeholder.MarkLoading(/*forceReadOnly=*/false);
                placeholder.SetLikelyBinary(likelyBinary);

                buffers_.push_back(std::make_unique<Buffer>(std::move(placeholder)));
                Buffer& ref = *buffers_.back();
                asyncHugeFileOpener_(ref, path, allowBinary);
                if (onFileOpened_) {
                    onFileOpened_(ref);
                }
                return ref;
            }

            Buffer loaded = Buffer::FromHugeFile(path, allowBinary); // throws on failure

            loaded.Rename(UniqueName(loaded.Name()));

            buffers_.push_back(std::make_unique<Buffer>(std::move(loaded)));
            Buffer& ref = *buffers_.back();
            if (onFileOpened_) {
                onFileOpened_(ref);
            }
            return ref;
        }
    }

    if (asyncFileOpener_) {
        std::error_code      ec;
        const std::uintmax_t size = std::filesystem::file_size(path, ec);
        if (!ec && size > AsyncLoadThreshold()) {
            Buffer placeholder = Buffer::NewFile(path);
            placeholder.Rename(UniqueName(placeholder.Name()));
            placeholder.MarkLoading();
            placeholder.SetLikelyBinary(likelyBinary);

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

void BufferList::SetAsyncHugeFileOpener(std::function<void(Buffer&, const std::filesystem::path&, bool)> hook) {
    asyncHugeFileOpener_ = std::move(hook);
}

void BufferList::SetOnFileOpened(std::function<void(Buffer&)> hook) {
    onFileOpened_ = std::move(hook);
}

Buffer& BufferList::OpenOrCreateFile(const std::filesystem::path& path, bool allowBinary) {
    // Dedupe-by-path -- covers the NewFile branch below too (a second open
    // of a not-yet-saved path must reuse the pending buffer, not stack a
    // "name<2>" beside it); OpenFile repeats the check for its own direct
    // callers.
    if (Buffer* existing = FindByPath(path)) {
        return *existing;
    }

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

namespace {

    // weakly_canonical resolves symlinks and "."/".." through every
    // component that actually exists on disk (a not-yet-created NewFile
    // path keeps its trailing pieces as-is -- exactly right for matching a
    // pending unsaved buffer), so differently-spelled paths to the same
    // file compare equal. The error_code overload never throws; on any
    // failure fall back to plain absolute(), the pre-fix behavior.
    std::filesystem::path NormalizedPathKey(const std::filesystem::path& path) {
        std::error_code             ec;
        const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, ec);
        return ec ? std::filesystem::absolute(path) : canonical;
    }

} // namespace

Buffer* BufferList::FindByPath(const std::filesystem::path& path) {
    const std::filesystem::path key = NormalizedPathKey(path);
    for (auto& buffer : buffers_) {
        if (buffer->Path() && NormalizedPathKey(*buffer->Path()) == key) {
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

    std::erase(mruOrder_, it->get());
    buffers_.erase(it);
    return true;
}

void BufferList::TouchBuffer(const Buffer& buffer) {
    const auto owned = std::find_if(buffers_.begin(), buffers_.end(), [&buffer](const std::unique_ptr<Buffer>& b) {
        return b.get() == &buffer;
    });
    if (owned == buffers_.end()) {
        return;
    }
    Buffer* raw = owned->get();
    if (!mruOrder_.empty() && mruOrder_.back() == raw) {
        return;
    }
    std::erase(mruOrder_, raw);
    mruOrder_.push_back(raw);
}

Buffer* BufferList::MostRecentlyUsedBuffer(const Buffer* excluding) const {
    for (auto it = mruOrder_.rbegin(); it != mruOrder_.rend(); ++it) {
        if (*it != excluding) {
            return *it;
        }
    }
    return nullptr;
}

bool BufferList::MoveBufferToIndex(const Buffer& buffer, std::size_t targetIndex) {
    const auto it = std::find_if(buffers_.begin(), buffers_.end(), [&buffer](const std::unique_ptr<Buffer>& b) {
        return b.get() == &buffer;
    });
    if (it == buffers_.end()) {
        return false;
    }

    const std::size_t from = static_cast<std::size_t>(it - buffers_.begin());
    const std::size_t to   = std::min(targetIndex, buffers_.size() - 1);
    if (from == to) {
        return true;
    }

    if (from < to) {
        std::rotate(buffers_.begin() + static_cast<std::ptrdiff_t>(from),
                    buffers_.begin() + static_cast<std::ptrdiff_t>(from) + 1,
                    buffers_.begin() + static_cast<std::ptrdiff_t>(to) + 1);
    }
    else {
        std::rotate(buffers_.begin() + static_cast<std::ptrdiff_t>(to),
                    buffers_.begin() + static_cast<std::ptrdiff_t>(from),
                    buffers_.begin() + static_cast<std::ptrdiff_t>(from) + 1);
    }
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
