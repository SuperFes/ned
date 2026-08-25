#include "Minimap.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>

#include <notcurses/notcurses.h>

#include "Editor/MinimapSettings.h"
#include "Editor/Multibuffer.h"
#include "Editor/SyntaxTheme.h"
#include "EventLoop.h"

namespace ned::ui {

namespace {

    // A copy of one HighlightSpan plus its position in the original
    // mode_.highlight() result -- kept so a sorted-by-startByte copy can
    // still resolve an overlap the same "later in the original vector
    // wins" way BufferView's own ClassAtOffset does, per Mode.h's own
    // documented convention.
    struct IndexedSpan {
        std::size_t         startByte;
        std::size_t         endByte;
        editor::SyntaxClass syntaxClass;
        editor::CaptureId   captureId;
        std::size_t         originalIndex;
    };

    // per-capture-styling-in-minimap follow-up: the winning span's class AND
    // capture id, so the caller can resolve a per-capture-name color override
    // (theme_.BrushFor(cls, captureId)) the same way BufferView's own
    // real-buffer rendering does, not just the coarser per-SyntaxClass one.
    struct ClassAtResult {
        editor::SyntaxClass syntaxClass = editor::SyntaxClass::Default;
        editor::CaptureId   captureId   = editor::kNoCapture;
    };

    // Best-effort syntax class at offset: binary-searches sortedSpans (by
    // startByte) for the latest-starting span that could contain offset,
    // then walks backward through a small, bounded window of
    // earlier-starting-but-still-open candidates, taking whichever has the
    // highest originalIndex (the "later wins" rule) among those that
    // actually contain offset. Deliberately not a full interval-tree
    // resolution -- real overlap depth at any single point is small in
    // practice (a handful of nested captures at most), and this is a
    // cosmetic minimap, not the real highlighter.
    ClassAtResult ClassAt(const std::vector<IndexedSpan>& sortedSpans, std::size_t offset) {
        constexpr std::size_t kMaxBackwardScan = 64;
        auto                  it               = std::upper_bound(sortedSpans.begin(), sortedSpans.end(), offset,
                                                                  [](std::size_t value, const IndexedSpan& span) { return value < span.startByte; });
        std::size_t           scanned          = 0;
        std::size_t           bestIndex        = 0;
        bool                  found            = false;
        ClassAtResult         best;
        while (it != sortedSpans.begin() && scanned < kMaxBackwardScan) {
            --it;
            ++scanned;
            if (it->startByte <= offset && offset < it->endByte) {
                if (!found || it->originalIndex > bestIndex) {
                    best.syntaxClass = it->syntaxClass;
                    best.captureId   = it->captureId;
                    bestIndex        = it->originalIndex;
                    found            = true;
                }
            }
        }
        return best;
    }

