#include "InlineImages.h"

#include <algorithm>
#include <cstdlib>
#include <memory>

#include <notcurses/notcurses.h>

#include "Text/Base64.h"
#include "TranscriptFormat.h"
#include "UI/EventLoop.h"

namespace ned::ui::acppanel {

namespace {

    std::uint32_t Rgb(Color color) {
        std::uint8_t r = 0, g = 0, b = 0;
        ColorToRgb8(color, r, g, b);
        return (static_cast<std::uint32_t>(r) << 16) | (static_cast<std::uint32_t>(g) << 8) | b;
    }

    editor::image::RgbaImage Flattened(editor::image::RgbaImage image, Color background) {
        std::uint8_t r = 0, g = 0, b = 0;
        ColorToRgb8(background, r, g, b);
        editor::image::Flatten(image, r, g, b);
        return image;
    }

} // namespace

InlineImages::~InlineImages() {
    for (auto& [id, plane] : planes_) {
        ReleasePlane(plane);
    }
}

void InlineImages::SetEventLoop(EventLoop* eventLoop) {
    eventLoop_ = eventLoop;
}

const editor::image::RgbaImage* InlineImages::Decode(const editor::acp::Manager::TranscriptImage& image) {
    const auto known = sizes_.find(image.id);
    if (known != sizes_.end() && !known->second) {
        return nullptr;
    }
    const auto cached = std::find_if(decoded_.begin(), decoded_.end(), [&image](const auto& entry) { return entry.first == image.id; });
    if (cached != decoded_.end()) {
        decoded_.splice(decoded_.end(), decoded_, cached);
        return &decoded_.back().second;
    }
    std::optional<editor::image::RgbaImage> pixels;
    if (const std::optional<std::string> bytes = text::Base64Decode(image.data)) {
        pixels = editor::image::DecodeImage(*bytes);
    }
    if (!pixels) {
        sizes_[image.id] = std::nullopt;
        return nullptr;
    }
    sizes_[image.id] = std::pair{pixels->width, pixels->height};
    decoded_.emplace_back(image.id, std::move(*pixels));
    if (decoded_.size() > kMaxDecoded) {
        decoded_.pop_front();
    }
    return &decoded_.back().second;
}

std::pair<int, int> InlineImages::CellPixels() const {
    unsigned cellHeight = 0;
    unsigned cellWidth  = 0;
    if (eventLoop_ && eventLoop_->StdPlane()) {
        ncplane_pixel_geom(eventLoop_->StdPlane(), nullptr, nullptr, &cellHeight, &cellWidth, nullptr, nullptr);
    }
    // A terminal that doesn't say: the usual shape, twice as tall as wide.
    if (cellWidth == 0 || cellHeight == 0) {
        return {10, 20};
    }
    return {static_cast<int>(cellWidth), static_cast<int>(cellHeight)};
}

std::optional<editor::image::CellFit> InlineImages::Fit(const editor::acp::Manager::TranscriptImage& image, int maxColumns) {
    auto size = sizes_.find(image.id);
    if (size == sizes_.end()) {
        Decode(image);
        size = sizes_.find(image.id);
    }
    if (size == sizes_.end() || !size->second) {
        return std::nullopt;
    }
    const auto [cellWidth, cellHeight] = CellPixels();
    const editor::image::CellFit fit   = editor::image::FitToCells(size->second->first, size->second->second, cellWidth, cellHeight, maxColumns, kImageMaxRows);
    if (fit.columns <= 0 || fit.rows <= 0) {
        return std::nullopt;
    }
    return fit;
}

const std::vector<Cell>* InlineImages::Cells(const editor::acp::Manager::TranscriptImage& image, int columns, int rows, Color background) {
    const auto key = std::tuple{image.id, columns, rows, Rgb(background)};
    if (const auto cached = cells_.find(key); cached != cells_.end()) {
        return &cached->second;
    }
    notcurses* nc = eventLoop_ ? eventLoop_->NotcursesContext() : nullptr;
    if (nc == nullptr || columns <= 0 || rows <= 0) {
        return nullptr;
    }
    const editor::image::RgbaImage* pixels = Decode(image);
    if (pixels == nullptr) {
        return nullptr;
    }
    // Drawn on a plane of a pile that's never rendered, then read back cell
    // by cell: Notcurses picks the glyph blitter (octants, sextants,
    // quadrants or half blocks, whatever the terminal can show) and does the
    // colour work, and the result is ordinary cells.
    ncplane_options options{};
    options.rows = static_cast<unsigned>(rows);
    options.cols = static_cast<unsigned>(columns);
    const std::unique_ptr<ncplane, int (*)(ncplane*)> plane(ncpile_create(nc, &options), ncplane_destroy);
    if (!plane) {
        return nullptr;
    }
    // At most two by four dots a cell is all any glyph blitter samples.
    const editor::image::RgbaImage source =
        Flattened(editor::image::Resize(*pixels, std::min(pixels->width, columns * 2), std::min(pixels->height, rows * 4)), background);
    ncvisual* visual = ncvisual_from_rgba(source.pixels.data(), source.height, source.width * 4, source.width);
    if (visual == nullptr) {
        return nullptr;
    }
    ncvisual_options visualOptions{};
    visualOptions.n       = plane.get();
    visualOptions.scaling = NCSCALE_STRETCH;
    visualOptions.blitter = NCBLIT_DEFAULT;
    const ncplane* drawn  = ncvisual_blit(nc, visual, &visualOptions);
    ncvisual_destroy(visual);
    if (drawn == nullptr) {
        return nullptr;
    }

    std::vector<Cell> cells(static_cast<std::size_t>(columns) * static_cast<std::size_t>(rows));
    for (int y = 0; y < rows; ++y) {
        for (int x = 0; x < columns; ++x) {
            std::uint16_t stylemask = 0;
            std::uint64_t channels  = 0;
            char*         glyph     = ncplane_at_yx(plane.get(), y, x, &stylemask, &channels);
            Cell&         cell      = cells[static_cast<std::size_t>(y) * static_cast<std::size_t>(columns) + static_cast<std::size_t>(x)];
            cell.character          = glyph != nullptr && *glyph != '\0' ? glyph : " ";
            std::free(glyph);
            unsigned r = 0, g = 0, b = 0;
            cell.foreground_color = ncchannels_fg_default_p(channels) ? background
                                                                      : (ncchannels_fg_rgb8(channels, &r, &g, &b), Color::RGB(static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g), static_cast<std::uint8_t>(b)));
            cell.background_color = ncchannels_bg_default_p(channels) || ncchannels_bg_alpha(channels) == NCALPHA_TRANSPARENT
                                        ? background
                                        : (ncchannels_bg_rgb8(channels, &r, &g, &b), Color::RGB(static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g), static_cast<std::uint8_t>(b)));
        }
    }
    if (cells_.size() >= kMaxCellRenderings) {
        cells_.clear();
    }
    return &cells_.emplace(key, std::move(cells)).first->second;
}

