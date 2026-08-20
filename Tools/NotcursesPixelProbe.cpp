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
// Renders incrementally (a notcurses_render() call after each step, not
// just once at the end) specifically so that if a later step crashes
// outright, whatever already succeeded is still visible on screen instead
// of the whole probe appearing to "do nothing" -- notcurses_render() is
// the only point any of this actually reaches the terminal; anything
// drawn before a crash without an intervening render() is invisible, lost
// along with the process.
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

// Mirrors this probe's own stdout progress markers on stderr too, flushed
// immediately -- if the terminal itself gets left in a bad state by a
// crash inside Notcurses (raw mode never restored, garbled escape
// sequences mid-flight), these may be the only trace of how far execution
// actually got, especially if stdout is what's on the now-corrupted
// terminal. `! command 2>/tmp/probe.log` (or similar) captures this
// separately from whatever the terminal itself shows.
void Progress(const char* what) {
    std::fprintf(stderr, "[probe] %s\n", what);
    std::fflush(stderr);
}

} // namespace

int main() {
    Progress("starting notcurses_core_init");

    notcurses_options opts{};
    opts.flags = NCOPTION_NO_QUIT_SIGHANDLERS | NCOPTION_SUPPRESS_BANNERS;

    notcurses* nc = notcurses_core_init(&opts, nullptr);
    if (nc == nullptr) {
        std::fprintf(stderr, "notcurses_core_init failed -- are you running this in a real terminal?\n");
        return 1;
    }
    Progress("notcurses_core_init succeeded");

    ncplane* std_plane = notcurses_stdplane(nc);
    ncplane_erase(std_plane);
    ncplane_set_fg_default(std_plane);
    ncplane_set_bg_default(std_plane);

    int y = 1;
    ncplane_putstr_yx(std_plane, y++, 2, "Notcurses pixel-graphics probe");
    y++;
    notcurses_render(nc); // render #1 -- guarantees this much is visible no matter what happens next
    Progress("render #1 done (banner)");

    const ncpixelimpl_e impl = notcurses_check_pixel_support(nc);
    char line[256];
    std::snprintf(line, sizeof(line), "Detected pixel implementation: %s", PixelImplName(impl));
    ncplane_putstr_yx(std_plane, y++, 2, line);
    y += 2;
    notcurses_render(nc); // render #2 -- guarantees the detected implementation is visible even if the blit below crashes
    Progress(line);

    if (impl != NCPIXEL_NONE) {
        ncplane_putstr_yx(std_plane, y++, 2,
                          "Below: a 160x80 real RGBA image (horizontal red/vertical green gradient + an 8px");
        ncplane_putstr_yx(std_plane, y++, 2, "checkerboard) blitted via NCBLIT_PIXEL -- genuine per-pixel data, not text cells.");
        y += 1;
        notcurses_render(nc); // render #3 -- these two lines visible before the risky part even starts
        Progress("render #3 done (about to build+blit the test image)");

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
        Progress("test image buffer built, calling ncvisual_from_rgba");

        ncvisual* visual = ncvisual_from_rgba(pixels.data(), height, width * 4, width);
        Progress(visual == nullptr ? "ncvisual_from_rgba returned nullptr" : "ncvisual_from_rgba succeeded, calling ncvisual_blit");
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
            Progress(drawn == nullptr ? "ncvisual_blit returned nullptr" : "ncvisual_blit succeeded");
            if (drawn == nullptr) {
                ncplane_putstr_yx(std_plane, y++, 2, "ncvisual_blit(NCBLIT_PIXEL) failed -- support was detected but blitting didn't work");
            }
            else {
                unsigned drawnRows = 0;
                unsigned drawnCols = 0;
                ncplane_dim_yx(drawn, &drawnRows, &drawnCols);
                y += static_cast<int>(drawnRows) + 1;
            }
            notcurses_render(nc); // render #4 -- the actual pixel image (or the failure message), before doing anything else
            Progress("render #4 done (blit result)");
            ncvisual_destroy(visual);
            Progress("ncvisual_destroy done");
        }
    }

    y += 2;
    ncplane_set_fg_default(std_plane);
    ncplane_set_bg_default(std_plane);
    ncplane_putstr_yx(std_plane, y, 2, "Press any key to exit...");

    notcurses_render(nc); // render #5 -- final
    Progress("render #5 done (final) -- waiting for a keypress");

    ncinput ni;
    notcurses_get_blocking(nc, &ni);

    notcurses_stop(nc);
    Progress("notcurses_stop done, exiting cleanly");
    return 0;
}