    [[nodiscard]] bool IsBlank(char32_t codepoint) {
        return codepoint == U' ' || codepoint == U'\t' || codepoint == U'\n' || codepoint == U'\r';
    }

} // namespace

Minimap::Minimap(const ActiveBuffer& activeBuffer, const editor::Mode& mode, const Theme& theme) : activeBuffer_(activeBuffer), mode_(mode), theme_(theme) {
    if (const char* path = std::getenv("NED_DEBUG_MINIMAP"); path && *path) {
        debugLogPath_ = path;
    }
}

Minimap::~Minimap() {
    ReleasePlane();
}

void Minimap::DebugLog(const std::string& line) const {
    if (!debugLogPath_.empty()) {
        std::ofstream(debugLogPath_, std::ios::app) << line << std::endl;
    }
}

void Minimap::SetOnScroll(std::function<void(int)> onScroll) {
    onScroll_ = std::move(onScroll);
}

void Minimap::SetEventLoop(EventLoop* eventLoop) {
    eventLoop_ = eventLoop;
    if (eventLoop_ != nullptr) {
        DebugLog(std::string("SetEventLoop: CanPixelGraphics()=") + (eventLoop_->CanPixelGraphics() ? "true" : "false"));
    }
}

void Minimap::ReleasePlane() const {
    if (plane_ != nullptr) {
        ncplane_destroy(plane_);
        plane_ = nullptr;
    }
}

void Minimap::ClearBufferCache(text::Buffer& buffer) {
    highlightCacheByBuffer_.erase(&buffer);
    if (cacheBuffer_ == &buffer) {
        cacheBuffer_ = nullptr;
    }
}

void Minimap::ForEachDensityDot(
    int subRows, int subCols, int charsPerDot,
    const std::function<void(int subRow, int subCol, std::size_t offset, std::size_t linesInRow)>& visit) const {
    text::Buffer&     buffer     = activeBuffer_.Get();
    const text::Rope& content    = buffer.Content();
    const std::size_t totalLines = content.LineCount();
    if (totalLines == 0 || subRows <= 0 || subCols <= 0) {
        return;
    }

    // "Only go as deep to the right as 1-2 chars per pixel" -- the user's
    // own framing, not a compression ratio: a line longer than this simply
    // isn't rendered past this column, no attempt to squeeze it in.
    const int maxColumn = subCols * std::max(charsPerDot, 1);

    for (int subRow = 0; subRow < subRows; ++subRow) {
        const std::size_t lineStart =
            static_cast<std::size_t>((static_cast<long long>(subRow) * static_cast<long long>(totalLines)) / subRows);
        std::size_t lineEnd =
            static_cast<std::size_t>((static_cast<long long>(subRow + 1) * static_cast<long long>(totalLines)) / subRows);
        if (lineEnd <= lineStart) {
            lineEnd = lineStart + 1;
        }
        lineEnd = std::min(lineEnd, totalLines);
        const std::size_t linesInRow = lineEnd - lineStart;

        for (std::size_t line = lineStart; line < lineEnd; ++line) {
            const std::size_t lineStartByte = content.LineToByteOffset(line);
            const std::size_t lineEndByte =
                (line + 1 < totalLines) ? content.LineToByteOffset(line + 1) : content.ByteLength();

            std::size_t offset          = lineStartByte;
            int         column          = 0;
            int         lastVisitedCol  = -1; // weighted-minimap-density follow-up: see below
            while (offset < lineEndByte && column < maxColumn) {
                const auto decoded = content.CodepointAt(offset);
                if (!IsBlank(decoded.codepoint)) {
                    const int subCol = column / std::max(charsPerDot, 1);
                    // At most one visit() per (subRow, subCol) per real
                    // line, not one per character -- a caller weighting
                    // hits by linesInRow (a density: "how many of the
                    // lines sharing this row actually had ink here")
                    // needs "did this line touch this dot at all," not
                    // "how many of this line's own characters happened to
                    // land in the same charsPerDot-wide group" (a single
                    // dense line could otherwise saturate that count on
                    // its own, defeating the whole point of weighting by
                    // line count).
                    if (subCol < subCols && subCol != lastVisitedCol) {
                        visit(subRow, subCol, offset, linesInRow);
                        lastVisitedCol = subCol;
                    }
                }
                ++column;
                offset += decoded.byteLength;
            }
        }
    }
}

void Minimap::EnsurePlane() const {
    const int height = size().height;
    const int width  = editor::MinimapWidth();
    if (eventLoop_ == nullptr || width <= 0 || height <= 0) {
        DebugLog("EnsurePlane: bail -- eventLoop_=" + std::string(eventLoop_ == nullptr ? "null" : "set") +
                 " width=" + std::to_string(width) + " height=" + std::to_string(height));
        ReleasePlane();
        return;
    }

    const bool usePixel = eventLoop_->CanPixelGraphics();

    // NCBLIT_PIXEL's NCSCALE_NONE path (the confirmed-correct one -- see
    // this class's own header comment on the real root cause every other
    // combination's failure traced back to) needs a source raster built at
    // exactly the terminal's real cell-pixel resolution, since it does no
    // scaling math at all. The glyph path (NCBLIT_DEFAULT) doesn't care --
    // NCSCALE_STRETCH resamples whatever it's handed to fit the plane, so a
    // fixed, generous resolution is fine there.
    unsigned celldimy = 0, celldimx = 0;
    if (usePixel) {
        ncplane_pixel_geom(eventLoop_->StdPlane(), nullptr, nullptr, &celldimy, &celldimx, nullptr, nullptr);
    }

    text::Buffer& buffer = activeBuffer_.Get();
    const bool    sameCache =
        plane_ != nullptr && cacheBuffer_ == &buffer && cacheContentGeneration_ == buffer.ContentGeneration() &&
        cacheHeight_ == height && cacheWidth_ == width && cacheCharsPerDot_ == editor::MinimapCharsPerDot() &&
        cacheScrollableLength_ == scrollable_length && cachePosition_ == position &&
        cacheItemVisualLength_ == item_visual_length &&
        (!usePixel || (cachePixelCellDimY_ == static_cast<int>(celldimy) && cachePixelCellDimX_ == static_cast<int>(celldimx)));

    if (sameCache) {
        ncplane_move_yx(plane_, Box_().y_min, Box_().x_min);
        return;
    }

    // Own the plane outright (ncplane_create, sized exactly width x height
    // cells) rather than letting ncvisual_blit create/size one itself, and
    // reuse it (not destroy/recreate) across reblits whose cell size hasn't
    // changed.
    const bool needNewPlane = plane_ == nullptr || cacheHeight_ != height || cacheWidth_ != width;

    cacheBuffer_            = &buffer;
    cacheContentGeneration_ = buffer.ContentGeneration();
    cacheHeight_            = height;
    cacheWidth_             = width;
    cacheCharsPerDot_       = editor::MinimapCharsPerDot();
    cacheScrollableLength_  = scrollable_length;
    cachePosition_          = position;
    cacheItemVisualLength_  = item_visual_length;
    cachePixelCellDimY_     = static_cast<int>(celldimy);
    cachePixelCellDimX_     = static_cast<int>(celldimx);

    // Source-image resolution: exactly celldim x cells for the real-pixel
    // path (NCSCALE_NONE needs an exact match, see above); a fixed,
    // generous 8 sub-rows/4 sub-cols per cell otherwise (twice the density
    // this feature's braille-era renderer ever used -- NCSCALE_STRETCH
    // resamples it to fit regardless, so this only needs to be "detailed
    // enough," not exact).
    const int dotRows = usePixel && celldimy > 0 ? height * static_cast<int>(celldimy) : height * 8;
    const int dotCols = usePixel && celldimx > 0 ? width * static_cast<int>(celldimx) : width * 4;

    // Reserve a thin strip at the right edge for the scroll-position band
    // alone -- content dots never get placed there (see the
    // ForEachDensityDot call below, which walks contentCols, not dotCols,
    // offset by leftMarginCols when writing into pixels), so it always
    // reads as a clean, unobstructed scrollbar instead of competing with
    // code-colored dots for the same pixels. A matching-width blank margin
    // on the left keeps the content centered rather than flush against one
    // edge with a gap only on the other. Scales with resolution rather than
    // a fixed pixel count, since dotCols itself spans a very different real
    // range between glyph mode (width*4) and real-pixel mode
    // (width*celldimx, often several times wider) -- clamped so it's never
    // too thin to see (2px) or wide enough to matter (6px).
    const int scrollStripCols = std::clamp(dotCols / 8, 2, 6);
    const int leftMarginCols  = scrollStripCols;
    const int contentCols     = std::max(dotCols - scrollStripCols - leftMarginCols, 1);

    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(dotCols) * static_cast<std::size_t>(dotRows) * 4);

