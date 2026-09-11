#include <catch2/catch_test_macros.hpp>

#include <string>

#include "Text/OffsetRemap.h"
#include "Text/Rope.h"
#include "Text/RopeStorage.h"

using ned::text::ChangedByteRange;
using ned::text::ChangedSpan;
using ned::text::RemapOffset;
using ned::text::RemapOffsetBetween;
using ned::text::Rope;
using ned::text::RopeStorage;
using ned::text::StorageContentEquals;

namespace {

RopeStorage Storage(const std::string& text) {
    return RopeStorage{Rope(text)};
}

} // namespace

TEST_CASE("Identical content reports no changed range at all", "[OffsetRemap]") {
    const RopeStorage a = Storage("int alpha = 1;\n");
    const RopeStorage b = Storage("int alpha = 1;\n");
    REQUIRE(StorageContentEquals(a, b));
    REQUIRE_FALSE(ChangedByteRange(a, b).has_value());
    // Nothing changed, so nothing moves -- including the end-of-document
    // offset, which is the one most likely to fall off an arithmetic path.
    REQUIRE(RemapOffsetBetween(0, a, b) == 0);
    REQUIRE(RemapOffsetBetween(a.ByteLength(), a, b) == a.ByteLength());
}

TEST_CASE("An insertion shifts only the offsets after it", "[OffsetRemap]") {
    const RopeStorage before = Storage("int alpha = 1;");
    const RopeStorage after  = Storage("yyint alpha = 1;"); // two characters at the very start

    const auto span = ChangedByteRange(before, after);
    REQUIRE(span.has_value());

    // "alpha" starts at 4 before and at 6 after.
    REQUIRE(RemapOffset(4, *span) == 6);
    REQUIRE(RemapOffset(9, *span) == 11);
    // An offset at the insertion point itself stays put: the new bytes were
    // not part of whatever the caller was pointing at.
    REQUIRE(RemapOffset(0, *span) == 0);
}

TEST_CASE("A deletion pulls back the offsets after it", "[OffsetRemap]") {
    const RopeStorage before = Storage("int alpha = 1;");
    const RopeStorage after  = Storage("alpha = 1;"); // leading "int " removed

    const auto span = ChangedByteRange(before, after);
    REQUIRE(span.has_value());
    REQUIRE(RemapOffset(4, *span) == 0);   // "alpha" moved to the front
    REQUIRE(RemapOffset(9, *span) == 5);   // the space after it
    REQUIRE(RemapOffset(14, *span) == 10); // end of document
}

TEST_CASE("An offset inside the replaced region collapses to the new span's start", "[OffsetRemap]") {
    // "alpha" -> "b": anything that pointed into the old word has nowhere
    // honest to go, so it lands at the start of what replaced it rather than
    // at an invented position inside unrelated text.
    const RopeStorage before = Storage("int alpha = 1;");
    const RopeStorage after  = Storage("int b = 1;");

    const auto span = ChangedByteRange(before, after);
    REQUIRE(span.has_value());
    REQUIRE(RemapOffset(5, *span) == span->newStart);
    REQUIRE(RemapOffset(8, *span) == span->newStart);
    // Either edge is outside the replaced region and relocates exactly.
    REQUIRE(RemapOffset(4, *span) == 4);
    REQUIRE(RemapOffset(9, *span) == span->newEnd);
}

TEST_CASE("A middle-of-document edit leaves both ends alone", "[OffsetRemap]") {
    const RopeStorage before = Storage("alpha\nbeta\ngamma\n");
    const RopeStorage after  = Storage("alpha\nbetaXX\ngamma\n");

    const auto span = ChangedByteRange(before, after);
    REQUIRE(span.has_value());
    REQUIRE(RemapOffset(0, *span) == 0);   // start of "alpha"
    REQUIRE(RemapOffset(11, *span) == 13); // start of "gamma"
    REQUIRE(RemapOffset(before.ByteLength(), *span) == after.ByteLength());
}

TEST_CASE("Remapping is exact for an edit at the very end of the document", "[OffsetRemap]") {
    // The common-suffix walk has nothing to work with here, which is the
    // degenerate case the prefix/suffix model is most likely to get wrong.
    const RopeStorage before = Storage("alpha beta");
    const RopeStorage after  = Storage("alpha beta gamma");

    REQUIRE(RemapOffsetBetween(0, before, after) == 0);
    REQUIRE(RemapOffsetBetween(6, before, after) == 6); // "beta" is untouched
}

TEST_CASE("Two distant edits are reported as one span, and that is documented rather than exact",
          "[OffsetRemap]") {
    // The model is one contiguous changed region, so an offset *between* two
    // separate edits relocates as if it sat inside the change. Pinned so the
    // limit is a decision on record rather than a surprise: a caller needing
    // better has to compare versions more often.
    const RopeStorage before = Storage("aaa MIDDLE zzz");
    const RopeStorage after  = Storage("bbb MIDDLE yyy");

    const auto span = ChangedByteRange(before, after);
    REQUIRE(span.has_value());
    REQUIRE(span->oldStart == 0);
    REQUIRE(span->oldEnd == before.ByteLength());
    REQUIRE(RemapOffset(4, *span) == span->newStart); // "MIDDLE" collapses, as documented
}
