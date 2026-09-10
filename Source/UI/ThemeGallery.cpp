#include "ThemeGallery.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "Border.h"
#include "Compositing.h"
#include "KeyTranslation.h"
#include "Paint.h"
#include "PaintParse.h"
#include "Text/Utf8.h"
#include "ThemePaints.h"

namespace ned::ui {

namespace {

    bool IsQuit(const editor::KeyChord& chord) {
        return chord.Special == editor::SpecialKey::Escape || (chord.Control && chord.Codepoint == U'g');
    }

    // TreeView.cpp's own PaintRowText, duplicated rather than shared for the
    // reason its own comment gives -- small enough, and this codebase
    // duplicates a helper this size rather than growing a shared dependency
    // for it. Writes one codepoint per cell and stops at `xEnd`.
    int PaintText(Canvas& c, int x, int xEnd, int row, std::string_view text, const Brush& brush) {
        std::size_t       pos = 0;
        const std::string body(text);
        while (pos < body.size() && x < xEnd) {
            const std::size_t next = text::NextCodepointBoundary(body, pos);
            Cell&             cell = c[{.x = x, .y = row}];
            cell.character         = body.substr(pos, next - pos);
            brush.ApplyTo(cell);
            ++x;
            pos = next;
        }
        return x;
    }

    // Deliberately varied: letters with ascenders and descenders, digits,
    // and punctuation, because a fill that reads fine behind "AAA" can still
    // lose a comma.
    constexpr std::string_view kSample = "Agj Qy 0123 (){} .,;:";

    std::string FormatRatio(double ratio) {
        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "%.1f", ratio);
        return buffer;
    }

} // namespace

ThemeGallery::ThemeGallery(const Theme& theme) : theme_(theme) {
}

void ThemeGallery::SetOnCancel(std::function<void()> onCancel) {
    onCancel_ = std::move(onCancel);
}

void ThemeGallery::ScrollToTop() {
    scrollEntry_ = 0;
}

std::vector<ThemeGallery::Entry> ThemeGallery::Entries() const {
    std::vector<Entry> entries;
    for (const std::string& name : SurfaceNames()) {
        entries.push_back(Entry{.kind       = Entry::Kind::Surface,
                                .name       = name,
                                .overridden = SurfaceOverride(name).has_value()});
    }
    for (const std::string& name : NamedPaintNames()) {
        entries.push_back(Entry{.kind = Entry::Kind::Paint, .name = name, .overridden = true});
    }
    return entries;
}

int ThemeGallery::VisibleEntryCount(int interiorHeight) const {
    // One row is spent on the footer legend, so the entries get what is
    // left. A panel too short for even one entry reports zero rather than a
    // negative count.
    return std::max(0, (interiorHeight - 1) / kEntryHeight);
}

void ThemeGallery::ClampScroll(int interiorHeight, std::size_t entryCount) {
    const int visible = VisibleEntryCount(interiorHeight);
    if (visible <= 0 || entryCount <= static_cast<std::size_t>(visible)) {
        scrollEntry_ = 0;
        return;
    }
    scrollEntry_ = std::min(scrollEntry_, entryCount - static_cast<std::size_t>(visible));
}