    // Real transparency follow-up: tried NCVISUAL_OPTION_BLEND with a
    // transparent background and half-opaque dots -- confirmed live it
    // isn't reliably supported for the Kitty pixel protocol across
    // terminals (Alacritty showed an unrelated side effect from its own
    // window transparency; Konsole rendered nothing at all). Reverted to
    // fully opaque, same as the glyph path: the scroll-position band is
    // baked directly into this raster (identical proportional math to
    // ScrollBar's own thumbStart/thumbRows, ScrollBar.cpp). Computed in
    // whole *cell* rows first (clamped to at least one, exactly like
    // ScrollBar's own thumb) and only then scaled up to dot-rows --
    // computing directly in dot-rows let the 1-dot-minimum clamp produce a
    // band as thin as 1 row out of dotRows (392 for a typical pane), which
    // nearest-neighbor downsampling (NCVISUAL_OPTION_NOINTERPOLATE, glyph
    // path) could -- and in practice did -- skip over entirely. A whole
    // cell's worth of dot-rows is wide enough that some sampled row within
    // it is always hit.
    const int cellDotRows     = dotRows / height; // exact -- dotRows is always a multiple of height
    const int total           = std::max(scrollable_length, 1);
    const bool scrolls        = total > item_visual_length;
    const int bandRowsCells   = std::clamp(static_cast<int>((static_cast<long long>(item_visual_length) * height) / total), 1, height);
    const int maxBandTopCells = height - bandRowsCells;
    const int bandTopCells =
        scrolls ? std::clamp(static_cast<int>((static_cast<long long>(position) * maxBandTopCells) /
                                               std::max(total - item_visual_length, 1)),
                             0, maxBandTopCells)
                : 0;
    const int bandTop  = bandTopCells * cellDotRows;
    const int bandRows = bandRowsCells * cellDotRows;

