// editor::WriteBufferToDisk's dispatch decision: which saves leave the main
// thread, which never do, and what the buffer's state is in between.
//
// No threads here -- the dispatcher is a hook, so a test can stand in for
// the real background saver and drive the halves by hand. That is exactly
// what the asynchronous path does, just without the waiting.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <unistd.h>

#include "Editor/BufferSave.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"

using ned::editor::AsyncSaveRequest;
using ned::editor::SaveDispatch;
using ned::editor::SetAsyncSaveDispatcher;
using ned::editor::ShouldSaveAsynchronously;
using ned::editor::WriteBufferToDisk;
using ned::text::Buffer;

namespace {

std::filesystem::path TempPath(const std::string& name) {
    return std::filesystem::temp_directory_path() / ("ned_async_dispatch_" + name + "_" + std::to_string(::getpid()));
}

std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

// Restores the process-wide dispatcher and threshold whatever the test did,
// so one case can't leak either into the next.
class DispatchFixture {
  public:
    explicit DispatchFixture(std::uintmax_t threshold) : originalThreshold_(ned::text::AsyncLoadThreshold()) {
        ned::text::SetAsyncLoadThreshold(threshold);
    }
    ~DispatchFixture() {
        SetAsyncSaveDispatcher(nullptr);
        ned::text::SetAsyncLoadThreshold(originalThreshold_);
    }
    DispatchFixture(const DispatchFixture&)            = delete;
    DispatchFixture& operator=(const DispatchFixture&) = delete;

  private:
    std::uintmax_t originalThreshold_;
};

Buffer FileBufferWith(const std::filesystem::path& path, std::string_view text) {
    {
        std::ofstream seed(path, std::ios::binary);
    }
    Buffer buffer = Buffer::FromFile(path);
    buffer.InsertAt(0, text);
    return buffer;
}

} // namespace

TEST_CASE("ShouldSaveAsynchronously tracks the async-load threshold", "[AsyncSaveDispatch]") {
    const DispatchFixture fixture{8};

    Buffer small("small");
    small.InsertAt(0, "tiny");
    REQUIRE_FALSE(ShouldSaveAsynchronously(small));

    Buffer large("large");
    large.InsertAt(0, "more than eight bytes");
    REQUIRE(ShouldSaveAsynchronously(large));
}

TEST_CASE("A large save goes to the dispatcher and is still in flight when the call returns", "[AsyncSaveDispatch]") {
    const DispatchFixture       fixture{8};
    const std::filesystem::path path = TempPath("inflight.txt");
    std::filesystem::remove(path);

    std::optional<AsyncSaveRequest> captured;
    SetAsyncSaveDispatcher([&captured](AsyncSaveRequest request) {
        captured = std::move(request);
        return true;
    });

    Buffer buffer = FileBufferWith(path, "content large enough to dispatch\n");
    WriteBufferToDisk(buffer, SaveDispatch::Automatic);

    REQUIRE(captured.has_value());
    REQUIRE(captured->bufferName == buffer.Name());
    // Taken, so nothing is on disk yet and the buffer knows it.
    REQUIRE(buffer.IsSaving());
    REQUIRE(ReadFile(path).empty());

    // Now play the background half: write, then settle the buffer.
    ned::text::ExecuteSavePlan(captured->plan);
    buffer.FinishSave(captured->bufferPath, std::move(captured->plan));

    REQUIRE_FALSE(buffer.IsSaving());
    REQUIRE_FALSE(buffer.Modified());
    REQUIRE(ReadFile(path) == "content large enough to dispatch\n");
    std::filesystem::remove(path);
}

TEST_CASE("A dispatcher that refuses falls back to writing synchronously", "[AsyncSaveDispatch]") {
    const DispatchFixture       fixture{8};
    const std::filesystem::path path = TempPath("refused.txt");
    std::filesystem::remove(path);

    bool asked = false;
    SetAsyncSaveDispatcher([&asked](AsyncSaveRequest) {
        asked = true;
        return false;
    });

    Buffer buffer = FileBufferWith(path, "content large enough to dispatch\n");
    WriteBufferToDisk(buffer, SaveDispatch::Automatic);

    REQUIRE(asked);
    REQUIRE_FALSE(buffer.IsSaving());
    REQUIRE_FALSE(buffer.Modified());
    REQUIRE(ReadFile(path) == "content large enough to dispatch\n");
    std::filesystem::remove(path);
}

