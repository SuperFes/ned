//
// Standalone diagnostic, same spirit as NotcursesAlphaProbe.cpp -- not part
// of ned/ned_lib. Answers the real question behind a possible minimap
// feature: does this terminal support Notcurses' real pixel-graphics
// blitting (NCBLIT_PIXEL, via sixel/Kitty/iTerm2/etc.), and if so, does a
// genuine per-pixel image actually render with real fidelity? If yes, a
// minimap could rasterize actual zoomed-out text as a true small image; if
// the answer is NCPIXEL_NONE, a minimap has to fall back to a text-cell
// approximation (block/braille density) instead -- a materially different
// design, worth knowing before committing to either.
//
// Run it directly in a real terminal: ./build/notcurses_pixel_probe
// Reads one keypress, then exits and restores the terminal.
//

#include <cstdint>
#include <cstdio>
#include <vector>

#include <notcurses/notcurses.h>

namespace {

const char* PixelImplName(ncpixelimpl_e impl) {
    switch (impl) {
        case NCPIXEL_NONE:
            return "NONE -- no pixel graphics support detected; a minimap would need a text-cell approximation";
        case NCPIXEL_SIXEL:
            return "SIXEL";
        case NCPIXEL_LINUXFB:
            return "LINUX FRAMEBUFFER";
        case NCPIXEL_ITERM2:
            return "ITERM2";
        case NCPIXEL_KITTY_STATIC:
            return "KITTY (static)";
        case NCPIXEL_KITTY_ANIMATED:
            return "KITTY (animated)";
        case NCPIXEL_KITTY_SELFREF:
            return "KITTY (self-referential)";
        default:
            return "UNKNOWN";
    }
}

} // namespace

int main() {
    notcurses_options opts{};
    opts.flags = NCOPTION_NO_QUIT_SIGHANDLERS | NCOPTION_SUPPRESS_BANNERS;

    notcurses* nc = notcurses_core_init(&opts, nullptr);
    if (nc == nullptr) {
        std::fprintf(stderr, "notcurses_core_init failed -- are you running this in a real terminal?\n");
        return 1;
    }

    ncplane* std_plane = notcurses_stdplane(nc);
    ncplane_erase(std_plane);
    ncplane_set_fg_default(std_plane);
    ncplane_set_bg_default(std_plane);

    int y = 1;
    ncplane_putstr_yx(std_plane, y++, 2, "Notcurses pixel-graphics probe");
    y++;

    const ncpixelimpl_e impl = notcurses_check_pixel_support(nc);
    char line[256];
    std::snprintf(line, sizeof(line), "Detected pixel implementation: %s", PixelImplName(impl));
    ncplane_putstr_yx(std_plane, y++, 2, line);
    y += 2;

    if (impl != NCPIXEL_NONE) {
        ncplane_putstr_yx(std_plane, y++, 2,
                          "Below: a 160x80 real RGBA image (horizontal red/vertical green gradient + an 8px");
        ncplane_putstr_yx(std_plane, y++, 2, "checkerboard) blitted via NCBLIT_PIXEL -- genuine per-pixel data, not text cells.");
        y += 1;

        // ncvisual_from_rgba expects 'rows' lines of 'cols' 32-bit 8bpc RGBA
        // pixels each (R,G,B,A byte order in memory, not a packed native-
        // endian uint32) -- built as raw bytes to match that exactly rather
        // than risk an endianness-dependent packed-int layout.
        constexpr int width  = 160;
        constexpr int height = 80;
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4);
        for (int py = 0; py < height; ++py) {
            for (int px = 0; px < width; ++px) {
                std::uint8_t r = static_cast<std::uint8_t>(255 * px / width);
                std::uint8_t g = static_cast<std::uint8_t>(255 * py / height);
                std::uint8_t b = 128;
                if (((px / 8) + (py / 8)) % 2 == 0) {
                    r = static_cast<std::uint8_t>(r / 2);
                    g = static_cast<std::uint8_t>(g / 2);
                    b = static_cast<std::uint8_t>(b / 2);
                }
                const std::size_t idx = (static_cast<std::size_t>(py) * width + px) * 4;
                pixels[idx + 0]        = r;
                pixels[idx + 1]        = g;
                pixels[idx + 2]        = b;
                pixels[idx + 3]        = 255; // fully opaque
            }
        }

        ncvisual* visual = ncvisual_from_rgba(pixels.data(), height, width * 4, width);
        if (visual == nullptr) {
            ncplane_putstr_yx(std_plane, y++, 2, "ncvisual_from_rgba failed");
        }
        else {
            ncvisual_options vopts{};
            vopts.n       = nullptr; // let Notcurses create its own plane sized to fit
            vopts.scaling = NCSCALE_NONE;
            vopts.y       = y;
            vopts.x       = 2;
            vopts.blitter = NCBLIT_PIXEL;

            ncplane* drawn = ncvisual_blit(nc, visual, &vopts);
            if (drawn == nullptr) {
                ncplane_putstr_yx(std_plane, y++, 2, "ncvisual_blit(NCBLIT_PIXEL) failed -- support was detected but blitting didn't work");
            }
            else {
                unsigned drawnRows = 0;
                unsigned drawnCols = 0;
                ncplane_dim_yx(drawn, &drawnRows, &drawnCols);
                y += static_cast<int>(drawnRows) + 1;
            }
            ncvisual_destroy(visual);
        }
    }

    y += 2;
    ncplane_set_fg_default(std_plane);
    ncplane_set_bg_default(std_plane);
    ncplane_putstr_yx(std_plane, y, 2, "Press any key to exit...");

    notcurses_render(nc);

    ncinput ni;
    notcurses_get_blocking(nc, &ni);

    notcurses_stop(nc);
    return 0;
}