    // minimap-natural-scale follow-up: a buffer short enough to need no
    // scrolling at all used to still have its handful of real lines
    // stretched across the full dotRows canvas (the same resolution a
    // buffer with thousands of lines uses) -- a short file's minimap read
    // as big, blocky, stretched-out glyphs bearing no visible resemblance
    // to "a zoomed-out version of a few short lines." One dot-row per real
    // line instead when there's nothing to scroll to (the user's own
    // framing), leaving the remainder of the canvas as plain background
    // below it -- deliberately *not* cellDotRows dot-rows per line (an
    // earlier version of this fix tried that, replicating one line's data
    // cellDotRows times for "the same per-line resolution the scrolling
    // case uses"): every one of those replicated sub-rows ends up pixel-
    // identical to its neighbors, which is exactly the perfectly-uniform-
    // per-cell source block shape Notcurses' NCBLIT_DEFAULT auto-blitter
    // renders as blank (confirmed live -- see this class's own git history/
    // the investigation that found it). One real dot-row per line instead
    // lets adjacent *different* lines share a terminal cell's sub-rows
    // exactly like the scrolling/compressed case already does, giving each
    // cell real internal variation to render instead of a uniform block.
    const int contentSubRows = scrolls ? dotRows : std::clamp(static_cast<int>(buffer.Content().LineCount()), 0, dotRows);

    // theme_.background is Color::Default in most bundled themes
    // (deliberately -- see DarkTheme()'s own comment -- "let the
    // terminal's own background show" wherever real Cells leave it alone).
    // An opaque raster can't leave a pixel "unspecified" the same way --
    // ColorToRgb8's own generic Color::Default approximation is a neutral
    // mid-gray, which reads as a visibly wrong silver box; a concrete
    // near-black guess is right far more often than neutral gray, since
    // this project's whole theme catalog is dark-first. Also the blend
    // target for desaturating dot colors below, in both real-pixel and
    // glyph mode -- "saturate towards light or dark given your theme," a
    // direct user request, and reusing this same value keeps the whole
    // raster internally consistent rather than picking two different
    // notions of "the background."
    const Color backgroundApprox =
        theme_.background.kind == Color::Kind::Default ? Color::RGB(0x1a1a1a) : theme_.background;

