#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <thread>
#include <vector>

#include <poll.h>

#include "Editor/PostQueue.h"

using ned::editor::PostQueue;

namespace {

bool WakeFdReadable(const PostQueue& queue) {
    pollfd fd{queue.WakeFd(), POLLIN, 0};
    return poll(&fd, 1, 0) == 1 && (fd.revents & POLLIN) != 0;
}

} // namespace

TEST_CASE("PostQueue runs posted work in order on the draining thread", "[PostQueue]") {
    PostQueue        queue;
    std::vector<int> seen;

    queue.Post([&seen] { seen.push_back(1); });
    queue.Post([&seen] { seen.push_back(2); });

    REQUIRE(seen.empty());
    REQUIRE(queue.DrainPosted_());
    REQUIRE(seen == std::vector<int>{1, 2});
    REQUIRE_FALSE(queue.DrainPosted_());
}

TEST_CASE("PostQueue's wake fd signals a post from another thread", "[PostQueue]") {
    PostQueue queue;
    REQUIRE_FALSE(WakeFdReadable(queue));

    bool ran = false;
    std::jthread([&queue, &ran] { queue.Post([&ran] { ran = true; }); }).join();

    REQUIRE(WakeFdReadable(queue));
    queue.DiscardWakeups();
    REQUIRE_FALSE(WakeFdReadable(queue));

    REQUIRE(queue.DrainPosted_());
    REQUIRE(ran);
}

TEST_CASE("PostQueue's Wake signals without queueing work", "[PostQueue]") {
    PostQueue queue;
    queue.Wake();

    REQUIRE(WakeFdReadable(queue));
    REQUIRE_FALSE(queue.DrainPosted_());
}

TEST_CASE("PostQueue defers work posted by a running callback to the next drain", "[PostQueue]") {
    PostQueue queue;
    int       inner = 0;

    queue.Post([&queue, &inner] { queue.Post([&inner] { ++inner; }); });

    REQUIRE(queue.DrainPosted_());
    REQUIRE(inner == 0);
    REQUIRE(queue.DrainPosted_());
    REQUIRE(inner == 1);
}

TEST_CASE("PostQueue keeps draining past a callback that throws", "[PostQueue]") {
    PostQueue queue;
    bool      after = false;

    queue.Post([] { throw std::runtime_error("boom"); });
    queue.Post([&after] { after = true; });

    REQUIRE(queue.DrainPosted_());
    REQUIRE(after);
}
