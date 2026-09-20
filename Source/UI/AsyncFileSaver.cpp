#include "AsyncFileSaver.h"

#include <chrono>
#include <exception>

#include "EventLoop.h"

namespace ned::ui {

namespace {
    constexpr std::chrono::milliseconds kProgressRepaintInterval{200};
} // namespace

AsyncFileSaver::AsyncFileSaver(editor::AsyncSaveRequest request, text::BufferList& bufferList, EventLoop& eventLoop) :
    bufferList_(bufferList), bufferName_(std::move(request.bufferName)), bufferPath_(std::move(request.bufferPath)),
    plan_(std::move(request.plan)) {
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

const std::string& AsyncFileSaver::Error() const {
    return error_;
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
            if (buffer != nullptr) {
                buffer->FinishSave(bufferPath_, std::move(plan_));
            }
        }
        else {
            if (buffer != nullptr) {
                buffer->AbandonSave();
            }
            error_ = std::move(failure);
        }
        done_ = true;
    });
}

} // namespace ned::ui