    std::uint8_t bgR, bgG, bgB, bandR, bandG, bandB;
    ColorToRgb8(backgroundApprox, bgR, bgG, bgB);
    ColorToRgb8(theme_.selectionBackground, bandR, bandG, bandB);

    // Multibuffers follow-up: a *vcs diff*-style multibuffer's own added/
    // removed lines get the same background wash here that the main
    // BufferView gives them (Editor/Multibuffer.h's LineTint) -- without
    // this the minimap renders as a flat, undifferentiated blob for a view
    // whose entire point is showing where the changes are at a glance.
    // Looked up once per raster build (this buffer's content never changes
    // after BuildMultibuffer constructs it, so EnsurePlane's own
    // cacheContentGeneration_ check already keeps this from recomputing
    // every frame), not per dot -- a plain per-line lookup, same "cosmetic
    // minimap, not the real highlighter" posture ClassAt's own comment
    // states for syntax color.
    const auto*       multibufferIndex  = editor::multibuffer::MultibufferIndexFor(buffer);
    const std::size_t totalLinesForTint = buffer.Content().LineCount();
    std::uint8_t      addedR = 0, addedG = 0, addedB = 0, removedR = 0, removedG = 0, removedB = 0;
    if (multibufferIndex) {
        ColorToRgb8(theme_.diffAddedBackground, addedR, addedG, addedB);
        ColorToRgb8(theme_.diffRemovedBackground, removedR, removedG, removedB);
    }

    for (int py = 0; py < dotRows; ++py) {
        const bool   inBand = py >= bandTop && py < bandTop + bandRows;
        std::uint8_t r = bgR, g = bgG, b = bgB;
        if (inBand) {
            r = bandR;
            g = bandG;
            b = bandB;
        }
        else if (multibufferIndex != nullptr && totalLinesForTint > 0) {
            // Same subRow -> line mapping ForEachDensityDot uses below (py
            // here IS a subRow -- dotRows/subRows are the same value this
            // raster is built at) -- a whole dot-row can span many lines of
            // a short diff or a fraction of one on a long file; taking the
            // tint of the row's own first line is an approximation, not
            // exact per-line resolution, the same trade-off ClassAt's
            // bounded backward scan already makes for syntax color.
            const std::size_t line = static_cast<std::size_t>(
                (static_cast<long long>(py) * static_cast<long long>(totalLinesForTint)) / dotRows);
            switch (multibufferIndex->TintForLine(line)) {
                case editor::multibuffer::LineTint::Added:
                    r = addedR;
                    g = addedG;
                    b = addedB;
                    break;
                case editor::multibuffer::LineTint::Removed:
                    r = removedR;
                    g = removedG;
                    b = removedB;
                    break;
                default:
                    break;
            }
        }
        for (int px = 0; px < dotCols; ++px) {
            const std::size_t idx = (static_cast<std::size_t>(py) * static_cast<std::size_t>(dotCols) +
                                     static_cast<std::size_t>(px)) *
                                    4;
            pixels[idx + 0]       = r;
            pixels[idx + 1]       = g;
            pixels[idx + 2]       = b;
            pixels[idx + 3]       = 255;
        }
    }