std::optional<double> ThemeGallery::PaintEntry(Canvas& interior, const Entry& entry, int row, int interiorWidth) {
    const int top       = row * kEntryHeight;
    const int swatchX   = std::min(kLabelWidth, interiorWidth);
    const int swatchEnd = interiorWidth; // exclusive

    // --- the label column, on the panel's own background ------------------
    const Brush       nameBrush{.background = theme_.background, .foreground = theme_.defaultForeground};
    const std::string label = entry.kind == Entry::Kind::Paint ? "$" + entry.name
                                                               : (entry.overridden ? "* " : "  ") + entry.name;
    PaintText(interior, 0, std::max(0, swatchX - 1), top, label, nameBrush);

    if (swatchEnd - swatchX < 4) {
        return std::nullopt; // no room for a swatch worth measuring
    }

    // --- the swatch -------------------------------------------------------
    // A band Canvas of its own, so a gradient's own parameters run across
    // exactly this entry's box (Fill normalises u/v against the box it is
    // given) and so a Fade text paint modulates only these cells.
    const Point origin = interior.Origin();
    const Box   band{.x_min = origin.x + swatchX,
                     .x_max = origin.x + swatchEnd - 1,
                     .y_min = origin.y + top,
                     .y_max = origin.y + top + kSwatchHeight - 1};
    Canvas      swatch = interior.ForBox(band);

    // ui::Paint has to be written out in full inside a Widget subclass:
    // Widget::Paint is a member function, so the unqualified name resolves
    // to it and hides the type. Qualified lookup skips class members.
    const bool     isSurface = entry.kind == Entry::Kind::Surface;
    const Surface  surface   = isSurface ? SurfaceFor(theme_, entry.name) : Surface{};
    ned::ui::Paint solo;
    if (!isSurface) {
        solo = NamedPaint(entry.name).value_or(ned::ui::Paint{});
    }
    const ned::ui::Paint& fill = isSurface ? surface.fill : solo;

    // Cells persist between frames, so a paint that legitimately covers
    // nothing has to have something underneath it to show -- ThemePaints'
    // own ClearCanvas rule, and the reason a translucent fill composites
    // instead of falling down the dither path on an opaque theme.
    ClearCanvas(swatch, ChromeBackdrop(theme_));
    Fill(swatch, fill);

    const int bandWidth = swatchEnd - swatchX;
    if (isSurface && PaintsColour(surface.border)) {
        // A one-cell frame down each edge: enough to see the border paint's
        // own colour without turning a two-row band into a box.
        Fill(swatch, Box{.x_min = 0, .x_max = 0, .y_min = 0, .y_max = kSwatchHeight - 1}, surface.border);
        Fill(swatch, Box{.x_min = bandWidth - 1, .x_max = bandWidth - 1, .y_min = 0, .y_max = kSwatchHeight - 1},
             surface.border);
    }

    // Sample text on the swatch's first row only -- the second stays clean,
    // so a gradient can be read as a gradient rather than through glyphs.
    const int textStart = 2;
    const int textEnd   = std::max(textStart, bandWidth - 2);
    for (int x = textStart, pos = 0; x < textEnd && pos < static_cast<int>(kSample.size()); ++x, ++pos) {
        const Color colour    = isSurface ? TextColourAt(surface, swatch, {.x = x, .y = 0}, theme_.defaultForeground)
                                          : theme_.defaultForeground;
        Cell&       cell      = swatch[{.x = x, .y = 0}];
        cell.character        = std::string(1, kSample[static_cast<std::size_t>(pos)]);
        cell.foreground_color = colour;
    }

    // A Fade text paint takes some of the glyphs away *after* they are
    // down, which is exactly why it runs here and not before the loop.
    if (isSurface) {
        ApplyTextFade(swatch, surface);
    }

    // --- measure what actually landed ------------------------------------
    // Read back rather than recompute: a translucent fill, a dithered one,
    // or a Fade all resolve to something the theme's own fields do not say,
    // and the pair a reader's eye has to separate is the composited one.
    std::optional<double> worst;
    for (int x = textStart; x < textEnd; ++x) {
        const Cell& cell = swatch[{.x = x, .y = 0}];
        if (IsBlankGlyph(cell.character)) {
            continue;
        }
        const double ratio = ContrastRatio(cell.foreground_color, cell.background_color);
        worst              = worst ? std::min(*worst, ratio) : ratio;
    }
    return worst;
}

