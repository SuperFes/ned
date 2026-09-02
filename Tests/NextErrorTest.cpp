#include <catch2/catch_test_macros.hpp>

#include <string>

#include "Editor/Multibuffer.h"
#include "Editor/NextError.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"

using ned::editor::ClearLastResultsBufferForTesting;
using ned::editor::CollectResultLocations;
using ned::editor::LastResultsBuffer;
using ned::editor::SetLastResultsBuffer;
using ned::editor::multibuffer::BuildMultibuffer;
using ned::editor::multibuffer::ClearRegistryForTesting;
using ned::editor::multibuffer::ExcerptSource;
using ned::text::Buffer;
using ned::text::BufferList;

namespace {

// Both process-wide statics this test touches (Multibuffer.h's own registry
// and NextError.h's "last results buffer" name) need the same reset-around-
// each-TEST_CASE treatment MultibufferTest.cpp's own RegistryResetGuard
// documents -- a stale entry from one TEST_CASE can otherwise leak into the
// next.
struct ResetGuard {
    ResetGuard() {
        ClearRegistryForTesting();
        ClearLastResultsBufferForTesting();
    }
    ~ResetGuard() {
        ClearRegistryForTesting();
        ClearLastResultsBufferForTesting();
    }
};

} // namespace

TEST_CASE("LastResultsBuffer starts unset and round-trips through SetLastResultsBuffer", "[NextError]") {
    ResetGuard guard;
    REQUIRE_FALSE(LastResultsBuffer().has_value());

    SetLastResultsBuffer("*search results*");
    REQUIRE(LastResultsBuffer() == "*search results*");

    // A later builder overwrites -- "last" means last, not first.
    SetLastResultsBuffer("*vcs status*");
    REQUIRE(LastResultsBuffer() == "*vcs status*");
}

TEST_CASE("CollectResultLocations parses flat \"path:line:\" results lines in document order", "[NextError]") {
    ResetGuard guard;
    Buffer     buffer("results");
    buffer.InsertAtPoint("alpha.txt:3: first match\n"
                          "beta.txt:9: second match\n"
                          "not a results line at all\n"
                          "gamma.txt:1: third match\n");

    const std::vector<ned::editor::ErrorLocation> locations = CollectResultLocations(buffer);
    REQUIRE(locations.size() == 3);
    CHECK(locations[0].sourcePath == "alpha.txt");
    CHECK(locations[0].sourceLine == 3);
    CHECK(locations[1].sourcePath == "beta.txt");
    CHECK(locations[1].sourceLine == 9);
    CHECK(locations[2].sourcePath == "gamma.txt");
    CHECK(locations[2].sourceLine == 1);
    // Ascending resultBufferOffset -- the same order the lines appear in.
    REQUIRE(locations[0].resultBufferOffset < locations[1].resultBufferOffset);
    REQUIRE(locations[1].resultBufferOffset < locations[2].resultBufferOffset);
}

TEST_CASE("CollectResultLocations returns nothing for a buffer with no results-shaped lines", "[NextError]") {
    ResetGuard guard;
    Buffer     buffer("log");
    buffer.InsertAtPoint("just some prose\nwith no locations in it\n");

    REQUIRE(CollectResultLocations(buffer).empty());
}

TEST_CASE("CollectResultLocations skips a malformed line number rather than aborting the scan", "[NextError]") {
    ResetGuard guard;
    Buffer     buffer("results");
    // A line number too large for std::stoul's unsigned long range --
    // CollectResultLocations must skip it, not throw out of the whole scan.
    buffer.InsertAtPoint("alpha.txt:999999999999999999999: overflow\n"
                          "beta.txt:5: a real match\n");

    const std::vector<ned::editor::ErrorLocation> locations = CollectResultLocations(buffer);
    REQUIRE(locations.size() == 1);
    CHECK(locations[0].sourcePath == "beta.txt");
    CHECK(locations[0].sourceLine == 5);
}

TEST_CASE("CollectResultLocations prefers the MultibufferIndex when one is registered", "[NextError]") {
    ResetGuard guard;
    BufferList bufferList;

    std::vector<ExcerptSource> excerpts;
    excerpts.push_back(ExcerptSource{"/repo/a.cpp", 10, 10, "a.cpp:10", "line ten\n"});
    excerpts.push_back(ExcerptSource{"/repo/b.cpp", 20, 20, "b.cpp:20", "line twenty\n"});
    // sourceStartLine == 0 -- "no single source line applies," must be
    // skipped exactly like ExcerptSpan's own doc comment says.
    excerpts.push_back(ExcerptSource{"/repo/c.cpp", 0, 0, "c.cpp: no line", "context only\n"});

    Buffer& multibuffer = BuildMultibuffer(bufferList, "*test multibuffer*", excerpts);

    const std::vector<ned::editor::ErrorLocation> locations = CollectResultLocations(multibuffer);
    REQUIRE(locations.size() == 2);
    CHECK(locations[0].sourcePath == "/repo/a.cpp");
    CHECK(locations[0].sourceLine == 10);
    CHECK(locations[1].sourcePath == "/repo/b.cpp");
    CHECK(locations[1].sourceLine == 20);
    REQUIRE(locations[0].resultBufferOffset < locations[1].resultBufferOffset);
}