    std::vector<IndexedSpan> sortedSpans;
    if (mode_.highlight) {
        // per-buffer-highlight-cache follow-up: this call used to run
        // unconditionally every time the raster cache above missed --
        // which includes every scroll tick, not just a buffer switch --
        // even though its result only actually changes when buffer's
        // content/mode does. See highlightCacheByBuffer_'s own doc comment
        // in Minimap.h.
        const auto it = highlightCacheByBuffer_.find(&buffer);
        const std::vector<editor::HighlightSpan>* spans;
        if (it == highlightCacheByBuffer_.end() || it->second.contentGeneration != buffer.ContentGeneration() ||
            it->second.classGeneration != editor::CaptureClassGeneration() || it->second.modeName != mode_.name) {
            HighlightCacheEntry entry;
            entry.spans             = mode_.highlight(buffer.Text());
            entry.contentGeneration = buffer.ContentGeneration();
            entry.classGeneration   = editor::CaptureClassGeneration();
            entry.modeName          = mode_.name;
            spans                   = &highlightCacheByBuffer_.insert_or_assign(&buffer, std::move(entry)).first->second.spans;
        }
        else {
            spans = &it->second.spans;
        }
        sortedSpans.reserve(spans->size());
        for (std::size_t i = 0; i < spans->size(); ++i) {
            sortedSpans.push_back(IndexedSpan{(*spans)[i].startByte, (*spans)[i].endByte, (*spans)[i].syntaxClass, (*spans)[i].captureId, i});
        }
        std::sort(sortedSpans.begin(), sortedSpans.end(),
                  [](const IndexedSpan& a, const IndexedSpan& b) { return a.startByte < b.startByte; });
    }

    // Real-pixel mode has enough columns to give each source character its
    // own pixel column (charsPerDot=1) instead of the horizontal
    // compression the glyph path still needs (a braille/quadrant/octant
    // cell only has 2-4 sub-columns to represent a line in at all) -- see
    // ForEachDensityDot's own doc comment.
    const int effectiveCharsPerDot = usePixel ? 1 : editor::MinimapCharsPerDot();

    // weighted-minimap-density follow-up: accumulate, per (subRow, subCol),
    // how many of the real lines compressed into that row actually had ink
    // there (hitCount, out of rowLineCount[subRow]) and whichever color was
    // last seen there -- then blend proportionally to that fraction below,
    // rather than one line's ink flatly overwriting whatever an earlier
    // line sharing the same row already wrote. Without this, a *references*
    // multibuffer's own repeated separator/rule lines (near-full-width ink
    // on almost every one of them) dominated every row they shared with the
    // real matched-line content, since whichever line ForEachDensityDot
    // happened to walk last for that row is what a flat overwrite would
    // have shown.
    std::vector<std::uint16_t> hitCount(static_cast<std::size_t>(contentSubRows) * static_cast<std::size_t>(contentCols), 0);
    std::vector<Color>         hitColor(hitCount.size(), theme_.defaultForeground);
    std::vector<std::size_t>   rowLineCount(static_cast<std::size_t>(std::max(contentSubRows, 1)), 1);

    ForEachDensityDot(contentSubRows, contentCols, effectiveCharsPerDot,
                      [&](int subRow, int subCol, std::size_t offset, std::size_t linesInRow) {
                          // defaultForeground (ordinary buffer text), not lineNumberForeground
                          // (a deliberately dim gutter color) -- most real code is plain
                          // identifiers/punctuation with no distinct SyntaxClass span at all,
                          // so this fallback is what most of the minimap's "ink" actually
                          // renders in; the dim gutter color made most of it read as flat
                          // gray instead of matching how the real buffer looks.
                          Color fg = theme_.defaultForeground;
                          if (!sortedSpans.empty()) {
                              const ClassAtResult at = ClassAt(sortedSpans, offset);
                              if (at.syntaxClass != editor::SyntaxClass::Default) {
                                  fg = theme_.BrushFor(at.syntaxClass, at.captureId).foreground;
                              }
                          }
                          const std::size_t idx = static_cast<std::size_t>(subRow) * static_cast<std::size_t>(contentCols) +
                                                  static_cast<std::size_t>(subCol);
                          ++hitCount[idx];
                          hitColor[idx]                                       = fg; // last-hit color -- one representative color per pixel is enough here
                          rowLineCount[static_cast<std::size_t>(subRow)] = std::max<std::size_t>(linesInRow, 1);
                      });

