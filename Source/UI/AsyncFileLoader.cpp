#include "AsyncFileLoader.h"

#include <chrono>
#include <fstream>
#include <iterator>
#include <optional>
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

    std::string                         content;
    std::string                         chunk(kChunkBytes, '\0');
    std::optional<text::Charset>        charset;
    std::optional<text::CharsetDecoder> decoder;
    bool                                undecodable = false;
    auto                                lastPreview = std::chrono::steady_clock::now();

    // BufferList only sends a file here once its head decodes, so this is a
    // malformed stretch further in: the file's bytes are kept exactly as
    // they are, the way a confirmed binary open keeps them.
    const auto keepRawBytes = [&] {
        undecodable = true;
        charset     = text::Charset::Utf8;
        file.clear();
        file.seekg(0);
        content.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    };

    while (!stopToken.stop_requested()) {
        file.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        const auto bytesRead = static_cast<std::size_t>(file.gcount());
        if (bytesRead == 0) {
            break;
        }
        progress_->bytesRead.fetch_add(bytesRead, std::memory_order_relaxed);

        std::string_view bytes(chunk.data(), bytesRead);
        if (!charset) {
            charset = text::ResolveLoadCharset(path, bytes, std::nullopt);
            decoder.emplace(*charset);
            bytes.remove_prefix(text::PreambleLength(bytes, *charset));
        }
        if (!decoder->Feed(bytes, content)) {
            keepRawBytes();
            break;
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

    if (!undecodable && decoder && !decoder->Finish()) {
        keepRawBytes(); // ended partway through a character
    }

    const text::Charset    finalCharset   = charset.value_or(text::Charset::Utf8);
    const text::LineEnding detectedEnding = text::DetectLineEnding(content);
    text::Rope             finalContent(text::HasCarriageReturn(content) ? text::NormalizeToLf(content) : content);
    eventLoop.Post([this, finalContent, detectedEnding, finalCharset, undecodable] {
        if (text::Buffer* buffer = bufferList_.Find(bufferName_)) {
            if (undecodable) {
                buffer->SetLikelyBinary(true);
            }
            buffer->FinishLoad(finalContent, detectedEnding, finalCharset);
        }
        done_ = true;
    });
}

} // namespace ned::ui
