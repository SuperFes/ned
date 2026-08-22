//
// Standalone diagnostic, same spirit as NotcursesPixelProbe.cpp -- not part
// of ned/ned_lib. NotcursesPixelProbe.cpp already confirmed this terminal's
// pixel-graphics support in the abstract (a big, generously-sized test
// image blitted straight into a CHILDPLANE of the standard plane). This
// probe instead exercises the *exact* code path Source/UI/Minimap.cpp's
// EnsurePlane() actually uses for the real minimap -- a small,
// explicitly-owned plane (ncplane_create, sized in cells) reblitted in
// place via NCSCALE_NONE -- at the same tiny cell footprint (5 wide) a real
// minimap column uses, since "a big image blits fine" and "a 5-cell-wide
// image blits fine" turned out to be different questions in practice
// (though not for the reason it first looked like -- see Minimap.h's own
// header comment for the real story: a project-level build bug, this
// project's own CXX_INCLUDES silently resolving <notcurses/notcurses.h> to
// the system's installed notcurses instead of the vendored/pinned one, not
// a real notcurses defect).
//
// Renders four panels so a fuzzy/tiny/absent pixel image is directly
// comparable against the same content drawn via Notcurses' own non-pixel
// blitters (NCBLIT_2x2, NCBLIT_BRAILLE) -- which need no terminal
// graphics-protocol support at all -- and against a much wider NCBLIT_PIXEL
// panel, rather than having to eyeball "is that smudge on screen actually
// an image" in isolation.
//
// Run it directly in a real terminal: ./build/notcurses_minimap_probe
// Reads one keypress, then exits and restores the terminal.
//

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include <notcurses/notcurses.h>

namespace {

// A small, high-contrast, unambiguous test pattern -- concentric colored
// rings on a dark background, chosen specifically so "is this a real image
// or scattered noise" is obvious even shrunk to a handful of cells: a
// blurry/aliased version of concentric rings still reads as rings, not as
// a field of disconnected specks the way sparse text content might.
std::vector<std::uint8_t> BuildRingImage(int width, int height) {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4);
    const double              cx = width / 2.0;
    const double              cy = height / 2.0;
    const double              maxR = std::min(cx, cy);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const double dx = x - cx;
            const double dy = y - cy;
            const double r  = std::sqrt(dx * dx + dy * dy) / maxR;
            const int    ring = static_cast<int>(r * 6.0) % 2;
            std::uint8_t red, green, blue;
            if (r > 1.0) {
                red = 20;
                green = 20;
                blue = 30;
            }
            else if (ring == 0) {
                red = 255;
                green = 80;
                blue = 80;
            }
            else {
                red = 80;
                green = 160;
                blue = 255;
            }
            const std::size_t idx = (static_cast<std::size_t>(y) * width + x) * 4;
            pixels[idx + 0]        = red;
            pixels[idx + 1]        = green;
            pixels[idx + 2]        = blue;
            pixels[idx + 3]        = 255;
        }
    }
    return pixels;
}

