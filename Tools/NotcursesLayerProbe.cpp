//
// Standalone diagnostic, same shape as the other Notcurses probes -- not
// part of ned/ned_lib, linked straight against notcurses-core.
//
// It answers one question the cell-model reasoning cannot settle on its
// own: can *layered planes* produce a background that is translucent to
// whatever is behind the terminal window, rather than to Notcurses' own
// idea of a backdrop?
//
// The claim under test is that they cannot -- that whatever a stack of
// planes composites to, each cell still reaches the terminal as one glyph
// with one background, and Konsole only lets a cell through to the desktop
// when that background is *default*. If that claim is wrong, a translucent
// current-line highlight and a translucent sticky-header band both become
// possible, so it is worth being sure rather than reasoning about it.
//
// Six panels, each drawn from real ncplanes in a known z-order:
//
//   A  opaque plane under a transparent-background text plane   (control:
//      does layering work at all?)
//   B  NCALPHA_BLEND plane over nothing, text plane above it    (the crux:
//      desktop, or blended with black?)
//   C  default-background plane under text                      (control:
//      what ned does today -- the desktop must show)
//   D  BLEND planes stacked 1/2/3 deep over nothing             (does depth
//      approach the desktop, or approach black?)
//   E  an RGBA image with 50% alpha UNDER a text plane          (the one
//      that would change the answer: real per-pixel alpha, text on top)
//   F  the same image with no text over it                      (control
//      for E -- does the image alpha reach the desktop at all?)
//
// Run it in a terminal with a *translucent* background, or the whole thing
// is moot: ./build/notcurses_layer_probe
//
// Renders incrementally -- a notcurses_render() after each panel rather than
// one at the end -- specifically so that if a later panel dies, whatever
// already succeeded stays on screen instead of the whole probe appearing to
// do nothing. The image panels are the fragile ones (a terminal can report a
// pixel backend and still fail the blit), so they come last and progress is
// mirrored to stderr: ./build/notcurses_layer_probe 2>/tmp/layer.err
//
// --skip-images leaves them out entirely; --force-images runs them even where
// no pixel backend is reported. Reads one keypress, then exits and restores
// the terminal.
//

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <notcurses/notcurses.h>

namespace {

// Mirrors this probe's own progress on stderr, flushed immediately -- if a
// later step crashes or the terminal is left mid-escape, this may be the only
// trace of how far execution actually got. Capture it separately:
//     ./build/notcurses_layer_probe 2>/tmp/layer.err
void Progress(const char* what) {
    std::fprintf(stderr, "[layer-probe] %s\n", what);
    std::fflush(stderr);
}

struct Rgb {
    int r;
    int g;
    int b;
};

void Label(ncplane* plane, int y, int x, const char* text) {
    ncplane_set_fg_rgb8(plane, 210, 210, 220);
    ncplane_set_bg_default(plane);
    ncplane_putstr_yx(plane, y, x, text);
}

void Dim(ncplane* plane, int y, int x, const char* text) {
    ncplane_set_fg_rgb8(plane, 130, 130, 145);
    ncplane_set_bg_default(plane);
    ncplane_putstr_yx(plane, y, x, text);
}

ncplane* MakePlane(ncplane* parent, int y, int x, int rows, int cols, std::vector<ncplane*>& owned) {
    ncplane_options opts{};
    opts.y    = y;
    opts.x    = x;
    opts.rows = static_cast<unsigned>(rows);
    opts.cols = static_cast<unsigned>(cols);

    ncplane* plane = ncplane_create(parent, &opts);
    if (plane != nullptr) {
        owned.push_back(plane);
    }
    return plane;
}

// A plane filled with one background, at the requested alpha. Spaces are
// written explicitly rather than left to the base cell so the fill is a real
// cell-by-cell paint, exactly like ned's own Screen::Flush produces.
ncplane* FilledPlane(ncplane* parent, int y, int x, int rows, int cols, Rgb colour, unsigned alpha,
                     std::vector<ncplane*>& owned) {
    ncplane* plane = MakePlane(parent, y, x, rows, cols, owned);
    if (plane == nullptr) {
        return nullptr;
    }
    ncplane_set_bg_rgb8(plane, colour.r, colour.g, colour.b);
    ncplane_set_bg_alpha(plane, alpha);
    ncplane_set_fg_default(plane);
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            ncplane_putchar_yx(plane, row, col, ' ');
        }
    }
    return plane;
}

