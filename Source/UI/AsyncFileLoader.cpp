#include "AsyncFileLoader.h"

#include <chrono>
#include <fstream>
#include <system_error>

#include "EventLoop.h"
#include "Text/Charset.h"
#include "Text/LineEnding.h"
#include "Text/Rope.h"

namespace ned::ui {

namespace {
    constexpr std::size_t               kChunkBytes = 4 * 1024 * 1024;
    constexpr std::chrono::milliseconds kPreviewInterval{200};
} // namespace

AsyncFileLoader::AsyncFileLoader(text::Buffer& placeholder, text::BufferList& bufferList, std::filesystem::path path,
                                 EventLoop& eventLoop, std::function<void(text::Buffer&)> onBufferClosing) : bufferList_(bufferList), onBufferClosing_(std::move(onBufferClosing)), bufferName_(placeholder.Name()) {
    // totalBytes written before thread_ starts, per LoadProgress's contract
    // -- a failed size query just leaves 0, which ModeLine treats as
    // "unknown, show no percentage" rather than an error.
    std::error_code sizeEc;
    if (const std::uintmax_t size = std::filesystem::file_size(path, sizeEc); !sizeEc) {
        progress_->totalBytes = size;
    }
    placeholder.SetLoadProgress(progress_);

    thread_ = std::jthread(
        [this, path = std::move(path), &eventLoop](std::stop_token stopToken) { Run(stopToken, path, eventLoop); });
}

AsyncFileLoader::~AsyncFileLoader() {
    if (thread_.joinable()) {
        thread_.request_stop();
    }
}

bool AsyncFileLoader::Done() const {
    return done_;
}

void AsyncFileLoader::DiscardPlaceholder() {
    if (text::Buffer* placeholder = bufferList_.Find(bufferName_)) {
        if (onBufferClosing_) {
            onBufferClosing_(*placeholder);
        }
        bufferList_.Close(bufferName_);
    }
    done_ = true;
}

void AsyncFileLoader::Run(std::stop_token stopToken, std::filesystem::path path, EventLoop& eventLoop) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        eventLoop.Post([this] { DiscardPlaceholder(); });
        return;
    }

    std::string content;
    std::string chunk(kChunkBytes, '\0');
    bool          sniffed     = false;
    text::Charset charset     = text::Charset::Utf8;
    auto        lastPreview = std::chrono::steady_clock::now();

    while (!stopToken.stop_requested()) {
        file.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        const auto bytesRead = static_cast<std::size_t>(file.gcount());
        if (bytesRead == 0) {
            break;
        }
        content.append(chunk.data(), bytesRead);
        progress_->bytesRead.fetch_add(bytesRead, std::memory_order_relaxed);

        if (!sniffed) {
            sniffed = true;
            charset = text::SniffCharset(content);
            if (text::CharsetConverts(charset)) {
                content.erase(0, text::CharsetPreamble(charset).size());
            }
            else {
                charset = text::Charset::Utf8; // kept byte for byte (Text/Charset.h)
            }
        }

        if (file.bad()) {
            eventLoop.Post([this] { DiscardPlaceholder(); });
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        if (now - lastPreview >= kPreviewInterval) {
            lastPreview = now;
            // crlf-handling follow-up: normalized for display only -- the
            // accumulating `content` above stays raw so a '\r'/'\n' pair
            // split across a chunk boundary is never mis-detected.
            // detection/LineEndingKind() are only settled once, at
            // FinishLoad below, against the complete file.
            text::Rope preview(text::HasCarriageReturn(content) ? text::NormalizeToLf(content) : content);
            eventLoop.Post([this, preview] {
                if (text::Buffer* buffer = bufferList_.Find(bufferName_)) {
                    buffer->ReplaceContentForLoad(preview);
                }
            });
        }

        if (bytesRead < chunk.size()) {
            break; // short read -- EOF
        }
    }

    if (stopToken.stop_requested()) {
        return; // loader destroyed (buffer closed / app exiting) -- nothing left to post
    }

    const text::LineEnding detectedEnding = text::DetectLineEnding(content);
    text::Rope             finalContent(text::HasCarriageReturn(content) ? text::NormalizeToLf(content) : content);
    eventLoop.Post([this, finalContent, detectedEnding, charset] {
        if (text::Buffer* buffer = bufferList_.Find(bufferName_)) {
            buffer->FinishLoad(finalContent, detectedEnding, charset);
        }
        done_ = true;
    });
}

} // namespace ned::ui
