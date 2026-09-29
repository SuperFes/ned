#include "Decode.h"

#include <algorithm>
#include <cmath>
#include <memory>

#include <png.h>
#include <turbojpeg.h>
#include <webp/decode.h>

namespace ned::editor::image {

namespace {

    bool Allowed(std::uint64_t width, std::uint64_t height) {
        return width > 0 && height > 0 && width * height <= kMaxImagePixels;
    }

    std::optional<RgbaImage> DecodePng(std::string_view bytes) {
        png_image png{};
        png.version = PNG_IMAGE_VERSION;
        if (png_image_begin_read_from_memory(&png, bytes.data(), bytes.size()) == 0) {
            return std::nullopt;
        }
        if (!Allowed(png.width, png.height)) {
            png_image_free(&png);
            return std::nullopt;
        }
        png.format = PNG_FORMAT_RGBA;
        RgbaImage image{.width = static_cast<int>(png.width), .height = static_cast<int>(png.height)};
        image.pixels.resize(PNG_IMAGE_SIZE(png));
        if (png_image_finish_read(&png, nullptr, image.pixels.data(), 0, nullptr) == 0) {
            png_image_free(&png);
            return std::nullopt;
        }
        return image;
    }

    std::optional<RgbaImage> DecodeJpeg(std::string_view bytes) {
        const std::unique_ptr<void, int (*)(tjhandle)> handle(tj3Init(TJINIT_DECOMPRESS), [](tjhandle h) {
            tj3Destroy(h);
            return 0;
        });
        if (!handle) {
            return std::nullopt;
        }
        const auto* data = reinterpret_cast<const unsigned char*>(bytes.data());
        if (tj3DecompressHeader(handle.get(), data, bytes.size()) != 0) {
            return std::nullopt;
        }
        const int width  = tj3Get(handle.get(), TJPARAM_JPEGWIDTH);
        const int height = tj3Get(handle.get(), TJPARAM_JPEGHEIGHT);
        if (width <= 0 || height <= 0 || !Allowed(static_cast<std::uint64_t>(width), static_cast<std::uint64_t>(height))) {
            return std::nullopt;
        }
        RgbaImage image{.width = width, .height = height};
        image.pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
        if (tj3Decompress8(handle.get(), data, bytes.size(), image.pixels.data(), 0, TJPF_RGBA) != 0) {
            return std::nullopt;
        }
        return image;
    }

    std::optional<RgbaImage> DecodeWebp(std::string_view bytes) {
        const auto* data   = reinterpret_cast<const std::uint8_t*>(bytes.data());
        int         width  = 0;
        int         height = 0;
        if (WebPGetInfo(data, bytes.size(), &width, &height) == 0 ||
            !Allowed(static_cast<std::uint64_t>(width), static_cast<std::uint64_t>(height))) {
            return std::nullopt;
        }
        RgbaImage image{.width = width, .height = height};
        image.pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
        if (WebPDecodeRGBAInto(data, bytes.size(), image.pixels.data(), image.pixels.size(), width * 4) == nullptr) {
            return std::nullopt;
        }
        return image;
    }

} // namespace

std::optional<RgbaImage> DecodeImage(std::string_view bytes) {
    if (bytes.starts_with("\x89PNG\r\n\x1a\n")) {
        return DecodePng(bytes);
    }
    if (bytes.starts_with("\xff\xd8\xff")) {
        return DecodeJpeg(bytes);
    }
    if (bytes.size() >= 12 && bytes.starts_with("RIFF") && bytes.substr(8, 4) == "WEBP") {
        return DecodeWebp(bytes);
    }
    return std::nullopt;
}

RgbaImage Resize(const RgbaImage& image, int width, int height) {
    RgbaImage out{.width = std::max(width, 0), .height = std::max(height, 0)};
    out.pixels.assign(static_cast<std::size_t>(out.width) * static_cast<std::size_t>(out.height) * 4, 0);
    if (image.width <= 0 || image.height <= 0 || out.pixels.empty()) {
        return out;
    }
    auto span = [](int index, int source, int dest) {
        const int first = static_cast<int>(static_cast<std::int64_t>(index) * source / dest);
        const int last  = static_cast<int>(static_cast<std::int64_t>(index + 1) * source / dest);
        return std::pair{first, std::max(last, first + 1)};
    };
    for (int y = 0; y < out.height; ++y) {
        const auto [y0, y1] = span(y, image.height, out.height);
        for (int x = 0; x < out.width; ++x) {
            const auto [x0, x1] = span(x, image.width, out.width);
            // Colour weighted by alpha, so transparent pixels don't darken
            // the edges they're averaged into.
            std::uint64_t r = 0, g = 0, b = 0, a = 0, count = 0;
            for (int sy = y0; sy < y1; ++sy) {
                const std::uint8_t* row = image.pixels.data() + static_cast<std::size_t>(sy) * static_cast<std::size_t>(image.width) * 4;
                for (int sx = x0; sx < x1; ++sx) {
                    const std::uint8_t* p     = row + static_cast<std::size_t>(sx) * 4;
                    const std::uint64_t alpha = p[3];
                    r += p[0] * alpha;
                    g += p[1] * alpha;
                    b += p[2] * alpha;
                    a += alpha;
                    ++count;
                }
            }
            std::uint8_t* q = out.pixels.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(out.width) + static_cast<std::size_t>(x)) * 4;
            if (a > 0) {
                q[0] = static_cast<std::uint8_t>(r / a);
                q[1] = static_cast<std::uint8_t>(g / a);
                q[2] = static_cast<std::uint8_t>(b / a);
            }
            q[3] = static_cast<std::uint8_t>(a / count);
        }
    }
    return out;
}

void Flatten(RgbaImage& image, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    for (std::size_t i = 0; i + 3 < image.pixels.size(); i += 4) {
        const unsigned alpha = image.pixels[i + 3];
        auto           blend = [alpha](std::uint8_t over, std::uint8_t under) {
            return static_cast<std::uint8_t>((over * alpha + under * (255 - alpha) + 127) / 255);
        };
        image.pixels[i + 0] = blend(image.pixels[i + 0], r);
        image.pixels[i + 1] = blend(image.pixels[i + 1], g);
        image.pixels[i + 2] = blend(image.pixels[i + 2], b);
        image.pixels[i + 3] = 255;
    }
}

CellFit FitToCells(int width, int height, int cellWidth, int cellHeight, int maxColumns, int maxRows) {
    if (width <= 0 || height <= 0 || cellWidth <= 0 || cellHeight <= 0 || maxColumns <= 0 || maxRows <= 0) {
        return {};
    }
    // Rows for a given width, and width for a given number of rows, at the
    // image's aspect once a cell's own shape is taken into account.
    auto rowsFor = [&](int columns) {
        return std::max(1, static_cast<int>(std::lround(static_cast<double>(columns) * cellWidth * height / (static_cast<double>(width) * cellHeight))));
    };
    auto columnsFor = [&](int rows) {
        return std::max(1, static_cast<int>(std::lround(static_cast<double>(rows) * cellHeight * width / (static_cast<double>(height) * cellWidth))));
    };
    int columns = std::min(maxColumns, (width + cellWidth - 1) / cellWidth);
    int rows    = rowsFor(columns);
    if (rows > maxRows) {
        rows    = maxRows;
        columns = std::min(columns, columnsFor(rows));
    }
    return {.columns = columns, .rows = rows};
}

} // namespace ned::editor::image
