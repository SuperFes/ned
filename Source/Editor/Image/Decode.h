//
// Images as RGBA pixels, for Notcurses to draw: decoding the formats an ACP
// agent or a clipboard paste hands over (PNG, JPEG, WebP), resizing to the
// raster a blitter wants, and fitting an image into a box of terminal cells.
//

#ifndef NED_EDITOR_IMAGE_DECODE_H
#define NED_EDITOR_IMAGE_DECODE_H

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace ned::editor::image {

// Row-major, four bytes (R, G, B, A) per pixel.
struct RgbaImage {
    int                       width  = 0;
    int                       height = 0;
    std::vector<std::uint8_t> pixels;
};

// Larger images are refused rather than decoded: the pixels alone would
// be 160 MB.
inline constexpr std::uint64_t kMaxImagePixels = 40'000'000;

// Decodes a PNG, JPEG or WebP, recognised by its leading bytes; nullopt for
// any other format, a corrupt image, or one over kMaxImagePixels.
[[nodiscard]] std::optional<RgbaImage> DecodeImage(std::string_view bytes);

// `image` resized to exactly width x height: each destination pixel
// averages the source pixels it covers, so shrinking doesn't alias.
[[nodiscard]] RgbaImage Resize(const RgbaImage& image, int width, int height);

// Composites every pixel over an opaque background, for a surface that
// can't show transparency.
void Flatten(RgbaImage& image, std::uint8_t r, std::uint8_t g, std::uint8_t b);

// A box of terminal cells.
struct CellFit {
    int columns = 0;
    int rows    = 0;
};

// The largest box within maxColumns x maxRows showing a width x height
// image at its own aspect, given a cell's size in pixels -- and never wider
// than the image's own pixels fill, so a small image stays small.
[[nodiscard]] CellFit FitToCells(int width, int height, int cellWidth, int cellHeight, int maxColumns, int maxRows);

} // namespace ned::editor::image

#endif // NED_EDITOR_IMAGE_DECODE_H
