//
// Pictures in the ACP transcript, drawn by Notcurses. Each image is decoded
// once and drawn two ways: as cells (one glyph and two colours each, from
// Notcurses' best glyph blitter), which every truecolor terminal shows and
// which scroll, clip and sit under overlays like any other text; and, on a
// terminal with pixel graphics, as a real bitmap on a plane of its own over
// those cells -- only while the whole picture is on screen and nothing
// overlaps it, since a plane can't be clipped or covered by ordinary cells.
//

#ifndef NED_UI_ACPPANEL_INLINEIMAGES_H
#define NED_UI_ACPPANEL_INLINEIMAGES_H

#include <cstdint>
#include <list>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Editor/Acp/Manager.h"
#include "Editor/Image/Decode.h"
#include "UI/Widget.h"

struct ncplane;

namespace ned::ui {
class EventLoop;
}

namespace ned::ui::acppanel {

class InlineImages {
  public:
    InlineImages() = default;
    ~InlineImages();
    InlineImages(const InlineImages&)            = delete;
    InlineImages& operator=(const InlineImages&) = delete;

    // Null (the default, and every headless test without one) draws
    // nothing; Fit still works, assuming cells twice as tall as wide.
    void SetEventLoop(EventLoop* eventLoop);

    // The box `image` fills within maxColumns x kImageMaxRows; nullopt when
    // it doesn't decode.
    [[nodiscard]] std::optional<editor::image::CellFit> Fit(const editor::acp::Manager::TranscriptImage& image, int maxColumns);

    // The picture as columns x rows cells, row-major, over `background`;
    // null when there's no Notcurses to draw it or it doesn't decode.
    [[nodiscard]] const std::vector<Cell>* Cells(const editor::acp::Manager::TranscriptImage& image, int columns, int rows, Color background);

    // Puts the picture's bitmap over `box` (absolute screen cells) for this
    // frame, on a terminal with pixel graphics. Returns whether it's shown.
    bool ShowPixels(const editor::acp::Manager::TranscriptImage& image, Box box, Color background);
    // Removes every bitmap not shown since the last call -- once a frame,
    // after painting, so one whose rows scrolled away, whose panel closed
    // or that something now covers doesn't stay on the terminal.
    void EndFrame();
    // Removes every bitmap. Notcurses frees all planes when it stops (a
    // suspend), so this must run first.
    void ReleaseAll();

  private:
    struct Plane {
        ncplane* plane = nullptr;
        Box      box;
        bool     shown = false;
    };

    // The decoded pixels, decoding (and remembering) on first use; null
    // when it doesn't decode.
    const editor::image::RgbaImage*   Decode(const editor::acp::Manager::TranscriptImage& image);
    [[nodiscard]] std::pair<int, int> CellPixels() const; // width, height
    void                              ReleasePlane(Plane& plane);

    EventLoop* eventLoop_ = nullptr;
    // Every image's size, once known (nullopt: it doesn't decode).
    std::unordered_map<std::uint64_t, std::optional<std::pair<int, int>>> sizes_;
    // Decoded pixels, most recently used last. A handful covers what a
    // transcript shows at once, and each can be tens of megabytes.
    static constexpr std::size_t                                  kMaxDecoded = 6;
    std::list<std::pair<std::uint64_t, editor::image::RgbaImage>> decoded_;
    // Cell renderings by image, size and background.
    static constexpr std::size_t                                                    kMaxCellRenderings = 32;
    std::map<std::tuple<std::uint64_t, int, int, std::uint32_t>, std::vector<Cell>> cells_;
    std::unordered_map<std::uint64_t, Plane>                                        planes_;
};

} // namespace ned::ui::acppanel

#endif // NED_UI_ACPPANEL_INLINEIMAGES_H
