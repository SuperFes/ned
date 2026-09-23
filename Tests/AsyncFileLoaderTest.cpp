// UI/AsyncFileLoader, UI/HugeFileLoader -- what happens to the placeholder
// when the background load never gets off the ground.
//
// The placeholder is the buffer the user is looking at: BufferList::OpenFile
// creates it synchronously, the caller makes it current, and only then does
// the loader thread find out the file cannot be read (permissions, a file
// deleted between the main thread's stat and the loader's open, an I/O error
// on a network mount). Retiring it therefore has to tell whoever is showing
// it first -- closing it out from under a pane leaves that pane's
// ActiveBuffer pointing at freed memory, which is a crash nobody can
// reproduce on purpose.
//
// The thread is real, but nothing waits on wall-clock time: the loaders
// marshal completion through EventLoop::Post, so draining until Done() is
// deterministic.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <unistd.h>

#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "UI/AsyncFileLoader.h"
#include "UI/EventLoop.h"
#include "UI/HugeFileLoader.h"

namespace {

// A path that cannot be opened, which is every real load failure's shape
// without needing to arrange a permissions trick or a mid-read unmount.
std::filesystem::path UnreadablePath() {
    return std::filesystem::temp_directory_path() / ("ned_loader_missing_" + std::to_string(::getpid())) / "absent.txt";
}

template <typename Loader>
void PumpUntilDone(ned::ui::EventLoop& eventLoop, const Loader& loader) {
    while (!loader.Done()) {
        eventLoop.DrainPosted_();
    }
}

} // namespace

TEST_CASE("A failed async load retires its placeholder, announcing it while it is still alive", "[AsyncFileLoader]") {
    ned::text::BufferList bufferList;
    ned::ui::EventLoop    eventLoop;

    ned::text::Buffer& placeholder = bufferList.CreateBuffer("absent.txt");
    const std::string  name        = placeholder.Name();

    bool announced         = false;
    bool aliveWhenTold     = false;
    bool wasThePlaceholder = false;
    {
        ned::ui::AsyncFileLoader loader(placeholder, bufferList, UnreadablePath(), eventLoop,
                                        [&](ned::text::Buffer& closing) {
                                            announced         = true;
                                            wasThePlaceholder = (closing.Name() == name);
                                            // The whole point: still in the list, so a pane
                                            // told now can still be pointed elsewhere.
                                            aliveWhenTold = (bufferList.Find(name) != nullptr);
                                        });
        PumpUntilDone(eventLoop, loader);
    }

    CHECK(announced);
    CHECK(wasThePlaceholder);
    CHECK(aliveWhenTold);
    CHECK(bufferList.Find(name) == nullptr); // and then actually retired
}

TEST_CASE("A failed huge-file load retires its placeholder the same way", "[AsyncFileLoader]") {
    ned::text::BufferList bufferList;
    ned::ui::EventLoop    eventLoop;

    ned::text::Buffer& placeholder = bufferList.CreateBuffer("absent-huge.txt");
    const std::string  name        = placeholder.Name();

    bool aliveWhenTold = false;
    {
        ned::ui::HugeFileLoader loader(placeholder, bufferList, UnreadablePath(), /*allowBinary=*/false, eventLoop,
                                       [&](ned::text::Buffer&) { aliveWhenTold = (bufferList.Find(name) != nullptr); });
        PumpUntilDone(eventLoop, loader);
    }

    CHECK(aliveWhenTold);
    CHECK(bufferList.Find(name) == nullptr);
}

// A placeholder the user closed by hand while the doomed load was still
// running is simply gone by the time the failure posts -- the loader must
// not announce (or close) a buffer that is no longer there.
TEST_CASE("A failed async load says nothing about a placeholder already closed by hand", "[AsyncFileLoader]") {
    ned::text::BufferList bufferList;
    ned::ui::EventLoop    eventLoop;

    ned::text::Buffer& placeholder = bufferList.CreateBuffer("absent.txt");
    const std::string  name        = placeholder.Name();

    bool announced = false;
    {
        ned::ui::AsyncFileLoader loader(placeholder, bufferList, UnreadablePath(), eventLoop,
                                        [&](ned::text::Buffer&) { announced = true; });
        bufferList.Close(name);
        PumpUntilDone(eventLoop, loader);
    }

    CHECK_FALSE(announced);
}