void ThemeGallery::Paint(Canvas c) {
    const int width  = c.size().width;
    const int height = c.size().height;
    if (width <= 2 || height <= 2) {
        return;
    }

    // Translucency phase 7: the same "popup" Surface every other overlay
    // body uses -- which also means this panel shows its own `popup` row
    // being applied to itself, the most direct feedback the gallery can give.
    const Brush   interiorBrush{.background = theme_.background, .foreground = theme_.defaultForeground};
    const Surface surface = SurfaceFor(theme_, "popup");
    const bool    fillReadsDestination =
        surface.fill.kind == PaintKind::Blur || surface.fill.kind == PaintKind::Stack;
    for (int y = 1; y < height - 1; ++y) {
        for (int x = 1; x < width - 1; ++x) {
            Cell& cell            = c[{.x = x, .y = y}];
            cell.character        = " ";
            interiorBrush.ApplyTextTo(cell);
            if (!fillReadsDestination) {
                cell.background_color = interiorBrush.background;
            }
        }
    }

    const int   interiorWidth  = width - 2;
    const int   interiorHeight = height - 2;
    const Point origin         = c.Origin();
    Canvas      interior       = c.ForBox(Box{.x_min = origin.x + 1,
                                              .x_max = origin.x + width - 2,
                                              .y_min = origin.y + 1,
                                              .y_max = origin.y + height - 2});

    Fill(interior, surface.fill);
    DrawBorder(c, theme_.border);
    RecolourBorder(c, surface.border);
    DrawBorderTitle(c, "Theme gallery", theme_.borderAccent);

    const std::vector<Entry> entries = Entries();
    ClampScroll(interiorHeight, entries.size());

    const int visible = VisibleEntryCount(interiorHeight);
    for (int row = 0; row < visible; ++row) {
        const std::size_t index = scrollEntry_ + static_cast<std::size_t>(row);
        if (index >= entries.size()) {
            break;
        }
        const std::optional<double> contrast = PaintEntry(interior, entries[index], row, interiorWidth);
        if (!contrast) {
            continue;
        }
        // The readout sits under the name, in the label column, so it is
        // legible whatever the swatch turned out to be.
        const bool        poor = *contrast < kContrastFloor;
        const Brush       brush{.background = theme_.background,
                                .foreground = poor ? theme_.diagnosticWarning : theme_.lineNumberForeground};
        const std::string text = FormatRatio(*contrast) + (poor ? " ! low contrast" : "");
        PaintText(interior, 2, std::max(0, std::min(kLabelWidth, interiorWidth) - 1),
                  row * kEntryHeight + 1, text, brush);
    }

    // The footer earns its row: without it the panel looks like a static
    // picture rather than something with more below the fold.
    const int         footerRow = interiorHeight - 1;
    const std::size_t shown     = std::min(entries.size(), scrollEntry_ + static_cast<std::size_t>(std::max(0, visible)));
    const std::string footer    = std::to_string(entries.size() == 0 ? 0 : scrollEntry_ + 1) + "-" +
                                  std::to_string(shown) + " of " + std::to_string(entries.size()) +
                                  "   * = set by theme    up/down scroll    Esc close";
    PaintText(interior, 0, interiorWidth, footerRow, footer,
              Brush{.background = theme_.background, .foreground = theme_.lineNumberForeground});
}

bool ThemeGallery::OnEvent(const Event& event) {
    if (event.is_mouse()) {
        const std::optional<MouseEvent> mouse = LocalMouseEvent(event);
        if (!mouse) {
            return false; // outside our own bounds -- not ours to consume
        }
        if (mouse->button == MouseEvent::Button::WheelUp && scrollEntry_ > 0) {
            --scrollEntry_;
        }
        else if (mouse->button == MouseEvent::Button::WheelDown) {
            ++scrollEntry_; // Paint's own ClampScroll is what bounds this
        }
        return true;
    }
    if (!Focused()) {
        return false;
    }
    const auto chord = TranslateKey(event);
    if (!chord) {
        return true; // focused: swallow undecodable input rather than leaking it
    }
    if (IsQuit(*chord)) {
        if (onCancel_) {
            onCancel_();
        }
        return true;
    }

    const int  visible = std::max(1, VisibleEntryCount(std::max(0, size().height - 2)));
    const bool up      = chord->Special == editor::SpecialKey::Up || (chord->Control && chord->Codepoint == U'p');
    const bool down    = chord->Special == editor::SpecialKey::Down || (chord->Control && chord->Codepoint == U'n');

    if (up) {
        scrollEntry_ = scrollEntry_ > 0 ? scrollEntry_ - 1 : 0;
    }
    else if (down) {
        ++scrollEntry_;
    }
    else if (chord->Special == editor::SpecialKey::PageUp) {
        scrollEntry_ = scrollEntry_ > static_cast<std::size_t>(visible) ? scrollEntry_ - visible : 0;
    }
    else if (chord->Special == editor::SpecialKey::PageDown) {
        scrollEntry_ += static_cast<std::size_t>(visible);
    }
    else if (chord->Special == editor::SpecialKey::Home) {
        scrollEntry_ = 0;
    }
    else if (chord->Special == editor::SpecialKey::End) {
        scrollEntry_ = Entries().size(); // clamped on the next Paint
    }
    // Every other key is consumed: this panel owns real keyboard focus, so
    // anything leaking through would edit the buffer underneath invisibly.
    return true;
}

} // namespace ned::ui
