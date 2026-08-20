#include "AsyncFileLoader.h"

#include <chrono>
#include <fstream>
#include <system_error>

#include "EventLoop.h"
#include "Text/Rope.h"

namespace ned::ui {

namespace {
    constexpr std::size_t               kChunkBytes = 4 * 1024 * 1024;
    constexpr std::chrono::milliseconds kPreviewInterval{200};
    constexpr std::string_view          kUtf8Bom = "\xEF\xBB\xBF";
} // namespace

AsyncFileLoader::AsyncFileLoader(text::Buffer& placeholder, text::BufferList& bufferList, std::filesystem::path path,
                                 EventLoop& eventLoop) : bufferList_(bufferList), bufferName_(placeholder.Name()) {
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

void AsyncFileLoader::Run(std::stop_token stopToken, std::filesystem::path path, EventLoop& eventLoop) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        eventLoop.Post([this] {
            bufferList_.Close(bufferName_);
            done_ = true;
        });
        return;
    }

    std::string content;
    std::string chunk(kChunkBytes, '\0');
    bool        strippedBom = false;
    auto        lastPreview = std::chrono::steady_clock::now();

    while (!stopToken.stop_requested()) {
        file.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        const auto bytesRead = static_cast<std::size_t>(file.gcount());
        if (bytesRead == 0) {
            break;
        }
        content.append(chunk.data(), bytesRead);
        progress_->bytesRead.fetch_add(bytesRead, std::memory_order_relaxed);

        if (!strippedBom) {
            strippedBom = true;
            if (content.starts_with(kUtf8Bom)) {
                content.erase(0, kUtf8Bom.size());
            }
        }

        if (file.bad()) {
            eventLoop.Post([this] {
                bufferList_.Close(bufferName_);
                done_ = true;
            });
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        if (now - lastPreview >= kPreviewInterval) {
            lastPreview = now;
            text::Rope preview(content);
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

    text::Rope finalContent(content);
    eventLoop.Post([this, finalContent] {
        if (text::Buffer* buffer = bufferList_.Find(bufferName_)) {
            buffer->FinishLoad(finalContent);
        }
        done_ = true;
    });
}

} // namespace ned::ui
