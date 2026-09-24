#include "FileMagic.h"

#include <magic.h>

#include <memory>
#include <mutex>

namespace ned::editor {

namespace {

    struct MagicCloser {
        void operator()(magic_t cookie) const {
            magic_close(cookie);
        }
    };
    using MagicHandle = std::unique_ptr<std::remove_pointer_t<magic_t>, MagicCloser>;

    std::mutex& MagicMutex() {
        static std::mutex mutex;
        return mutex;
    }

    // Null when the database would not load; tried once.
    magic_t Cookie() {
        static const MagicHandle handle = [] {
            MagicHandle opened(magic_open(MAGIC_MIME_TYPE));
            if (opened && magic_load(opened.get(), nullptr) != 0) {
                opened.reset();
            }
            return opened;
        }();
        return handle.get();
    }

} // namespace

std::optional<std::string> MimeTypeOf(std::string_view bytes) {
    const std::lock_guard<std::mutex> lock(MagicMutex());
    const magic_t                     cookie = Cookie();
    if (cookie == nullptr) {
        return std::nullopt;
    }
    const char* mime = magic_buffer(cookie, bytes.data(), bytes.size());
    if (mime == nullptr) {
        return std::nullopt;
    }
    return std::string(mime);
}

} // namespace ned::editor