TEST_CASE("ForceSynchronous never reaches the dispatcher, however large the buffer", "[AsyncSaveDispatch]") {
    const DispatchFixture       fixture{8};
    const std::filesystem::path path = TempPath("forced.txt");
    std::filesystem::remove(path);

    bool asked = false;
    SetAsyncSaveDispatcher([&asked](AsyncSaveRequest) {
        asked = true;
        return true;
    });

    Buffer buffer = FileBufferWith(path, "content large enough to dispatch\n");
    WriteBufferToDisk(buffer); // the default -- what save-some-buffers and the batch paths use

    REQUIRE_FALSE(asked);
    REQUIRE_FALSE(buffer.IsSaving());
    REQUIRE(ReadFile(path) == "content large enough to dispatch\n");
    std::filesystem::remove(path);
}

TEST_CASE("A small save stays synchronous even under Automatic", "[AsyncSaveDispatch]") {
    const DispatchFixture       fixture{1024 * 1024};
    const std::filesystem::path path = TempPath("small.txt");
    std::filesystem::remove(path);

    bool asked = false;
    SetAsyncSaveDispatcher([&asked](AsyncSaveRequest) {
        asked = true;
        return true;
    });

    Buffer buffer = FileBufferWith(path, "small\n");
    WriteBufferToDisk(buffer, SaveDispatch::Automatic);

    REQUIRE_FALSE(asked);
    REQUIRE(ReadFile(path) == "small\n");
    std::filesystem::remove(path);
}

TEST_CASE("A dispatched save reports progress as it writes", "[AsyncSaveDispatch]") {
    const DispatchFixture       fixture{8};
    const std::filesystem::path path = TempPath("progress.txt");
    std::filesystem::remove(path);

    std::optional<AsyncSaveRequest> captured;
    SetAsyncSaveDispatcher([&captured](AsyncSaveRequest request) {
        captured = std::move(request);
        return true;
    });

    // Comfortably past the writer's own 256 KiB flush size, so the write
    // crosses several flush boundaries rather than reporting only once.
    Buffer buffer = FileBufferWith(path, std::string(1024 * 1024, 'x'));
    WriteBufferToDisk(buffer, SaveDispatch::Automatic);
    REQUIRE(captured.has_value());

    REQUIRE(buffer.CurrentSaveProgress() != nullptr);
    REQUIRE(buffer.CurrentSaveProgress()->totalBytes == buffer.Content().ByteLength());
    REQUIRE(buffer.CurrentSaveProgress()->bytesWritten.load() == 0);

    // Count the reports as well as the total: a sink that fires once at
    // the end would satisfy "progress moved" while being useless to a
    // progress bar, which is the whole reason it exists.
    int            reports    = 0;
    std::uintmax_t highest    = 0;
    auto           sink       = std::move(captured->plan.onProgress);
    captured->plan.onProgress = [&](std::uintmax_t bytesWritten) {
        ++reports;
        REQUIRE(bytesWritten >= highest); // monotonic, never restarts
        highest = bytesWritten;
        sink(bytesWritten);
    };

    ned::text::ExecuteSavePlan(captured->plan);

    REQUIRE(reports > 1);
    // Still saving, so the progress is still readable -- and it moved.
    REQUIRE(buffer.CurrentSaveProgress()->bytesWritten.load() > 0);
    REQUIRE(buffer.CurrentSaveProgress()->bytesWritten.load() == highest);

    buffer.FinishSave(captured->bufferPath, std::move(captured->plan));
    REQUIRE(buffer.CurrentSaveProgress() == nullptr);
    std::filesystem::remove(path);
}

TEST_CASE("An edit during a dispatched save survives its completion", "[AsyncSaveDispatch]") {
    const DispatchFixture       fixture{8};
    const std::filesystem::path path = TempPath("editduring.txt");
    std::filesystem::remove(path);

    std::optional<AsyncSaveRequest> captured;
    SetAsyncSaveDispatcher([&captured](AsyncSaveRequest request) {
        captured = std::move(request);
        return true;
    });

    Buffer buffer = FileBufferWith(path, "original content here\n");
    WriteBufferToDisk(buffer, SaveDispatch::Automatic);
    REQUIRE(captured.has_value());

    // The whole reason the save leaves the main thread: this keeps working.
    buffer.InsertAt(0, "TYPED ");

    ned::text::ExecuteSavePlan(captured->plan);
    buffer.FinishSave(captured->bufferPath, std::move(captured->plan));

    REQUIRE(ReadFile(path) == "original content here\n");
    REQUIRE(buffer.Text() == "TYPED original content here\n");
    REQUIRE(buffer.Modified());
    std::filesystem::remove(path);
}
