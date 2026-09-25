#include <catch2/catch_test_macros.hpp>

#include <string>

#include "Text/DisplayWidth.h"
#include "Text/Rope.h"
#include "Text/RopeStorage.h"

using ned::text::Glyph;
using ned::text::GlyphAt;
using ned::text::GlyphText;
using ned::text::PlaceholderText;
using ned::text::PrefixBytesForColumns;
using ned::text::Rope;
using ned::text::RopeStorage;
using ned::text::StringColumns;

TEST_CASE("ASCII glyphs are one byte and one column", "[DisplayWidth]") {
    const Glyph glyph = GlyphAt(std::string_view("ab"), 0);
    CHECK(glyph.byteLength == 1);
    CHECK(glyph.columns == 1);
    CHECK_FALSE(glyph.placeholder);
}

TEST_CASE("CJK and emoji are two columns wide", "[DisplayWidth]") {
    CHECK(GlyphAt(std::string_view("漢"), 0).columns == 2);
    CHECK(GlyphAt(std::string_view("😀"), 0).columns == 2);
    CHECK(GlyphAt(std::string_view("　"), 0).columns == 2); // ideographic space
    CHECK(GlyphAt(std::string_view("ｱ"), 0).columns == 1);  // halfwidth katakana
    CHECK(StringColumns("abc 漢字テスト end") == 4 + 10 + 4);
}

TEST_CASE("A cluster is one glyph: combining marks, ZWJ sequences, skin tones, flags", "[DisplayWidth]") {
    const std::string combining = "éx";
    const Glyph       accented  = GlyphAt(std::string_view(combining), 0);
    CHECK(accented.byteLength == 3);
    CHECK(accented.columns == 1);
    CHECK_FALSE(accented.placeholder);

    const std::string family = "\U0001F468‍\U0001F469‍\U0001F467";
    CHECK(GlyphAt(std::string_view(family), 0).byteLength == family.size());
    CHECK(StringColumns(family) == 2);

    CHECK(StringColumns("\U0001F44D\U0001F3FD") == 2);
    CHECK(GlyphAt(std::string_view("\U0001F1FA\U0001F1F8"), 0).byteLength == 8);
}

TEST_CASE("The storage and string forms segment identically", "[DisplayWidth]") {
    const std::string text = "a漢é\U0001F44D\U0001F3FD\tz";
    const RopeStorage rope{Rope(text)};
    for (std::size_t offset = 0; offset < text.size();) {
        const Glyph fromString  = GlyphAt(std::string_view(text), offset);
        const Glyph fromStorage = GlyphAt(rope, offset, text.size());
        REQUIRE(fromString.byteLength == fromStorage.byteLength);
        CHECK(fromString.columns == fromStorage.columns);
        CHECK(GlyphText(std::string_view(text), offset, fromString) == GlyphText(rope, offset, fromStorage));
        offset += fromString.byteLength;
    }
}

TEST_CASE("A cluster never extends past the range's end", "[DisplayWidth]") {
    const RopeStorage rope{Rope("é")};
    CHECK(GlyphAt(rope, 0, 1).byteLength == 1);
}

TEST_CASE("Controls and zero-width characters become hex placeholders", "[DisplayWidth]") {
    const Glyph escape = GlyphAt(std::string_view("\x1b"), 0);
    CHECK(escape.placeholder);
    CHECK(escape.columns == 4);
    CHECK(GlyphText(std::string_view("\x1b"), 0, escape) == "◁1B▷");

    CHECK(GlyphAt(std::string_view("\u009B"), 0).placeholder); // C1 CSI
    CHECK(GlyphAt(std::string_view("\x7f"), 0).placeholder);

    const Glyph rlo = GlyphAt(std::string_view("‮"), 0); // Trojan Source's bidi override
    CHECK(rlo.placeholder);
    CHECK(rlo.columns == 6);
    CHECK(PlaceholderText(0x202E) == "◁202E▷");

    CHECK(GlyphAt(std::string_view("​"), 0).placeholder);
    CHECK(GlyphAt(std::string_view("﻿"), 0).placeholder);
    CHECK(GlyphAt(std::string_view("́"), 0).placeholder); // a combining mark with no base
    CHECK(PlaceholderText(0xE0001) == "◁0E0001▷");
}

TEST_CASE("Private-use glyphs (Nerd Font icons) print as themselves", "[DisplayWidth]") {
    const Glyph icon = GlyphAt(std::string_view(""), 0);
    CHECK_FALSE(icon.placeholder);
    CHECK(icon.columns == 1);
}

TEST_CASE("A tab runs to the next tab stop and is its own glyph", "[DisplayWidth]") {
    CHECK(StringColumns("\t", 0, 4) == 4);
    CHECK(StringColumns("ab\t", 0, 4) == 4);
    CHECK(StringColumns("\t", 3, 4) == 1);
    CHECK(GlyphAt(std::string_view("\t́"), 0).byteLength == 1);
}

TEST_CASE("Malformed bytes are one column and re-encode as U+FFFD", "[DisplayWidth]") {
    const std::string text  = "\xff";
    const Glyph       glyph = GlyphAt(std::string_view(text), 0);
    CHECK(glyph.columns == 1);
    CHECK(GlyphText(std::string_view(text), 0, glyph) == "�");
}

TEST_CASE("A prefix never splits a wide glyph", "[DisplayWidth]") {
    CHECK(PrefixBytesForColumns("漢字", 3) == 3);
    CHECK(PrefixBytesForColumns("漢字", 4) == 6);
    CHECK(PrefixBytesForColumns("a漢", 1) == 1);
    CHECK(PrefixBytesForColumns("ab", 5) == 2);
}