// A text plane whose cells carry NO background of their own -- the layering
// question in its purest form: does what is beneath show through?
ncplane* TextPlane(ncplane* parent, int y, int x, int cols, const char* text, std::vector<ncplane*>& owned) {
    ncplane* plane = MakePlane(parent, y, x, 1, cols, owned);
    if (plane == nullptr) {
        return nullptr;
    }
    ncplane_set_bg_alpha(plane, NCALPHA_TRANSPARENT);
    ncplane_set_fg_rgb8(plane, 255, 255, 255);
    ncplane_putstr_yx(plane, 0, 0, text);
    return plane;
}

const char* PixelImplName(ncpixelimpl_e impl) {
    switch (impl) {
        case NCPIXEL_NONE:
            return "NONE";
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

int main(int argc, char** argv) {
    // --force-images runs the image panels even where Notcurses reports no
    // pixel backend. They will fail or degrade, which is the point: it is how
    // that path gets exercised somewhere other than the one terminal that
    // supports it.
    bool forceImages = false;
    bool skipImages  = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--force-images") == 0) {
            forceImages = true;
        }
        else if (std::strcmp(argv[i], "--skip-images") == 0) {
            skipImages = true;
        }
        else {
            std::fprintf(stderr, "usage: %s [--force-images] [--skip-images]\n", argv[0]);
            return 2;
        }
    }

    notcurses_options opts{};
    opts.flags = NCOPTION_NO_QUIT_SIGHANDLERS | NCOPTION_SUPPRESS_BANNERS;

    notcurses* nc = notcurses_core_init(&opts, nullptr);
    if (nc == nullptr) {
        std::fprintf(stderr, "notcurses_core_init failed -- are you running this in a real terminal?\n");
        return 1;
    }

    ncplane* std_plane = notcurses_stdplane(nc);
    ncplane_erase(std_plane);

    unsigned rows = 0;
    unsigned cols = 0;
    ncplane_dim_yx(std_plane, &rows, &cols);

    std::vector<ncplane*> owned;
    const int             panelWidth = static_cast<int>(cols) > 76 ? 72 : static_cast<int>(cols) - 4;

    Label(std_plane, 0, 2, "Notcurses plane-layering probe -- run this in a TRANSLUCENT terminal window.");
    Dim(std_plane, 1, 2, "The question: can layered planes make a background see-through to the DESKTOP,");
    Dim(std_plane, 2, 2, "or only to Notcurses' own idea of what is beneath? C and F are the controls.");

    int y = 4;
    notcurses_render(nc); // whatever follows, the header is already visible
    Progress("header rendered");

    // --- A: does layering work at all? -----------------------------------
    Label(std_plane, y, 2, "A) opaque plane, text plane above it (control: layering itself)");
    FilledPlane(std_plane, y + 1, 2, 1, panelWidth, Rgb{40, 70, 120}, NCALPHA_OPAQUE, owned);
    TextPlane(std_plane, y + 1, 4, panelWidth - 4, "text on an opaque plane -- must be legible on solid blue", owned);
    y += 3;
    notcurses_render(nc);
    Progress("panel A rendered");

    // --- B: the crux -----------------------------------------------------
    Label(std_plane, y, 2, "B) NCALPHA_BLEND plane over NOTHING, text above it");
    FilledPlane(std_plane, y + 1, 2, 1, panelWidth, Rgb{40, 200, 90}, NCALPHA_BLEND, owned);
    TextPlane(std_plane, y + 1, 4, panelWidth - 4, "if this shows your DESKTOP tinted green, layering wins", owned);
    Dim(std_plane, y + 2, 2, "   desktop through the green => layering solves it. Dark/olive green => blended with black.");
    y += 4;
    notcurses_render(nc);
    Progress("panel B rendered");

    // --- C: what ned does today ------------------------------------------
    Label(std_plane, y, 2, "C) default-background plane under text (control: today's behaviour)");
    FilledPlane(std_plane, y + 1, 2, 1, panelWidth, Rgb{0, 0, 0}, NCALPHA_TRANSPARENT, owned);
    TextPlane(std_plane, y + 1, 4, panelWidth - 4, "your desktop MUST show through here -- no colour is painted", owned);
    y += 3;
    notcurses_render(nc);
    Progress("panel C rendered");

    // --- D: does depth help? ---------------------------------------------
    Label(std_plane, y, 2, "D) BLEND stacked 1 / 2 / 3 deep over nothing");
    for (int depth = 1; depth <= 3; ++depth) {
        const int x = 2 + (depth - 1) * (panelWidth / 3);
        for (int layer = 0; layer < depth; ++layer) {
            FilledPlane(std_plane, y + 1, x, 1, panelWidth / 3 - 2, Rgb{40, 200, 90}, NCALPHA_BLEND, owned);
        }
        char caption[32];
        std::snprintf(caption, sizeof(caption), " depth %d", depth);
        Dim(std_plane, y + 2, x, caption);
    }
    y += 4;

    // --- E/F: real per-pixel alpha, under and over text -------------------
    const ncpixelimpl_e impl = notcurses_check_pixel_support(nc);
    char                capability[128];
    std::snprintf(capability, sizeof(capability), "E/F) pixel backend: %s", PixelImplName(impl));
    Label(std_plane, y, 2, capability);

    if (skipImages || (impl == NCPIXEL_NONE && !forceImages)) {
        Dim(std_plane, y + 1, 2,
            skipImages ? "   image tests skipped (--skip-images) -- B and D are the ones that matter anyway."
                       : "   no pixel graphics here, so the image tests cannot run -- see B and D above.");
        y += 3;
    }
    else {
        unsigned celly = 0;
        unsigned cellx = 0;
        ncplane_pixel_geom(std_plane, nullptr, nullptr, &celly, &cellx, nullptr, nullptr);
        char geometry[96];
        std::snprintf(geometry, sizeof(geometry), "cell pixel geometry: %ux%u", cellx, celly);
        Progress(geometry);

        const int imageCells = panelWidth / 2;
        const int pxW        = imageCells * static_cast<int>(cellx);
        const int pxH        = static_cast<int>(celly);

        // A terminal can report a pixel backend and still hand back a zero
        // cell geometry; building a zero-sized RGBA buffer from that would
        // hand ncvisual_from_rgba a null pointer.
        if (pxW <= 0 || pxH <= 0) {
            Dim(std_plane, y + 1, 2, "   pixel backend reported, but cell geometry is 0 -- image tests skipped.");
            Progress("cell geometry was zero; skipping image panels");
            y += 3;
            goto finish;
        }

        // Half-alpha magenta: if per-pixel alpha reaches the desktop, this
        // reads as a tint over whatever is behind the window.
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(pxW) * pxH * 4);
        for (std::size_t i = 0; i < pixels.size(); i += 4) {
            pixels[i + 0] = 225;
            pixels[i + 1] = 70;
            pixels[i + 2] = 190;
            pixels[i + 3] = 128;
        }

        for (int variant = 0; variant < 2; ++variant) {
            const int  row      = y + 1 + variant * 2;
            const bool withText = variant == 0;

            ncvisual* visual = ncvisual_from_rgba(pixels.data(), pxH, pxW * 4, pxW);
            if (visual == nullptr) {
                Dim(std_plane, row, 2, "   ncvisual_from_rgba failed");
                continue;
            }
            ncvisual_options vopts{};
            vopts.n       = std_plane;
            vopts.y       = row;
            vopts.x       = 2;
            vopts.scaling = NCSCALE_NONE;
            vopts.blitter = NCBLIT_PIXEL;
            vopts.flags   = NCVISUAL_OPTION_CHILDPLANE;

            Progress(withText ? "blitting image for panel E" : "blitting image for panel F");
            ncplane* drawn = ncvisual_blit(nc, visual, &vopts);
            ncvisual_destroy(visual);
            Progress(drawn == nullptr ? "ncvisual_blit returned nullptr" : "ncvisual_blit succeeded");
            if (drawn == nullptr) {
                Dim(std_plane, row, 2, "   ncvisual_blit(NCBLIT_PIXEL) failed");
                continue;
            }
            owned.push_back(drawn);

            if (withText) {
                // The one that matters: a text plane created *after* the
                // image, so it sits above it, with no background of its own.
                TextPlane(std_plane, row, 4, imageCells - 4, "text over a 50% image", owned);
                Dim(std_plane, row + 1, 2 + imageCells,
                    " E) text over a half-alpha image: legible AND desktop visible?");
            }
            else {
                Dim(std_plane, row + 1, 2 + imageCells, " F) the same image with no text over it (control)");
            }
        }
        y += 5;
    }

finish:
    Dim(std_plane, static_cast<int>(rows) - 2, 2,
        "If B shows the desktop, ned can have translucent bands. If only C does, the cell model is the limit.");
    Label(std_plane, static_cast<int>(rows) - 1, 2, "Press any key to exit...");

    notcurses_render(nc);
    Progress("all panels rendered; waiting for a keypress");

    // Wait for a real key. A release event or a resize is not a keypress, and
    // treating one as such is how a probe exits before anyone has read it.
    ncinput ni;
    while (true) {
        const uint32_t key = notcurses_get_blocking(nc, &ni);
        if (key == static_cast<uint32_t>(-1)) {
            Progress("notcurses_get_blocking failed; exiting");
            break;
        }
        if (ni.evtype == NCTYPE_RELEASE || key == NCKEY_RESIZE || key == 0) {
            continue;
        }
        break;
    }

    for (ncplane* plane : owned) {
        ncplane_destroy(plane);
    }
    notcurses_stop(nc);
    return 0;
}
