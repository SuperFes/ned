#include "AsyncFileSaver.h"

#include <chrono>
#include <exception>

#include "Application.h"
#include "EventLoop.h"

namespace ned::ui {

namespace {
    constexpr std::chrono::milliseconds kProgressRepaintInterval{200};
} // namespace

AsyncFileSaver::AsyncFileSaver(editor::AsyncSaveRequest request, text::BufferList& bufferList, EventLoop& eventLoop,
                               std::function<void(std::string)> onFailure) :
    bufferList_(bufferList), bufferName_(std::move(request.bufferName)), bufferPath_(std::move(request.bufferPath)),
    plan_(std::move(request.plan)), onFailure_(std::move(onFailure)) {
    // The write reports into the buffer's SaveProgress on its own (the
    // dispatcher installed that sink), but a number nothing reads is
    // invisible: the event loop only repaints when something wakes it.
    // Chaining an empty Post onto the existing sink is that wake -- rate
    // limited, since a flush boundary comes round far more often than a
    // frame is worth drawing.
    plan_.onProgress = [sink = std::move(plan_.onProgress), &eventLoop,
                        last = std::chrono::steady_clock::time_point{}](std::uintmax_t bytesWritten) mutable {
        if (sink) {
            sink(bytesWritten);
        }
        const auto now = std::chrono::steady_clock::now();
        if (now - last >= kProgressRepaintInterval) {
            last = now;
            eventLoop.Post([] {});
        }
    };

    thread_ = std::jthread([this, &eventLoop](std::stop_token) { Run(eventLoop); });
}

AsyncFileSaver::~AsyncFileSaver() = default;

bool AsyncFileSaver::Done() const {
    return done_;
}

const std::string& AsyncFileSaver::BufferName() const {
    return bufferName_;
}

void AsyncFileSaver::Run(EventLoop& eventLoop) {
    std::string failure;
    try {
        editor::RunSavePlanWithBackup(plan_, bufferPath_);
    }
    catch (const std::exception& e) {
        failure = e.what();
    }

    // The buffer's own state is settled back on the main thread, where
    // every other reader of it lives. A buffer closed while the write ran
    // simply isn't found -- the file is written either way, which is what
    // the user asked for; there is just no buffer left to mark saved.
    eventLoop.Post([this, failure = std::move(failure)]() mutable {
        text::Buffer* buffer = bufferList_.Find(bufferName_);
        if (failure.empty()) {
            // Cleared even when the buffer is gone: what the exit code is
            // about is whether the file is on disk, and it is.
            Ned::Application::NoteSaveSucceeded(bufferPath_);
            if (buffer != nullptr) {
                buffer->FinishSave(bufferPath_, std::move(plan_));
            }
        }
        else {
            if (buffer != nullptr) {
                buffer->AbandonSave();
            }
            // Queued for the way out as well as shown now. The mode line is
            // the right place to say it while the editor is up, but a save
            // is exactly the operation whose failure must not depend on the
            // user still being there to read a status line -- they may well
            // have quit in the same breath, and if the buffer was closed
            // mid-write there is no mode line left to say it in at all.
            Ned::Application::NoteSaveFailed(bufferPath_, bufferName_, failure);
            if (onFailure_) {
                onFailure_(std::move(failure));
            }
        }
        done_ = true;
    });
}

} // namespace ned::ui