bool InlineImages::ShowPixels(const editor::acp::Manager::TranscriptImage& image, Box box, Color background) {
    if (!eventLoop_ || !eventLoop_->CanPixelGraphics() || eventLoop_->NotcursesContext() == nullptr || eventLoop_->StdPlane() == nullptr) {
        return false;
    }
    Plane& shown = planes_[image.id];
    if (shown.plane != nullptr && shown.box.x_max - shown.box.x_min == box.x_max - box.x_min &&
        shown.box.y_max - shown.box.y_min == box.y_max - box.y_min) {
        if (shown.box.x_min != box.x_min || shown.box.y_min != box.y_min) {
            ncplane_move_yx(shown.plane, box.y_min, box.x_min);
            shown.box = box;
        }
        shown.shown = true;
        return true;
    }
    ReleasePlane(shown);
    const editor::image::RgbaImage* pixels = Decode(image);
    if (pixels == nullptr) {
        return false;
    }
    const int columns                  = box.x_max - box.x_min + 1;
    const int rows                     = box.y_max - box.y_min + 1;
    const auto [cellWidth, cellHeight] = CellPixels();
    // Built at exactly the cells' pixel size and blitted unscaled -- the
    // combination Minimap found draws right across terminals.
    const editor::image::RgbaImage source = Flattened(editor::image::Resize(*pixels, columns * cellWidth, rows * cellHeight), background);

    ncplane_options options{};
    options.y    = box.y_min;
    options.x    = box.x_min;
    options.rows = static_cast<unsigned>(rows);
    options.cols = static_cast<unsigned>(columns);
    shown.plane  = ncplane_create(eventLoop_->StdPlane(), &options);
    if (shown.plane == nullptr) {
        return false;
    }
    ncvisual* visual = ncvisual_from_rgba(source.pixels.data(), source.height, source.width * 4, source.width);
    if (visual == nullptr) {
        ReleasePlane(shown);
        return false;
    }
    ncvisual_options visualOptions{};
    visualOptions.n       = shown.plane;
    visualOptions.scaling = NCSCALE_NONE;
    visualOptions.blitter = NCBLIT_PIXEL;
    ncplane* drawn        = ncvisual_blit(eventLoop_->NotcursesContext(), visual, &visualOptions);
    ncvisual_destroy(visual);
    if (drawn == nullptr) {
        ReleasePlane(shown);
        return false;
    }
    shown.plane = drawn;
    shown.box   = box;
    shown.shown = true;
    return true;
}

void InlineImages::EndFrame() {
    for (auto it = planes_.begin(); it != planes_.end();) {
        if (!it->second.shown) {
            ReleasePlane(it->second);
            it = planes_.erase(it);
            continue;
        }
        it->second.shown = false;
        ++it;
    }
}

void InlineImages::ReleasePlane(Plane& plane) {
    if (plane.plane != nullptr) {
        ncplane_destroy(plane.plane);
        plane.plane = nullptr;
    }
}

} // namespace ned::ui::acppanel