    for (int subRow = 0; subRow < contentSubRows; ++subRow) {
        for (int subCol = 0; subCol < contentCols; ++subCol) {
            const std::size_t idx =
                static_cast<std::size_t>(subRow) * static_cast<std::size_t>(contentCols) + static_cast<std::size_t>(subCol);
            if (hitCount[idx] == 0) {
                continue; // stays whatever the background/band loop above already painted
            }
            const float density = std::min(
                1.0F, static_cast<float>(hitCount[idx]) / static_cast<float>(rowLineCount[static_cast<std::size_t>(subRow)]));

            // Real-pixel mode only: blend each dot's color partway towards the
            // background (real RGB blending, not alpha -- confirmed live that
            // partial alpha compositing isn't reliable for the Kitty pixel
            // protocol across terminals, see the background-fill comment
            // above) -- full syntax saturation on every non-blank character
            // read as visually overwhelming compared to the real editor (a
            // direct, live user complaint), and it drowned out the
            // scroll-position band. Blending towards backgroundApprox rather
            // than a fixed gray is what makes this "saturate towards light or
            // dark" -- a dark theme's near-black target dims every color
            // towards black, a light theme's own light background would
            // lighten them instead, both automatically, no separate light/dark
            // branch needed. Glyph mode is unaffected -- it never had this
            // complaint (a quadrant/sextant/octant glyph's own 2-color-per-cell
            // blending already reads calmer than a real image's continuous
            // per-pixel color at this small a size).
            const Color  dotColor = usePixel ? Color::Interpolate(0.45F, hitColor[idx], backgroundApprox) : hitColor[idx];
            std::uint8_t inkR, inkG, inkB;
            ColorToRgb8(dotColor, inkR, inkG, inkB);

            const std::size_t pixelIdx = (static_cast<std::size_t>(subRow) * static_cast<std::size_t>(dotCols) +
                                          static_cast<std::size_t>(subCol + leftMarginCols)) *
                                         4;
            // Blend from whatever's already there (background or the
            // scroll-position band, painted above) towards the ink color,
            // proportional to density -- a column only 1 of 4 compressed
            // lines actually touched reads as a faint hint, not full-strength
            // ink the way a flat overwrite would have shown it.
            pixels[pixelIdx + 0] = static_cast<std::uint8_t>(
                static_cast<float>(pixels[pixelIdx + 0]) + density * (static_cast<float>(inkR) - static_cast<float>(pixels[pixelIdx + 0])));
            pixels[pixelIdx + 1] = static_cast<std::uint8_t>(
                static_cast<float>(pixels[pixelIdx + 1]) + density * (static_cast<float>(inkG) - static_cast<float>(pixels[pixelIdx + 1])));
            pixels[pixelIdx + 2] = static_cast<std::uint8_t>(
                static_cast<float>(pixels[pixelIdx + 2]) + density * (static_cast<float>(inkB) - static_cast<float>(pixels[pixelIdx + 2])));
            pixels[pixelIdx + 3] = 255;
        }
    }

    if (needNewPlane) {
        ReleasePlane();
        ncplane_options nopts{};
        nopts.y    = Box_().y_min;
        nopts.x    = Box_().x_min;
        nopts.rows = static_cast<unsigned>(height);
        nopts.cols = static_cast<unsigned>(width);
        plane_     = ncplane_create(eventLoop_->StdPlane(), &nopts);
        if (plane_ == nullptr) {
            DebugLog("EnsurePlane: ncplane_create returned null, giving up for this session");
            planeUnavailable_ = true;
            return;
        }
    }

    ncvisual* visual = ncvisual_from_rgba(pixels.data(), dotRows, dotCols * 4, dotCols);
    if (visual == nullptr) {
        DebugLog("EnsurePlane: ncvisual_from_rgba returned null, giving up for this session");
        ReleasePlane();
        planeUnavailable_ = true;
        return;
    }

