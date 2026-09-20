// UI/AsyncFileSaver -- what happens when the background write fails.
//
// The failure paths are the ones worth holding still, because they are the
// ones nobody sees by accident: a save that fails is a file that is not
// there, and the user may well have stopped looking. There are two
// independent channels for that, and this pins both -- the callback that
// puts it on the status line while the editor is up, and the exit report
// that survives the editor being gone.
//
// The thread is real, but nothing here waits on wall-clock time: the saver
// marshals its completion through EventLoop::Post, so pumping DrainPosted_
// until it reports done is deterministic.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <unistd.h>

#include "Editor/BufferSave.h"
#include "Editor/ExitReport.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/RopeStorage.h"
#include "UI/AsyncFileSaver.h"
#include "UI/EventLoop.h"

using ned::editor::AsyncSaveRequest;
using ned::editor::TakeExitReports;
using ned::ui::AsyncFileSaver;

namespace {

std::filesystem::path TempPath(const std::string& name) {
    return std::filesystem::temp_directory_path() / ("ned_async_saver_" + name + "_" + std::to_string(::getpid()));
}

// A plan aimed at a directory that does not exist, so ExecuteSavePlan throws
// on the saver's own thread -- the shape of every real write failure
// (unwritable target, full disk) without needing to arrange one.
AsyncSaveRequest FailingRequest(const std::string& bufferName) {
    const std::filesystem::path target = TempPath("nodir") / "nested" / "file.txt";

    AsyncSaveRequest request;
    request.bufferName      = bufferName;
    request.bufferPath      = target;
    request.plan.target     = target;
    request.plan.snapshot   = std::make_unique<ned::text::RopeStorage>(ned::text::Rope("content"));
    request.plan.attributes = ned::text::CaptureFileAttributes(target);
    request.progress        = std::make_shared<ned::text::SaveProgress>();
    return request;
}

// Runs the loop's posted work until the saver reports itself finished. The
// saver only ever completes by posting, so this terminates as soon as the
// write does.
void PumpUntilDone(ned::ui::EventLoop& eventLoop, const AsyncFileSaver& saver) {
    while (!saver.Done()) {
        eventLoop.DrainPosted_();
    }
}

struct DrainedReports {
    DrainedReports() {
        TakeExitReports();
    }
    ~DrainedReports() {
        TakeExitReports();
    }
    DrainedReports(const DrainedReports&)            = delete;
    DrainedReports& operator=(const DrainedReports&) = delete;
};

} // namespace

TEST_CASE("A failed background save reports through its callback and abandons the buffer", "[AsyncFileSaver]") {
    const DrainedReports drained;

    ned::text::BufferList bufferList;
    ned::ui::EventLoop    eventLoop;

    ned::text::Buffer& buffer = bufferList.CreateBuffer("doomed.txt");
    buffer.InsertAt(0, "content");
    ned::text::SavePlan armed = buffer.BeginSave(TempPath("armed.txt"), false, false);
    REQUIRE(buffer.IsSaving());

    std::string reported;
    {
        AsyncFileSaver saver(FailingRequest(buffer.Name()), bufferList, eventLoop,
                             [&reported](std::string message) { reported = std::move(message); });
        PumpUntilDone(eventLoop, saver);
    }

    // Said out loud rather than left for a poll that never happens.
    REQUIRE_FALSE(reported.empty());

    // And the buffer is released, not stuck mid-save, with its edit intact.
    REQUIRE_FALSE(buffer.IsSaving());
    REQUIRE(buffer.Modified());
}

TEST_CASE("A failed background save queues an exit report that outlives the UI", "[AsyncFileSaver]") {
    const DrainedReports drained;

    ned::text::BufferList bufferList;
    ned::ui::EventLoop    eventLoop;

    ned::text::Buffer& buffer = bufferList.CreateBuffer("doomed.txt");
    buffer.InsertAt(0, "content");
    ned::text::SavePlan armed = buffer.BeginSave(TempPath("armed2.txt"), false, false);

    {
        AsyncFileSaver saver(FailingRequest(buffer.Name()), bufferList, eventLoop, nullptr);
        PumpUntilDone(eventLoop, saver);
    }

    const std::vector<std::string> reports = TakeExitReports();
    REQUIRE(reports.size() == 1);
    REQUIRE(reports.front().find("doomed.txt") != std::string::npos);
}

TEST_CASE("A save whose buffer was closed mid-write still reports on exit", "[AsyncFileSaver]") {
    const DrainedReports drained;

    ned::text::BufferList bufferList;
    ned::ui::EventLoop    eventLoop;

    ned::text::Buffer& buffer = bufferList.CreateBuffer("closed.txt");
    buffer.InsertAt(0, "content");
    ned::text::SavePlan armed = buffer.BeginSave(TempPath("armed3.txt"), false, false);

    {
        std::string    reported;
        AsyncFileSaver saver(FailingRequest("closed.txt"), bufferList, eventLoop,
                             [&reported](std::string message) { reported = std::move(message); });

        // Gone before the write ends: this is the case with no mode line
        // left to say anything in, which is the whole reason the exit report
        // exists.
        bufferList.Close("closed.txt");
        PumpUntilDone(eventLoop, saver);
    }

    const std::vector<std::string> reports = TakeExitReports();
    REQUIRE(reports.size() == 1);
    REQUIRE(reports.front().find("closed.txt") != std::string::npos);
}