void Progress(const char* what) {
    std::fprintf(stderr, "[minimap-probe] %s\n", what);
    std::fflush(stderr);
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
    Progress("notcurses_core_init succeeded");

    ncplane* std_plane = notcurses_stdplane(nc);
    ncplane_erase(std_plane);
    ncplane_set_fg_default(std_plane);
    ncplane_set_bg_default(std_plane);

    int y = 1;
    ncplane_putstr_yx(std_plane, y++, 2, "Minimap-plane probe -- three 5-cell-wide columns, same ring image:");
    y++;

    const ncpixelimpl_e impl = notcurses_check_pixel_support(nc);
    char                 line[256];
    std::snprintf(line, sizeof(line), "Detected pixel implementation: %d (0 == NCPIXEL_NONE)", static_cast<int>(impl));
    ncplane_putstr_yx(std_plane, y++, 2, line);

    unsigned celldimy = 0, celldimx = 0;
    ncplane_pixel_geom(std_plane, nullptr, nullptr, &celldimy, &celldimx, nullptr, nullptr);
    std::snprintf(line, sizeof(line), "Cell-pixel geometry: %ux%u (h x w)", celldimy, celldimx);
    ncplane_putstr_yx(std_plane, y++, 2, line);
    y += 2;
    notcurses_render(nc);
    Progress(line);

    const int labelY   = y;
    const int panelY   = y + 1;
    const int panelRows = 20;
    const int panelCols = 5;

    ncplane_putstr_yx(std_plane, labelY, 2, "NCBLIT_PIXEL, owned");
    ncplane_putstr_yx(std_plane, labelY, 2 + panelCols + 4, "NCBLIT_QUADRANT");
    ncplane_putstr_yx(std_plane, labelY, 2 + 2 * (panelCols + 4), "NCBLIT_BRAILLE");
    notcurses_render(nc);

    // Panel 1: NCBLIT_PIXEL into a plane we own outright (ncplane_create,
    // sized in cells), reblitted via NCSCALE_NONE with a source image built
    // to exactly match this plane's real cell-pixel footprint -- the exact
    // Minimap::EnsurePlane() approach for real-pixel-graphics mode.
    {
        const int imgW = panelCols * (celldimx > 0 ? static_cast<int>(celldimx) : 8);
        const int imgH = panelRows * (celldimy > 0 ? static_cast<int>(celldimy) : 16);
        const std::vector<std::uint8_t> pixels = BuildRingImage(imgW, imgH);

        ncplane_options nopts{};
        nopts.y    = panelY;
        nopts.x    = 2;
        nopts.rows = panelRows;
        nopts.cols = panelCols;
        ncplane* plane = ncplane_create(std_plane, &nopts);
        Progress(plane == nullptr ? "ncplane_create (panel 1) failed" : "ncplane_create (panel 1) succeeded");

        if (plane != nullptr) {
            ncvisual* visual = ncvisual_from_rgba(pixels.data(), imgH, imgW * 4, imgW);
            if (visual != nullptr) {
                // NCSCALE_NONE follow-up: every prior attempt used
                // NCSCALE_STRETCH, whose scaling math (shape_sprixel_plane)
                // is what repeatedly produced wrong results. NCSCALE_NONE's
                // own code path is much simpler -- it just uses the
                // visual's true native pixel size directly, no
                // ncplane_dim_yx(vopts->n)-derived computation at all --
                // and imgW/imgH here are already built to exactly match
                // this plane's real cell-pixel footprint, so no actual
                // scaling should be needed either way.
                ncvisual_options vopts{};
                vopts.n       = plane;
                vopts.scaling = NCSCALE_NONE;
                vopts.blitter = NCBLIT_PIXEL;

                // ncvisual_geom(): the exact same geometry computation
                // ncvisual_blit() itself uses internally to decide how big
                // to resize our plane -- querying it directly here, before
                // actually blitting, tells us precisely what Notcurses
                // believes vs. what we asked for, instead of inferring it
                // after the fact from a wrong-sized result.
                ncvgeom geom{};
                if (ncvisual_geom(nc, visual, &vopts, &geom) == 0) {
                    char gbuf[256];
                    std::snprintf(gbuf, sizeof(gbuf),
                                  "ncvisual_geom (panel 1): blitter=%d pixy/pixx=%ux%u cdimy/cdimx=%ux%u rpixy/rpixx=%ux%u rcelly/rcellx=%ux%u scaley/scalex=%ux%u maxbmap=%ux%u",
                                  static_cast<int>(geom.blitter), geom.pixy, geom.pixx, geom.cdimy, geom.cdimx,
                                  geom.rpixy, geom.rpixx, geom.rcelly, geom.rcellx, geom.scaley, geom.scalex,
                                  geom.maxpixely, geom.maxpixelx);
                    Progress(gbuf);
                }
                else {
                    Progress("ncvisual_geom (panel 1) failed");
                }

                ncplane* drawn = ncvisual_blit(nc, visual, &vopts);
                if (drawn == nullptr) {
                    Progress("ncvisual_blit (panel 1, NCBLIT_PIXEL, NCSCALE_NONE) failed");
                }
                else {
                    unsigned dr = 0, dc = 0;
                    ncplane_dim_yx(drawn, &dr, &dc);
                    char buf[128];
                    std::snprintf(buf, sizeof(buf),
                                  "ncvisual_blit (panel 1, NCBLIT_PIXEL, NCSCALE_NONE) succeeded, plane is %u rows x %u cols (wanted %d x %d)",
                                  dr, dc, panelRows, panelCols);
                    Progress(buf);
                }
                ncvisual_destroy(visual);
            }
            else {
                Progress("ncvisual_from_rgba (panel 1) failed");
            }
        }
    }
    notcurses_render(nc);
    Progress("render done (panel 1)");

    // Panel 2: same ring image, same cell footprint, but via NCBLIT_QUADRANT
    // -- a pure text-cell blitter (2x2 sub-cells per glyph via Unicode
    // quadrant block characters), needs zero terminal graphics-protocol
    // support. If this looks like real rings and panel 1 doesn't, the gap
    // is specifically in pixel-graphics delivery, not in the image data or
    // plane sizing.
    {
        const int imgW = panelCols * 2;
        const int imgH = panelRows * 2;
        const std::vector<std::uint8_t> pixels = BuildRingImage(imgW, imgH);

        ncplane_options nopts{};
        nopts.y    = panelY;
        nopts.x    = 2 + panelCols + 4;
        nopts.rows = panelRows;
        nopts.cols = panelCols;
        ncplane* plane = ncplane_create(std_plane, &nopts);
        if (plane != nullptr) {
            ncvisual* visual = ncvisual_from_rgba(pixels.data(), imgH, imgW * 4, imgW);
            if (visual != nullptr) {
                ncvisual_options vopts{};
                vopts.n       = plane;
                vopts.scaling = NCSCALE_STRETCH;
                vopts.blitter = NCBLIT_2x2;
                ncplane* drawn = ncvisual_blit(nc, visual, &vopts);
                Progress(drawn == nullptr ? "ncvisual_blit (panel 2, NCBLIT_2x2) failed"
                                          : "ncvisual_blit (panel 2, NCBLIT_2x2) succeeded");
                ncvisual_destroy(visual);
            }
        }
    }
    notcurses_render(nc);
    Progress("render done (panel 2)");

    // Panel 3: same again via NCBLIT_BRAILLE (2x4 sub-cells per glyph) --
    // the closest built-in Notcurses equivalent to this project's own
    // hand-rolled braille minimap renderer, for a direct density/resolution
    // comparison against panel 1.
    {
        const int imgW = panelCols * 2;
        const int imgH = panelRows * 4;
        const std::vector<std::uint8_t> pixels = BuildRingImage(imgW, imgH);

        ncplane_options nopts{};
        nopts.y    = panelY;
        nopts.x    = 2 + 2 * (panelCols + 4);
        nopts.rows = panelRows;
        nopts.cols = panelCols;
        ncplane* plane = ncplane_create(std_plane, &nopts);
        if (plane != nullptr) {
            ncvisual* visual = ncvisual_from_rgba(pixels.data(), imgH, imgW * 4, imgW);
            if (visual != nullptr) {
                ncvisual_options vopts{};
                vopts.n       = plane;
                vopts.scaling = NCSCALE_STRETCH;
                vopts.blitter = NCBLIT_BRAILLE;
                ncplane* drawn = ncvisual_blit(nc, visual, &vopts);
                Progress(drawn == nullptr ? "ncvisual_blit (panel 3, NCBLIT_BRAILLE) failed"
                                          : "ncvisual_blit (panel 3, NCBLIT_BRAILLE) succeeded");
                ncvisual_destroy(visual);
            }
        }
    }
    notcurses_render(nc);
    Progress("render done (panel 3)");

    // Panel 4: width-isolation follow-up. Every NCBLIT_PIXEL attempt so far
    // (CHILDPLANE auto-sizing, owned-plane+STRETCH, owned-plane+NCSCALE_NONE)
    // failed specifically at a real minimap's 5-cell width, while a much
    // larger (160x80px, i.e. plenty of cells wide) test image blitted fine
    // (NotcursesPixelProbe.cpp). Same owned-plane+NCSCALE_NONE approach as
    // panel 1, run again here at 30 cells wide instead of 5 -- isolates
    // whether *narrowness itself* is what triggers the bug, independent of
    // which scaling mode is used.
    {
        const int wideCols = 30;
        const int wideRows = panelRows;
        const int wideY    = panelY + panelRows + 2;
        ncplane_putstr_yx(std_plane, wideY - 1, 2, "NCBLIT_PIXEL, owned, 30 cells wide (width-isolation test)");

        const int imgW = wideCols * (celldimx > 0 ? static_cast<int>(celldimx) : 8);
        const int imgH = wideRows * (celldimy > 0 ? static_cast<int>(celldimy) : 16);
        const std::vector<std::uint8_t> pixels = BuildRingImage(imgW, imgH);

        ncplane_options nopts{};
        nopts.y    = wideY;
        nopts.x    = 2;
        nopts.rows = wideRows;
        nopts.cols = wideCols;
        ncplane* plane = ncplane_create(std_plane, &nopts);
        Progress(plane == nullptr ? "ncplane_create (panel 4, wide) failed" : "ncplane_create (panel 4, wide) succeeded");

        if (plane != nullptr) {
            ncvisual* visual = ncvisual_from_rgba(pixels.data(), imgH, imgW * 4, imgW);
            if (visual != nullptr) {
                ncvisual_options vopts{};
                vopts.n       = plane;
                vopts.scaling = NCSCALE_NONE;
                vopts.blitter = NCBLIT_PIXEL;

                ncvgeom geom{};
                if (ncvisual_geom(nc, visual, &vopts, &geom) == 0) {
                    char gbuf[256];
                    std::snprintf(gbuf, sizeof(gbuf),
                                  "ncvisual_geom (panel 4): blitter=%d pixy/pixx=%ux%u cdimy/cdimx=%ux%u rpixy/rpixx=%ux%u rcelly/rcellx=%ux%u scaley/scalex=%ux%u maxbmap=%ux%u",
                                  static_cast<int>(geom.blitter), geom.pixy, geom.pixx, geom.cdimy, geom.cdimx,
                                  geom.rpixy, geom.rpixx, geom.rcelly, geom.rcellx, geom.scaley, geom.scalex,
                                  geom.maxpixely, geom.maxpixelx);
                    Progress(gbuf);
                }
                else {
                    Progress("ncvisual_geom (panel 4) failed");
                }

                ncplane* drawn = ncvisual_blit(nc, visual, &vopts);
                if (drawn == nullptr) {
                    Progress("ncvisual_blit (panel 4, wide, NCSCALE_NONE) failed");
                }
                else {
                    unsigned dr = 0, dc = 0;
                    ncplane_dim_yx(drawn, &dr, &dc);
                    char buf[128];
                    std::snprintf(buf, sizeof(buf),
                                  "ncvisual_blit (panel 4, wide, NCSCALE_NONE) succeeded, plane is %u rows x %u cols (wanted %d x %d)",
                                  dr, dc, wideRows, wideCols);
                    Progress(buf);
                }
                ncvisual_destroy(visual);
            }
        }
    }
    notcurses_render(nc);
    Progress("render done (panel 4), press any key to exit");

    ncinput ni;
    notcurses_get_blocking(nc, &ni);

    notcurses_stop(nc);
    Progress("notcurses_stop done, exiting cleanly");
    return 0;
}