    ncvisual_options vopts{};
    vopts.n = plane_;
    if (usePixel) {
        // Source raster is already built at exactly celldim x cells (above),
        // so NCSCALE_NONE needs no scaling math at all -- confirmed, live,
        // the only combination that reliably renders correctly (see this
        // class's own header comment for the real story: every other
        // combination's failure traced back to one project-level build bug,
        // not a real notcurses defect).
        vopts.scaling = NCSCALE_NONE;
        vopts.blitter = NCBLIT_PIXEL;
    }
    else {
        vopts.scaling = NCSCALE_STRETCH;
        vopts.blitter = NCBLIT_DEFAULT;
        // Shrinking dotRows x dotCols down to a handful of glyph sub-cells
        // otherwise interpolates (blends) neighboring source pixels --
        // muddying distinct syntax colors together into off-palette blends
        // instead of keeping each glyph's two colors true to whatever source
        // pixels they actually sample. Naive/nearest-neighbor scaling keeps
        // every displayed color one this class actually painted.
        vopts.flags = NCVISUAL_OPTION_NOINTERPOLATE;
    }

    ncplane* drawn = ncvisual_blit(eventLoop_->NotcursesContext(), visual, &vopts);
    ncvisual_destroy(visual);
    if (drawn == nullptr) {
        DebugLog("EnsurePlane: ncvisual_blit failed, giving up for this session");
        ReleasePlane();
        planeUnavailable_ = true;
        return;
    }
    plane_ = drawn;
    DebugLog(std::string("EnsurePlane: blit succeeded (") + (usePixel ? "NCBLIT_PIXEL" : "NCBLIT_DEFAULT") + ")");
}

void Minimap::PaintPlane(Canvas c) {
    const int width  = editor::MinimapWidth();
    const int height = size().height;
    if (width <= 0 || height <= 0 || planeUnavailable_) {
        ReleasePlane();
        return;
    }

    EnsurePlane();

    // plane_ (if EnsurePlane() succeeded) sits above this widget's own
    // cells in the same pile and covers this exact Box_(), fully opaque
    // (see EnsurePlane()'s own comment on why real per-pixel transparency
    // isn't used) -- so this is just a flat backstop, mattering only if the
    // blit failed this frame or hasn't landed on the terminal yet.
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            Cell& out            = c[{.x = x, .y = y}];
            out.character        = " ";
            out.foreground_color = theme_.background;
            out.background_color = theme_.background;
        }
    }
}

void Minimap::Paint(Canvas c) {
    PaintPlane(c);
}

int Minimap::PositionForRow(int row) const {
    const int height = size().height;
    if (height <= 0) {
        return 0;
    }
    const int total = std::max(scrollable_length, 1);
    if (total <= item_visual_length) {
        return 0;
    }
    const int maxPosition = total - item_visual_length;
    const int clampedRow  = std::clamp(row, 0, height - 1);
    // See ScrollBar::PositionForRow's own comment -- identical fix, kept
    // identical to that formula by design (this class's own header comment
    // on why a click always has to land exactly where it visually appears
    // to, between the two renderers as well as between the two widgets).
    if (clampedRow >= height - 1) {
        return maxPosition;
    }
    return std::clamp(static_cast<int>((static_cast<long long>(clampedRow) * total) / height), 0, maxPosition);
}

bool Minimap::OnEvent(const Event& event) {
    if (const auto mouse = LocalMouseEvent(event)) {
        if (mouse->button == MouseEvent::Button::Left && mouse->motion == MouseEvent::Motion::Pressed) {
            dragging_ = true;
            if (onScroll_) {
                onScroll_(PositionForRow(mouse->at.y));
            }
            return true;
        }
        if (mouse->motion == MouseEvent::Motion::Moved && dragging_) {
            if (onScroll_) {
                onScroll_(PositionForRow(mouse->at.y));
            }
            return true;
        }
    }
    if (event.is_mouse() && event.mouse().motion == MouseEvent::Motion::Released && dragging_) {
        dragging_ = false;
        return true;
    }
    return false;
}

} // namespace ned::ui
