//
// Standalone diagnostic, same shape as NotcursesAlphaProbe.cpp /
// NotcursesPixelProbe.cpp / NotcursesMinimapProbe.cpp -- not part of
// ned/ned_lib, linked directly against notcurses-core.
//
// NotcursesAlphaProbe answered one narrow question (can NCALPHA_BLEND
// composite against a truly unknown terminal backdrop?). This one answers
// the broader set that question opened up, all of which matter before any
// feature leans on translucency or gradients:
//
//   1. Notcurses cell alpha is a 2-bit enum (OPAQUE/BLEND/TRANSPARENT/
//      HIGHCONTRAST), not an 8-bit channel -- so what does an arbitrary
//      "40% opacity" actually cost? Stacked BLEND planes quantize to
//      1 - 0.5^depth; manually lerping RGB against a *known* backdrop is
//      exact. Page 1 puts both ramps side by side over the same backdrop
//      so the quantization is directly measurable by eye.
//   2. Foreground (glyph) alpha behaves differently from background alpha
//      -- HIGHCONTRAST is foreground-only and is the one alpha value that
//      is computed from what's beneath rather than blended with it
//      (page 2).
//   3. Overlapping translucent planes: does compositing accumulate the way
//      a painter's-algorithm mental model expects, does a TRANSPARENT
//      layer in the middle genuinely pass through to an opaque layer two
//      levels down, and can a tint be applied *without* erasing the glyphs
//      beneath it (page 3 -- the exact question ned's diff-gutter line
//      tint hit).
//   4. Gradients: Notcurses ships ncplane_gradient()/ncplane_gradient2x1(),
//      but they interpolate corner *colors* only and explicitly require all
//      four corners to share one alpha. Page 4 exercises the built-ins in
//      every direction they support; page 5 shows what an opacity-only
//      gradient costs once you leave that constraint (manual lerp, blend
//      staircase, and the built-in's own refusal); page 6 does the same
//      through ncvisual's RGBA path, which is the only place in Notcurses
//      that has real 8-bit-per-pixel alpha; page 7 combines colour and
//      opacity ramps on independent axes.
//
// Run it directly in a real terminal (it needs a real tty):
//     ./build/notcurses_gradient_probe
//
// Wants at least an 88x36 terminal.
// Pages: SPACE/n/RIGHT next, p/LEFT previous, q/ESC quit.
//
// Page 8 is the whole-window version of the question the other pages ask in
// small blocks: one colour gradient repeated as ten bands of increasing
// opacity with *nothing* drawn beneath any of them, so the only thing that
// can show through is the terminal itself. `m` cycles the mechanism drawing
// it -- stacked cell alpha, ncvisual per-pixel alpha with and without
// NCVISUAL_OPTION_BLEND, and per-pixel alpha ramping along the row as well
// as down the ladder -- which is what answers "can alpha vary per pixel the
// way colour does?" for each of the routes Notcurses actually offers.
//

#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <vector>

#include <notcurses/notcurses.h>

namespace {

struct Rgb {
    int r;
    int g;
    int b;
};

int ClampByte(double v) {
    if (v < 0.0) {
        return 0;
    }
    if (v > 255.0) {
        return 255;
    }
    return static_cast<int>(v + 0.5);
}

Rgb Lerp(Rgb from, Rgb to, double t) {
    return Rgb{
        ClampByte(from.r + (to.r - from.r) * t),
        ClampByte(from.g + (to.g - from.g) * t),
        ClampByte(from.b + (to.b - from.b) * t),
    };
}

const Rgb kText{215, 215, 225};
const Rgb kDim{120, 120, 135};

void Label(ncplane* plane, int y, int x, Rgb fg, const char* fmt, ...) {
    char    buf[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    ncplane_set_bg_default(plane);
    ncplane_set_fg_rgb8(plane, fg.r, fg.g, fg.b);
    ncplane_putstr_yx(plane, y, x, buf);
    ncplane_set_fg_default(plane);
}

// Page titles carry their own "n/N" so adding or reordering a page cannot
// leave a stale number behind in a string literal.
int gPageIndex = 0;
int gPageCount = 0;

void PageTitle(ncplane* plane, const char* title) {
    Label(plane, 1, 2, kText, "%d/%d  %s", gPageIndex + 1, gPageCount, title);
}

void PutCell(ncplane* plane, int y, int x, Rgb bg, Rgb fg, const char* egc) {
    ncplane_set_bg_rgb8(plane, bg.r, bg.g, bg.b);
    ncplane_set_fg_rgb8(plane, fg.r, fg.g, fg.b);
    ncplane_putstr_yx(plane, y, x, egc);
    ncplane_set_bg_default(plane);
    ncplane_set_fg_default(plane);
}

void FillRect(ncplane* plane, int y, int x, int h, int w, Rgb bg) {
    for (int r = 0; r < h; ++r) {
        for (int c = 0; c < w; ++c) {
            PutCell(plane, y + r, x + c, bg, kText, " ");
        }
    }
}

// The one shared backdrop every translucency test overlays. Deliberately a
// two-tone diagonal stripe rather than a flat colour: a flat backdrop makes
// a wrong blend look plausible (any single colour under a translucent layer
// still reads as "a colour"), whereas a stripe makes the *ratio* visible --
// at 50% the two bands stay clearly distinct, at 90% they nearly vanish.
// Keyed on absolute screen coordinates so the pattern stays continuous
// across separately-drawn blocks.
Rgb BackdropAt(int y, int x) {
    const int band = (((y * 2) + x) / 4) % 2;
    return band != 0 ? Rgb{34, 34, 44} : Rgb{92, 92, 116};
}

void DrawBackdrop(ncplane* plane, int y, int x, int h, int w, bool withText) {
    static const char* kPattern   = "ned // native text editor  ";
    const std::size_t  patternLen = std::strlen(kPattern);
    for (int r = 0; r < h; ++r) {
        for (int c = 0; c < w; ++c) {
            char egc[2] = {' ', '\0'};
            if (withText) {
                egc[0] = kPattern[static_cast<std::size_t>(c) % patternLen];
            }
            PutCell(plane, y + r, x + c, BackdropAt(y + r, x + c), Rgb{170, 170, 185}, egc);
        }
    }
}

// Composites `fn(u, v) -> (colour, alpha)` over BackdropAt() by hand. This
// is the exact-alpha reference every quantized Notcurses mechanism on these
// pages is compared against -- and, being a plain RGB lerp against a colour
// we already know, it is also what real editor code would use.
template <typename F>
void ManualComposite(ncplane* plane, int y, int x, int h, int w, F&& fn) {
    for (int r = 0; r < h; ++r) {
        for (int c = 0; c < w; ++c) {
            const double                 u      = w > 1 ? static_cast<double>(c) / (w - 1) : 0.0;
            const double                 v      = h > 1 ? static_cast<double>(r) / (h - 1) : 0.0;
            const std::pair<Rgb, double> sample = fn(u, v);
            const Rgb                    under  = BackdropAt(y + r, x + c);
            PutCell(plane, y + r, x + c, Lerp(under, sample.first, sample.second), kText, " ");
        }
    }
}

uint64_t TintChannels(Rgb colour, unsigned bgAlpha, unsigned fgAlpha) {
    uint64_t channels = 0;
    ncchannels_set_bg_rgb8(&channels, colour.r, colour.g, colour.b);
    ncchannels_set_bg_alpha(&channels, bgAlpha);
    ncchannels_set_fg_rgb8(&channels, kText.r, kText.g, kText.b);
    ncchannels_set_fg_alpha(&channels, fgAlpha);
    return channels;
}

// An overlay plane whose *base cell* carries the tint. Base cells apply
// wherever the plane's own gcluster is 0 -- i.e. everywhere on a plane
// nothing was ever written to -- so an empty-EGC base is how a plane tints
// a region without contributing a glyph of its own. Writing spaces instead
// would erase whatever glyph is beneath (page 3 shows both).
ncplane* MakeOverlay(ncplane* parent, int y, int x, int h, int w, uint64_t channels,
                     const char* baseEgc, std::vector<ncplane*>& owned) {
    ncplane_options opts{};
    opts.y    = y;
    opts.x    = x;
    opts.rows = static_cast<unsigned>(h);
    opts.cols = static_cast<unsigned>(w);

    ncplane* plane = ncplane_create(parent, &opts);
    if (plane == nullptr) {
        return nullptr;
    }
    ncplane_set_base(plane, baseEgc, 0, channels);
    owned.push_back(plane);
    return plane;
}

// `depth` BLEND overlays stacked on the same rectangle. Each layer blends
// 50/50 with everything already beneath it, so effective coverage is
// 1 - 0.5^depth: 0.5, 0.75, 0.875, ... -- an exponential staircase, never
// an arbitrary requested alpha.
void StackedBlend(ncplane* parent, int y, int x, int h, int w, Rgb colour, int depth,
                  std::vector<ncplane*>& owned) {
    for (int i = 0; i < depth; ++i) {
        MakeOverlay(parent, y, x, h, w, TintChannels(colour, NCALPHA_BLEND, NCALPHA_TRANSPARENT), "",
                    owned);
    }
}

double StackedBlendCoverage(int depth) {
    return 1.0 - std::pow(0.5, depth);
}

struct RgbaImage {
    int                  w = 0;
    int                  h = 0;
    std::vector<uint8_t> px;

    RgbaImage(int width, int height) : w(width), h(height), px(static_cast<std::size_t>(width) * height * 4, 0) {
    }

    void Set(int x, int y, Rgb colour, int alpha) {
        const std::size_t i = (static_cast<std::size_t>(y) * w + x) * 4;
        px[i + 0]           = static_cast<uint8_t>(colour.r);
        px[i + 1]           = static_cast<uint8_t>(colour.g);
        px[i + 2]           = static_cast<uint8_t>(colour.b);
        px[i + 3]           = static_cast<uint8_t>(alpha);
    }
};

// ncvisual is the only Notcurses path with genuine 8-bit-per-pixel alpha.
// What actually happens to that alpha depends on the blitter: the cell
// blitters (2x2/3x2/braille) average sub-cell samples in software and honour
// NCVISUAL_OPTION_BLEND against the plane beneath, while NCBLIT_PIXEL hands
// the bitmap to the terminal's own protocol, where partial alpha is the
// terminal's business (and BLEND is documented as unsupported).
const char* BlitterName(ncblitter_e blitter) {
    switch (blitter) {
        case NCBLIT_1x1:
            return "1x1";
        case NCBLIT_2x1:
            return "2x1 halves";
        case NCBLIT_2x2:
            return "2x2 quadrants";
        case NCBLIT_3x2:
            return "3x2 sextants";
        case NCBLIT_4x2:
            return "4x2 octants";
        case NCBLIT_BRAILLE:
            return "braille";
        case NCBLIT_PIXEL:
            return "pixel";
        case NCBLIT_4x1:
            return "4x1";
        case NCBLIT_8x1:
            return "8x1";
        default:
            return "default";
    }
}

// How many source pixels one cell will actually consume, and which blitter
// will actually run. Both matter and neither is the one you asked for: a
// requested blitter silently degrades to whatever the environment supports
// (tmux, for one, takes quadrants/sextants down to 2x1 halves), and if you
// size the source buffer for the blitter you *asked* for, the blitted plane
// comes out a different size than the slot you drew a backdrop into --
// which is exactly how an earlier draft of this probe ended up with two
// 80-cell-wide blocks overwriting each other in 40-cell slots.
struct BlitPlan {
    ncblitter_e effective = NCBLIT_1x1;
    unsigned    scaley    = 1;
    unsigned    scalex    = 1;
};

BlitPlan ResolveBlitter(notcurses* nc, ncblitter_e requested) {
    ncvisual_options vopts{};
    vopts.blitter = requested;
    vopts.scaling = NCSCALE_NONE;

    ncvgeom geom{};
    if (ncvisual_geom(nc, nullptr, &vopts, &geom) != 0 || geom.scaley == 0 || geom.scalex == 0) {
        return BlitPlan{requested, 1, 1};
    }
    return BlitPlan{geom.blitter, geom.scaley, geom.scalex};
}

// "rows x cols" of a blitted plane, or "FAILED" -- the blitter's own cell
// geometry decides how many cells a given RGBA buffer becomes, so the
// answer belongs on screen next to the result.
const char* PlaneGeom(ncplane* plane) {
    static char buf[32];
    if (plane == nullptr) {
        return "FAILED";
    }
    unsigned rows = 0;
    unsigned cols = 0;
    ncplane_dim_yx(plane, &rows, &cols);
    std::snprintf(buf, sizeof(buf), "%ux%u cells", rows, cols);
    return buf;
}

ncplane* BlitRgba(notcurses* nc, ncplane* parent, const RgbaImage& img, int y, int x,
                  ncblitter_e blitter, bool blend, std::vector<ncplane*>& owned) {
    ncvisual* visual = ncvisual_from_rgba(img.px.data(), img.h, img.w * 4, img.w);
    if (visual == nullptr) {
        return nullptr;
    }

    ncvisual_options vopts{};
    vopts.n       = parent;
    vopts.y       = y;
    vopts.x       = x;
    vopts.scaling = NCSCALE_NONE;
    vopts.blitter = blitter;
    vopts.flags   = NCVISUAL_OPTION_CHILDPLANE | (blend ? NCVISUAL_OPTION_BLEND : 0ull);

    ncplane* drawn = ncvisual_blit(nc, visual, &vopts);
    ncvisual_destroy(visual);
    if (drawn != nullptr) {
        owned.push_back(drawn);
    }
    return drawn;
}

Rgb HueRamp(double t) {
    // A fixed four-stop ramp rather than real HSV: the point is a visibly
    // non-linear colour path, so any per-channel clamping/rounding error in
    // a gradient shows up as banding instead of hiding in a grey ramp.
    static const Rgb kStops[] = {
        Rgb{40, 90, 220},
        Rgb{40, 200, 190},
        Rgb{230, 190, 60},
        Rgb{225, 60, 110},
    };
    const int    last   = 3;
    const double scaled = t * last;
    int          i      = static_cast<int>(scaled);
    if (i >= last) {
        i = last - 1;
    }
    return Lerp(kStops[i], kStops[i + 1], scaled - i);
}

double Radial(double u, double v) {
    const double dx = (u - 0.5) * 2.0;
    const double dy = (v - 0.5) * 2.0;
    const double d  = std::sqrt(dx * dx + dy * dy) / std::sqrt(2.0);
    return d > 1.0 ? 1.0 : d;
}

// ---------------------------------------------------------------- pages --

const char* PixelImplName(ncpixelimpl_e impl) {
    switch (impl) {
        case NCPIXEL_NONE:
            return "NONE (no pixel graphics)";
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

const char* EnvOr(const char* name, const char* fallback) {
    const char* v = std::getenv(name);
    return (v != nullptr && *v != '\0') ? v : fallback;
}

// What every other page's behaviour actually hinges on, gathered in one
// place: which terminal Notcurses thinks it is talking to, which pixel
// backend (if any) it will use, and -- the one that silently rewrites the
// ncvisual pages -- which blitter each request degrades to here.
//
// Worth reading with `--termtype <name>`: that overrides the terminfo entry
// Notcurses opens (normally $TERM), which is how much of the "force the
// output mode" advice from other tools is expressed. Note up front what it
// cannot do: notcurses 3.0.17 defines NCPIXEL_ITERM2 in its enum but never
// selects or implements it, so no termtype makes it emit iTerm2 inline
// images. Forcing a termtype only moves cell capabilities (quadrants,
// sextants, RGB) and therefore blitter degradation.
void PageCapabilities(notcurses* nc, ncplane* p, std::vector<ncplane*>&) {
    PageTitle(p, "What this terminal reports, and what Notcurses will do with it");

    int   y        = 3;
    char* detected = notcurses_detected_terminal(nc);
    Label(p, y++, 2, kDim, "detected terminal : %s", detected != nullptr ? detected : "(unknown)");
    if (detected != nullptr) {
        free(detected);
    }
    Label(p, y++, 2, kDim, "TERM              : %s", EnvOr("TERM", "(unset)"));
    Label(p, y++, 2, kDim, "COLORTERM         : %s", EnvOr("COLORTERM", "(unset)"));
    Label(p, y++, 2, kDim, "TERM_PROGRAM      : %s", EnvOr("TERM_PROGRAM", "(unset)"));
    Label(p, y++, 2, kDim, "KONSOLE_VERSION   : %s", EnvOr("KONSOLE_VERSION", "(unset)"));
    Label(p, y++, 2, kDim, "TMUX              : %s", EnvOr("TMUX", "(unset)"));
    ++y;

    Label(p, y++, 2, kText, "Capabilities:");
    Label(p, y++, 2, kDim, "  truecolor %s   palette %u   utf8 %s", notcurses_cantruecolor(nc) ? "yes" : "no",
          notcurses_palette_size(nc), notcurses_canutf8(nc) ? "yes" : "no");
    Label(p, y++, 2, kDim, "  halfblock %s   quadrant %s   sextant %s   braille %s",
          notcurses_canhalfblock(nc) ? "yes" : "no", notcurses_canquadrant(nc) ? "yes" : "no",
          notcurses_cansextant(nc) ? "yes" : "no", notcurses_canbraille(nc) ? "yes" : "no");
    Label(p, y++, 2, kDim, "  pixel     %s   backend %s", notcurses_canpixel(nc) ? "yes" : "no",
          PixelImplName(notcurses_check_pixel_support(nc)));
    ++y;

    Label(p, y++, 2, kText, "Blitter degradation (what a request actually runs as here):");
    const struct {
        ncblitter_e blitter;
        const char* name;
    } requests[] = {
        {NCBLIT_2x1, "2x1"},
        {NCBLIT_2x2, "2x2"},
        {NCBLIT_3x2, "3x2"},
        {NCBLIT_4x2, "4x2"},
        {NCBLIT_BRAILLE, "braille"},
        {NCBLIT_PIXEL, "pixel"},
    };
    for (const auto& request : requests) {
        const BlitPlan plan = ResolveBlitter(nc, request.blitter);
        Label(p, y++, 2, kDim, "  %-8s -> %-16s %ux%u source pixels per cell", request.name,
              BlitterName(plan.effective), plan.scalex, plan.scaley);
    }
    ++y;

    Label(p, y++, 2, kDim,
          "notcurses 3.0.17 has no iTerm2 image backend (NCPIXEL_ITERM2 is declared but never selected), so a");
    Label(p, y++, 2, kDim,
          "terminal whose alpha pipeline only works through the iTerm2 protocol cannot be reached from here --");
    Label(p, y++, 2, kDim,
          "Tools/TerminalImageAlphaProbe.cpp speaks that protocol (and kitty's, and sixel) directly instead.");
}

void PageBackgroundAlpha(notcurses*, ncplane* p, std::vector<ncplane*>& owned) {
    PageTitle(p, "Background transparency, degree by degree");
    Label(p, 2, 2, kDim,
          "Backdrop is a 2-tone diagonal stripe. At low opacity both bands stay distinct; at high opacity they merge.");

    int y = 4;
    Label(p, y++, 2, kText, "A) Manual RGB lerp against the known backdrop -- exact, arbitrary alpha (the reference):");
    const int swatchW = 9;
    const int steps   = 7;
    DrawBackdrop(p, y, 2, 3, swatchW * steps, false);
    for (int s = 0; s < steps; ++s) {
        const double alpha = static_cast<double>(s) / (steps - 1);
        ManualComposite(p, y, 2 + s * swatchW, 3, swatchW,
                        [alpha](double, double) { return std::make_pair(Rgb{60, 210, 200}, alpha); });
        Label(p, y + 3, 2 + s * swatchW + 1, kDim, "%3d%%", static_cast<int>(alpha * 100 + 0.5));
    }
    y += 5;

    Label(p, y++, 2, kText, "B) NCALPHA_BLEND overlay planes stacked 0..6 deep -- what Notcurses' own cell alpha can express:");
    DrawBackdrop(p, y, 2, 3, swatchW * steps, false);
    for (int s = 0; s < steps; ++s) {
        StackedBlend(p, y, 2 + s * swatchW, 3, swatchW, Rgb{60, 210, 200}, s, owned);
        Label(p, y + 3, 2 + s * swatchW, kDim, "d%d %2d%%", s,
              static_cast<int>(StackedBlendCoverage(s) * 100 + 0.5));
    }
    y += 5;

    Label(p, y++, 2, kText,
          "C) The same alpha values with NOTHING known beneath (straight onto the terminal's own background):");
    {
        const int w = 22;
        MakeOverlay(p, y, 2, 2, w, TintChannels(Rgb{60, 210, 200}, NCALPHA_BLEND, NCALPHA_TRANSPARENT), "", owned);
        MakeOverlay(p, y, 2 + w + 2, 2, w, TintChannels(Rgb{60, 210, 200}, NCALPHA_TRANSPARENT, NCALPHA_TRANSPARENT),
                    "", owned);
        MakeOverlay(p, y, 2 + 2 * (w + 2), 2, w, TintChannels(Rgb{60, 210, 200}, NCALPHA_OPAQUE, NCALPHA_TRANSPARENT),
                    "", owned);
        Label(p, y + 2, 2, kDim, "BLEND (nothing under)");
        Label(p, y + 2, 2 + w + 2, kDim, "TRANSPARENT");
        Label(p, y + 2, 2 + 2 * (w + 2), kDim, "OPAQUE");
    }
    y += 4;

    Label(p, y, 2, kDim,
          "Look for: B can only ever hit 0/50/75/87.5%% -- if you need 40%%, only A gets you there.");
}

void PageForegroundAlpha(notcurses*, ncplane* p, std::vector<ncplane*>& owned) {
    PageTitle(p, "Foreground (glyph) transparency");
    Label(p, 2, 2, kDim,
          "Foreground alpha is a separate 2-bit field from background alpha, and HIGHCONTRAST is foreground-only.");

    static const char* kSample = "  ned // fn main() -> Result<(), Error>  ";
    const int          sampleW = static_cast<int>(std::strlen(kSample));

    int y = 4;
    Label(p, y++, 2, kText, "A) Each foreground alpha over an opaque steel-blue band:");
    const unsigned alphas[]     = {NCALPHA_OPAQUE, NCALPHA_BLEND, NCALPHA_TRANSPARENT, NCALPHA_HIGHCONTRAST};
    const char*    alphaNames[] = {"OPAQUE", "BLEND", "TRANSPARENT", "HIGHCONTRAST"};
    for (int i = 0; i < 4; ++i) {
        const int row = y + i;
        FillRect(p, row, 2, 1, sampleW, Rgb{46, 66, 96});
        uint64_t channels = 0;
        ncchannels_set_bg_rgb8(&channels, 46, 66, 96);
        ncchannels_set_fg_rgb8(&channels, 240, 170, 60);
        if (ncchannels_set_fg_alpha(&channels, alphas[i]) < 0) {
            Label(p, row, 2, kDim, " (fg alpha rejected) ");
        }
        ncplane* overlay = MakeOverlay(p, row, 2, 1, sampleW, channels, " ", owned);
        if (overlay != nullptr) {
            ncplane_set_channels(overlay, channels);
            ncplane_putstr_yx(overlay, 0, 0, kSample);
        }
        Label(p, row, 2 + sampleW + 2, kDim, "%s", alphaNames[i]);
    }
    y += 5;

    Label(p, y++, 2, kText, "B) Manual foreground lerp -- arbitrary glyph opacity against the same known band:");
    {
        const int steps = 6;
        for (int s = 0; s < steps; ++s) {
            const double alpha = static_cast<double>(s) / (steps - 1);
            const int    row   = y;
            const int    x     = 2 + s * 13;
            const Rgb    bg{46, 66, 96};
            const Rgb    fg = Lerp(bg, Rgb{240, 170, 60}, alpha);
            FillRect(p, row, x, 1, 12, bg);
            ncplane_set_bg_rgb8(p, bg.r, bg.g, bg.b);
            ncplane_set_fg_rgb8(p, fg.r, fg.g, fg.b);
            ncplane_putstr_yx(p, row, x, " fn main() ");
            ncplane_set_bg_default(p);
            ncplane_set_fg_default(p);
            Label(p, row + 1, x + 2, kDim, "%3d%%", static_cast<int>(alpha * 100 + 0.5));
        }
        y += 3;
    }

    Label(p, y++, 2, kText, "C) Foreground BLEND stacked 1..4 deep (glyph drawn on every layer) over the same band:");
    for (int depth = 1; depth <= 4; ++depth) {
        const int x = 2 + (depth - 1) * 20;
        FillRect(p, y, x, 1, 18, Rgb{46, 66, 96});
        for (int i = 0; i < depth; ++i) {
            uint64_t channels = 0;
            ncchannels_set_bg_alpha(&channels, NCALPHA_TRANSPARENT);
            ncchannels_set_fg_rgb8(&channels, 240, 170, 60);
            ncchannels_set_fg_alpha(&channels, NCALPHA_BLEND);
            ncplane* overlay = MakeOverlay(p, y, x, 1, 18, channels, "", owned);
            if (overlay != nullptr) {
                ncplane_set_channels(overlay, channels);
                ncplane_putstr_yx(overlay, 0, 0, "  fn main() {}    ");
            }
        }
        Label(p, y + 1, x + 2, kDim, "depth %d", depth);
    }
    y += 3;

    Label(p, y++, 2, kText, "D) The same four alphas straight onto the terminal background (no known colour beneath):");
    for (int i = 0; i < 4; ++i) {
        const int x        = 2 + i * 22;
        uint64_t  channels = 0;
        ncchannels_set_bg_alpha(&channels, NCALPHA_TRANSPARENT);
        ncchannels_set_fg_rgb8(&channels, 240, 170, 60);
        if (ncchannels_set_fg_alpha(&channels, alphas[i]) < 0) {
            Label(p, y, x, kDim, "(rejected)");
            Label(p, y + 1, x, kDim, "%s", alphaNames[i]);
            continue;
        }
        ncplane* overlay = MakeOverlay(p, y, x, 1, 20, channels, "", owned);
        if (overlay != nullptr) {
            ncplane_set_channels(overlay, channels);
            ncplane_putstr_yx(overlay, 0, 0, " fn main() {}  ");
        }
        Label(p, y + 1, x, kDim, "%s", alphaNames[i]);
    }
    y += 3;

    Label(p, y, 2, kDim,
          "Look for: HIGHCONTRAST ignores the requested colour -- it derives a legible one from what is beneath.");
}

void PageOverlap(notcurses*, ncplane* p, std::vector<ncplane*>& owned) {
    PageTitle(p, "Overlapping translucent planes");
    Label(p, 2, 2, kDim, "Three 50%% BLEND rectangles over the striped backdrop; overlaps composite bottom-up.");

    int y = 4;
    DrawBackdrop(p, y, 2, 9, 62, false);
    MakeOverlay(p, y + 0, 2, 5, 30, TintChannels(Rgb{220, 60, 70}, NCALPHA_BLEND, NCALPHA_TRANSPARENT), "", owned);
    MakeOverlay(p, y + 2, 18, 5, 30, TintChannels(Rgb{60, 200, 90}, NCALPHA_BLEND, NCALPHA_TRANSPARENT), "", owned);
    MakeOverlay(p, y + 4, 34, 5, 30, TintChannels(Rgb{70, 120, 235}, NCALPHA_BLEND, NCALPHA_TRANSPARENT), "", owned);
    Label(p, y + 9, 2, kDim, "red -> green -> blue, each 50%% BLEND; the triple overlap is the deepest stack.");

    y += 11;
    Label(p, y++, 2, kText, "B) Tinting a row of real text -- empty-EGC base cell vs. writing spaces:");
    DrawBackdrop(p, y, 2, 1, 60, true);
    MakeOverlay(p, y, 2, 1, 60, TintChannels(Rgb{240, 120, 40}, NCALPHA_BLEND, NCALPHA_TRANSPARENT), "", owned);
    Label(p, y, 64, kDim, "empty EGC: glyphs survive");
    DrawBackdrop(p, y + 1, 2, 1, 60, true);
    MakeOverlay(p, y + 1, 2, 1, 60, TintChannels(Rgb{240, 120, 40}, NCALPHA_BLEND, NCALPHA_TRANSPARENT), " ", owned);
    Label(p, y + 1, 64, kDim, "space EGC: glyphs erased");
    y += 3;

    Label(p, y++, 2, kText, "C) BLEND over TRANSPARENT over an opaque teal band -- does the middle layer pass through?");
    FillRect(p, y, 2, 2, 60, Rgb{20, 110, 110});
    MakeOverlay(p, y, 2, 2, 60, TintChannels(Rgb{0, 0, 0}, NCALPHA_TRANSPARENT, NCALPHA_TRANSPARENT), "", owned);
    MakeOverlay(p, y, 2, 2, 60, TintChannels(Rgb{250, 240, 120}, NCALPHA_BLEND, NCALPHA_TRANSPARENT), "", owned);
    y += 3;

    Label(p, y, 2, kDim,
          "Look for: C should read as a 50%% yellow/teal mix -- a blocking middle layer would show plain yellow.");
}

void PageGradientDirections(notcurses*, ncplane* p, std::vector<ncplane*>& owned) {
    (void)owned;
    PageTitle(p, "Colour gradients, every direction (fully opaque)");
    Label(p, 2, 2, kDim, "Top half: computed per cell by hand. Bottom half: Notcurses' own ncplane_gradient() family.");

    const int blockH = 5;
    const int blockW = 34;
    int       y      = 4;

    ManualComposite(p, y, 2, blockH, blockW, [](double u, double) { return std::make_pair(HueRamp(u), 1.0); });
    Label(p, y + blockH, 2, kDim, "manual: horizontal (left -> right)");

    ManualComposite(p, y, 2 + blockW + 4, blockH, blockW,
                    [](double, double v) { return std::make_pair(HueRamp(v), 1.0); });
    Label(p, y + blockH, 2 + blockW + 4, kDim, "manual: vertical (top -> bottom)");
    y += blockH + 2;

    ManualComposite(p, y, 2, blockH, blockW,
                    [](double u, double v) { return std::make_pair(HueRamp((u + v) * 0.5), 1.0); });
    Label(p, y + blockH, 2, kDim, "manual: diagonal (UL -> LR)");

    ManualComposite(p, y, 2 + blockW + 4, blockH, blockW,
                    [](double u, double v) { return std::make_pair(HueRamp(Radial(u, v)), 1.0); });
    Label(p, y + blockH, 2 + blockW + 4, kDim, "manual: radial (centre -> edge)");
    y += blockH + 2;

    auto Chan = [](Rgb c) {
        uint64_t ch = 0;
        ncchannels_set_fg_rgb8(&ch, c.r, c.g, c.b);
        ncchannels_set_bg_rgb8(&ch, c.r, c.g, c.b);
        return ch;
    };
    const Rgb a{40, 90, 220};
    const Rgb b{230, 190, 60};
    const Rgb c{225, 60, 110};
    const Rgb d{40, 200, 190};

    const int rcH = ncplane_gradient(p, y, 2, blockH, blockW, " ", 0, Chan(a), Chan(b), Chan(a), Chan(b));
    Label(p, y + blockH, 2, kDim, "gradient() horizontal -> rc %d", rcH);

    const int rcV = ncplane_gradient(p, y, 2 + blockW + 4, blockH, blockW, " ", 0, Chan(a), Chan(a), Chan(c), Chan(c));
    Label(p, y + blockH, 2 + blockW + 4, kDim, "gradient() vertical -> rc %d", rcV);
    y += blockH + 2;

    const int rcD = ncplane_gradient(p, y, 2, blockH, blockW, " ", 0, Chan(a), Chan(b), Chan(d), Chan(c));
    Label(p, y + blockH, 2, kDim, "gradient() 4-corner -> rc %d", rcD);

    uint32_t ul = 0;
    uint32_t ur = 0;
    uint32_t ll = 0;
    uint32_t lr = 0;
    ncchannel_set_rgb8(&ul, a.r, a.g, a.b);
    ncchannel_set_rgb8(&ur, a.r, a.g, a.b);
    ncchannel_set_rgb8(&ll, c.r, c.g, c.b);
    ncchannel_set_rgb8(&lr, c.r, c.g, c.b);
    const int rc2x1 = ncplane_gradient2x1(p, y, 2 + blockW + 4, blockH, blockW, ul, ur, ll, lr);
    Label(p, y + blockH, 2 + blockW + 4, kDim, "gradient2x1 vertical, 2x rows -> rc %d", rc2x1);
    y += blockH + 2;

    Label(p, y, 2, kDim,
          "Look for: gradient2x1 has twice gradient()'s vertical gradations, paid for with the glyph (half blocks).");
}

void PageOpacityGradients(notcurses*, ncplane* p, std::vector<ncplane*>& owned) {
    PageTitle(p, "Opacity-only gradients (one constant colour, alpha ramps)");
    Label(p, 2, 2, kDim, "All four blocks are the same magenta over the same backdrop; only the alpha ramp differs.");

    const int blockH = 5;
    const int blockW = 34;
    const Rgb tint{225, 70, 190};
    int       y = 4;

    DrawBackdrop(p, y, 2, blockH, blockW, false);
    ManualComposite(p, y, 2, blockH, blockW, [tint](double u, double) { return std::make_pair(tint, u); });
    Label(p, y + blockH, 2, kDim, "manual alpha 0->100%% horizontal");

    DrawBackdrop(p, y, 2 + blockW + 4, blockH, blockW, false);
    ManualComposite(p, y, 2 + blockW + 4, blockH, blockW,
                    [tint](double, double v) { return std::make_pair(tint, v); });
    Label(p, y + blockH, 2 + blockW + 4, kDim, "manual alpha 0->100%% vertical");
    y += blockH + 2;

    DrawBackdrop(p, y, 2, blockH, blockW, false);
    ManualComposite(p, y, 2, blockH, blockW,
                    [tint](double u, double v) { return std::make_pair(tint, 1.0 - Radial(u, v)); });
    Label(p, y + blockH, 2, kDim, "manual radial alpha (opaque centre)");

    // The same ramp as far as BLEND stacking can follow it: seven columns,
    // each a deeper stack. This is the only *native* way to vary opacity
    // across a region, and it is why an opacity gradient in cell-land is a
    // staircase rather than a ramp.
    const int steps  = 7;
    const int stepW  = blockW / steps;
    const int stackX = 2 + blockW + 4;
    DrawBackdrop(p, y, stackX, blockH, stepW * steps, false);
    for (int s = 0; s < steps; ++s) {
        StackedBlend(p, y, stackX + s * stepW, blockH, stepW, tint, s, owned);
    }
    Label(p, y + blockH, stackX, kDim, "BLEND staircase 0/50/75/88/94/97/98%%");
    y += blockH + 2;

    // And the built-in gradient's own answer to "ramp the alpha": it is a
    // documented precondition that all four corner alphas match, so this is
    // expected to fail outright rather than interpolate.
    uint64_t ulOpaque = 0;
    ncchannels_set_fg_rgb8(&ulOpaque, tint.r, tint.g, tint.b);
    ncchannels_set_bg_rgb8(&ulOpaque, tint.r, tint.g, tint.b);
    uint64_t lrBlend = ulOpaque;
    ncchannels_set_bg_alpha(&lrBlend, NCALPHA_BLEND);
    ncchannels_set_fg_alpha(&lrBlend, NCALPHA_BLEND);

    DrawBackdrop(p, y, 2, 3, blockW, false);
    const int rcMixed = ncplane_gradient(p, y, 2, 3, blockW, " ", 0, ulOpaque, lrBlend, ulOpaque, lrBlend);
    Label(p, y + 3, 2, kDim, "gradient() with OPAQUE -> BLEND corners -> rc %d (corner alphas must all match)", rcMixed);
    y += 5;

    Label(p, y, 2, kDim,
          "Look for: the staircase is visibly uneven -- half the range is spent between the first two columns.");
}

void PageVisualOpacity(notcurses* nc, ncplane* p, std::vector<ncplane*>& owned) {
    PageTitle(p, "Opacity gradients through ncvisual (real 8-bit-per-pixel alpha)");
    Label(p, 2, 2, kDim,
          "One constant colour, alpha 0..255 left to right, over the striped backdrop. Only the blitter/flags change.");

    const int cellsH = 4;
    const int cellsW = 40;
    const Rgb tint{80, 220, 140};

    auto Ramp = [tint](const BlitPlan& plan, int cw, int ch) {
        RgbaImage img(cw * static_cast<int>(plan.scalex), ch * static_cast<int>(plan.scaley));
        for (int iy = 0; iy < img.h; ++iy) {
            for (int ix = 0; ix < img.w; ++ix) {
                const double u = img.w > 1 ? static_cast<double>(ix) / (img.w - 1) : 0.0;
                img.Set(ix, iy, tint, static_cast<int>(u * 255.0 + 0.5));
            }
        }
        return img;
    };

    struct Case {
        ncblitter_e blitter;
        bool        blend;
        const char* what;
    };
    const Case cases[] = {
        {NCBLIT_2x2, true, "2x2 + BLEND"},
        {NCBLIT_2x2, false, "2x2, no BLEND"},
        {NCBLIT_3x2, true, "3x2 + BLEND"},
        {NCBLIT_BRAILLE, true, "braille + BLEND"},
    };

    int y = 4;
    for (int i = 0; i < 4; ++i) {
        const int       x    = 2 + (i % 2) * (cellsW + 4);
        const BlitPlan  plan = ResolveBlitter(nc, cases[i].blitter);
        const RgbaImage img  = Ramp(plan, cellsW, cellsH);
        DrawBackdrop(p, y, x, cellsH, cellsW, false);
        ncplane* drawn = BlitRgba(nc, p, img, y, x, cases[i].blitter, cases[i].blend, owned);
        Label(p, y + cellsH, x, kDim, "%s -> %s", cases[i].what, PlaneGeom(drawn));
        Label(p, y + cellsH + 1, x, kDim, "  ran as %s (%ux%upx/cell)", BlitterName(plan.effective), plan.scalex,
              plan.scaley);
        if (i % 2 == 1) {
            y += cellsH + 3;
        }
    }

    const ncpixelimpl_e impl = notcurses_check_pixel_support(nc);
    if (impl == NCPIXEL_NONE) {
        Label(p, y, 2, kDim, "NCBLIT_PIXEL: no pixel-graphics support in this terminal -- both pixel cases skipped.");
        y += 2;
    }
    else {
        const BlitPlan  plan = ResolveBlitter(nc, NCBLIT_PIXEL);
        const RgbaImage img  = Ramp(plan, cellsW, cellsH);

        DrawBackdrop(p, y, 2, cellsH, cellsW, false);
        ncplane* drawn = BlitRgba(nc, p, img, y, 2, NCBLIT_PIXEL, false, owned);
        Label(p, y + cellsH, 2, kDim, "pixel, no BLEND -> %s", PlaneGeom(drawn));
        Label(p, y + cellsH + 1, 2, kDim, "  cell is %ux%upx", plan.scalex, plan.scaley);

        // BLEND is documented as unsupported for the pixel blitter -- worth
        // attempting anyway, since "documented" and "what this build does"
        // are different claims.
        DrawBackdrop(p, y, 2 + cellsW + 4, cellsH, cellsW, false);
        ncplane* blended = BlitRgba(nc, p, img, y, 2 + cellsW + 4, NCBLIT_PIXEL, true, owned);
        Label(p, y + cellsH, 2 + cellsW + 4, kDim, "pixel + BLEND -> %s", PlaneGeom(blended));
        y += cellsH + 3;
    }

    Label(p, y, 2, kDim,
          "Look for: whether alpha is honoured per pixel, thresholded (nothing drawn below a cut), or ignored.");
}

void PageColourAndOpacity(notcurses* nc, ncplane* p, std::vector<ncplane*>& owned) {
    PageTitle(p, "Colour and opacity ramping together, on independent axes");
    Label(p, 2, 2, kDim,
          "Colour ramps left to right; opacity ramps top to bottom. The backdrop is what makes the second axis readable.");

    const int blockH = 6;
    const int blockW = 40;
    int       y      = 4;

    DrawBackdrop(p, y, 2, blockH, blockW, false);
    ManualComposite(p, y, 2, blockH, blockW,
                    [](double u, double v) { return std::make_pair(HueRamp(u), 1.0 - v); });
    Label(p, y + blockH, 2, kDim, "manual colour L->R, alpha 100->0%% T->B");

    DrawBackdrop(p, y, 2 + blockW + 4, blockH, blockW, false);
    ManualComposite(p, y, 2 + blockW + 4, blockH, blockW, [](double u, double v) {
        const double d = Radial(u, v);
        return std::make_pair(HueRamp(d), 1.0 - d);
    });
    Label(p, y + blockH, 2 + blockW + 4, kDim, "manual radial colour + radial alpha");
    y += blockH + 2;

    auto Field = [](const BlitPlan& plan, int cw, int ch) {
        RgbaImage img(cw * static_cast<int>(plan.scalex), ch * static_cast<int>(plan.scaley));
        for (int iy = 0; iy < img.h; ++iy) {
            for (int ix = 0; ix < img.w; ++ix) {
                const double u = img.w > 1 ? static_cast<double>(ix) / (img.w - 1) : 0.0;
                const double v = img.h > 1 ? static_cast<double>(iy) / (img.h - 1) : 0.0;
                img.Set(ix, iy, HueRamp(u), static_cast<int>((1.0 - v) * 255.0 + 0.5));
            }
        }
        return img;
    };

    {
        const BlitPlan  plan = ResolveBlitter(nc, NCBLIT_2x2);
        const RgbaImage img  = Field(plan, blockW, blockH);
        DrawBackdrop(p, y, 2, blockH, blockW, false);
        ncplane* drawn = BlitRgba(nc, p, img, y, 2, NCBLIT_2x2, true, owned);
        Label(p, y + blockH, 2, kDim, "RGBA 2x2 + BLEND -> %s", PlaneGeom(drawn));
        Label(p, y + blockH + 1, 2, kDim, "  ran as %s", BlitterName(plan.effective));
    }

    {
        DrawBackdrop(p, y, 2 + blockW + 4, blockH, blockW, false);
        if (notcurses_check_pixel_support(nc) == NCPIXEL_NONE) {
            Label(p, y + blockH, 2 + blockW + 4, kDim, "RGBA pixel -> unsupported here, skipped");
        }
        else {
            const BlitPlan  plan  = ResolveBlitter(nc, NCBLIT_PIXEL);
            const RgbaImage img   = Field(plan, blockW, blockH);
            ncplane*        drawn = BlitRgba(nc, p, img, y, 2 + blockW + 4, NCBLIT_PIXEL, false, owned);
            Label(p, y + blockH, 2 + blockW + 4, kDim, "RGBA pixel -> %s", PlaneGeom(drawn));
            Label(p, y + blockH + 1, 2 + blockW + 4, kDim, "  cell is %ux%upx", plan.scalex, plan.scaley);
        }
    }
    y += blockH + 3;

    Label(p, y, 2, kDim,
          "Look for: whether the cell blitters keep colour fidelity as alpha drops, or collapse to the backdrop.");
}

// Page 8 cycles between mechanisms in place rather than showing them side
// by side: the whole point is a full-window field with nothing beneath it,
// and two of those cannot share a screen. `m` cycles; the state is a file
// static because a page function only ever receives the drawing context.
int gMechanism = 0;

const char* kMechanismNames[] = {
    "stacked NCALPHA_BLEND planes (cell alpha -- quantized to 1-0.5^depth)",
    "ncvisual RGBA, one alpha per band, NCVISUAL_OPTION_BLEND set",
    "ncvisual RGBA, one alpha per band, no BLEND flag",
    "ncvisual RGBA + BLEND, alpha ramping per pixel across the row as well as per band",
};
const int kMechanismCount = 4;

// Nearest BLEND-stack depth for a requested alpha, and what that depth
// actually delivers. Depth 0 is invisible and 1-0.5^d never reaches 1, so
// full opacity is a separate case (an OPAQUE cell, no stacking at all).
int NearestBlendDepth(double alpha) {
    int    best    = 0;
    double bestErr = 1e9;
    for (int d = 0; d <= 8; ++d) {
        const double err = std::fabs(StackedBlendCoverage(d) - alpha);
        if (err < bestErr) {
            bestErr = err;
            best    = d;
        }
    }
    return best;
}

// One band of the ladder as cell-alpha planes: the colour gradient is
// written per column, and the whole band is stacked `depth` deep so each
// layer re-blends with whatever the terminal itself is showing.
void GradientBlendBand(ncplane* parent, int y, int x, int h, int w, double alpha,
                       std::vector<ncplane*>& owned) {
    const bool opaque = alpha >= 0.999;
    const int  depth  = opaque ? 1 : NearestBlendDepth(alpha);
    for (int d = 0; d < depth; ++d) {
        ncplane_options opts{};
        opts.y    = y;
        opts.x    = x;
        opts.rows = static_cast<unsigned>(h);
        opts.cols = static_cast<unsigned>(w);

        ncplane* layer = ncplane_create(parent, &opts);
        if (layer == nullptr) {
            return;
        }
        owned.push_back(layer);
        for (int c = 0; c < w; ++c) {
            const double u        = w > 1 ? static_cast<double>(c) / (w - 1) : 0.0;
            const Rgb    colour   = HueRamp(u);
            uint64_t     channels = 0;
            ncchannels_set_bg_rgb8(&channels, colour.r, colour.g, colour.b);
            ncchannels_set_bg_alpha(&channels, opaque ? NCALPHA_OPAQUE : NCALPHA_BLEND);
            ncchannels_set_fg_alpha(&channels, NCALPHA_TRANSPARENT);
            ncplane_set_channels(layer, channels);
            for (int r = 0; r < h; ++r) {
                ncplane_putstr_yx(layer, r, c, " ");
            }
        }
    }
}

void PageFullWindowLadder(notcurses* nc, ncplane* p, std::vector<ncplane*>& owned) {
    unsigned rows = 0;
    unsigned cols = 0;
    ncplane_dim_yx(p, &rows, &cols);

    PageTitle(p, "Full-window alpha ladder -- one colour gradient, ten opacities, nothing drawn beneath");
    Label(p, 2, 2, kDim, "%s   [m cycles]", kMechanismNames[gMechanism]);

    const int top    = 4;
    const int bottom = static_cast<int>(rows) - 2;
    const int bands  = 10;
    const int gutter = 8;
    const int x      = gutter;
    const int w      = static_cast<int>(cols) - x - 1;

    const int availH = bottom - top;
    if (availH < bands) {
        Label(p, top, 2, kDim, "Terminal too short for a ten-band ladder.");
        return;
    }
    if (w < 8) {
        Label(p, top, 2, kDim, "Terminal too narrow for the ladder.");
        return;
    }

    const BlitPlan plan = ResolveBlitter(nc, NCBLIT_2x2);

    for (int i = 0; i < bands; ++i) {
        // Bands are spread across the whole remaining height rather than
        // given a fixed size, so the ladder genuinely fills the window and
        // an odd-height terminal's leftover rows go somewhere.
        const double alpha = static_cast<double>(i + 1) / bands;
        const int    y     = top + (availH * i) / bands;
        const int    bandH = top + (availH * (i + 1)) / bands - y;

        if (gMechanism == 0) {
            GradientBlendBand(p, y, x, bandH, w, alpha, owned);
            const bool opaque = alpha >= 0.999;
            const int  depth  = opaque ? 0 : NearestBlendDepth(alpha);
            Label(p, y, 1, kDim, "%3d%%", static_cast<int>(alpha * 100 + 0.5));
            Label(p, y + (bandH > 1 ? 1 : 0), 1, kDim, opaque ? " opaq" : " d%d=%2.0f%%", depth,
                  StackedBlendCoverage(depth) * 100.0);
        }
        else {
            RgbaImage img(w * static_cast<int>(plan.scalex), bandH * static_cast<int>(plan.scaley));
            for (int iy = 0; iy < img.h; ++iy) {
                for (int ix = 0; ix < img.w; ++ix) {
                    const double u = img.w > 1 ? static_cast<double>(ix) / (img.w - 1) : 0.0;
                    const double a = gMechanism == 3 ? alpha * u : alpha;
                    img.Set(ix, iy, HueRamp(u), static_cast<int>(a * 255.0 + 0.5));
                }
            }
            ncplane* drawn = BlitRgba(nc, p, img, y, x, NCBLIT_2x2, gMechanism != 2, owned);
            Label(p, y, 1, kDim, "%3d%%", static_cast<int>(alpha * 100 + 0.5));
            if (drawn == nullptr) {
                Label(p, y + (bandH > 1 ? 1 : 0), 1, kDim, " fail");
            }
        }
    }

    Label(p, static_cast<int>(rows) - 2, 2, kDim,
          "Same gradient in every band; only opacity differs, and nothing is drawn beneath -- what shows through "
          "is your terminal. A blank band rendered as nothing at all.");
}

// Braille cell as a 2x4 dither grid. Dot bit layout is fixed by Unicode:
// column 0 top-to-bottom is 0x01/0x02/0x04/0x40, column 1 is
// 0x08/0x10/0x20/0x80.
const uint8_t kBrailleDots[4][2] = {
    {0x01, 0x08},
    {0x02, 0x10},
    {0x04, 0x20},
    {0x40, 0x80},
};

// A real 8x8 ordered-dither matrix, indexed by *absolute* sub-pixel
// position rather than per cell. Thresholding each braille dot against it
// is what keeps a coverage ramp smooth across cell boundaries -- a
// per-cell pattern makes every cell in a band identical, which reads as
// vertical striping rather than a gradient (the first draft of this page
// did exactly that).
const int kBayer8[8][8] = {
    {0, 32, 8, 40, 2, 34, 10, 42},
    {48, 16, 56, 24, 50, 18, 58, 26},
    {12, 44, 4, 36, 14, 46, 6, 38},
    {60, 28, 52, 20, 62, 30, 54, 22},
    {3, 35, 11, 43, 1, 33, 9, 41},
    {51, 19, 59, 27, 49, 17, 57, 25},
    {15, 47, 7, 39, 13, 45, 5, 37},
    {63, 31, 55, 23, 61, 29, 53, 21},
};

// UTF-8 for U+2800 + bits. Written out rather than pulled from Text/Utf8.h
// because this probe deliberately links against notcurses-core only.
void BrailleGlyph(int bits, char out[4]) {
    const unsigned cp = 0x2800u + static_cast<unsigned>(bits & 0xff);
    out[0]            = static_cast<char>(0xe0 | (cp >> 12));
    out[1]            = static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
    out[2]            = static_cast<char>(0x80 | (cp & 0x3f));
    out[3]            = '\0';
}

// The whole point of this page: background stays DEFAULT, so the terminal's
// own background -- window translucency, background image, whatever -- is
// what fills the gaps. Only the glyph is coloured. `coverageAt` is sampled
// per braille dot (absolute sub-pixel coordinates), so a field can ramp
// within a cell rather than in cell-sized steps.
template <typename F>
void PutDitheredCell(ncplane* plane, int y, int x, Rgb fg, F&& coverageAt) {
    ncplane_set_bg_default(plane);
    ncplane_set_fg_rgb8(plane, fg.r, fg.g, fg.b);

    int bits = 0;
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 2; ++c) {
            const int    subY   = y * 4 + r;
            const int    subX   = x * 2 + c;
            const double thresh = (kBayer8[subY % 8][subX % 8] + 0.5) / 64.0;
            if (coverageAt(subY, subX) > thresh) {
                bits |= kBrailleDots[r][c];
            }
        }
    }
    if (bits == 0) {
        ncplane_putstr_yx(plane, y, x, " ");
        return;
    }
    if (bits == 0xff) {
        // Braille dots never fully cover a cell -- even U+28FF leaves the
        // inter-dot gaps showing. Full coverage is a full block instead, so
        // a ramp's opaque end is genuinely opaque.
        ncplane_putstr_yx(plane, y, x, "\u2588");
        return;
    }
    char glyph[4];
    BrailleGlyph(bits, glyph);
    ncplane_putstr_yx(plane, y, x, glyph);
}

void PutCoverageCell(ncplane* plane, int y, int x, Rgb fg, double coverage) {
    PutDitheredCell(plane, y, x, fg, [coverage](int, int) { return coverage; });
}

void PutShadeCell(ncplane* plane, int y, int x, Rgb fg, int level) {
    static const char* kShades[] = {" ", "\u2591", "\u2592", "\u2593", "\u2588"};
    ncplane_set_bg_default(plane);
    ncplane_set_fg_rgb8(plane, fg.r, fg.g, fg.b);
    ncplane_putstr_yx(plane, y, x, kShades[level < 0 ? 0 : (level > 4 ? 4 : level)]);
}

// Nothing on this page uses alpha at all: translucency here is *coverage*,
// how much of each cell is painted, over a background left DEFAULT so the
// window's own transparency fills the gaps.
//
// This is the route that needs no graphics protocol and no image support --
// it is ordinary text, so it works anywhere the window itself is
// translucent, including through Notcurses' cell rendering, which is what
// makes it usable from ned. It is not the only route: Konsole 26.08 does
// honour real per-pixel alpha from an iTerm2-protocol image all the way out
// to the window backdrop (confirmed live -- see Tools/TerminalImageAlphaProbe.cpp,
// which is how you would drive that path, since Notcurses cannot). The two
// differ in what they cost: this page's technique is one glyph per cell and
// nothing else, at the price of dithered rather than smooth coverage.
void PageBleedThrough(notcurses*, ncplane* p, std::vector<ncplane*>&) {
    unsigned rows = 0;
    unsigned cols = 0;
    ncplane_dim_yx(p, &rows, &cols);

    PageTitle(p, "Bleed-through without a graphics protocol: coverage dithering on a default background");
    Label(p, 2, 2, kDim,
          "No alpha anywhere below: every cell keeps bg DEFAULT and colours only its glyph, so the window's own "
          "transparency fills the gaps (needs profile opacity < 100%%).");

    const int x = 2;
    const int w = static_cast<int>(cols) - 4;
    int       y = 4;

    Label(p, y++, 2, kText, "A) Block shades -- four coverage levels, one glyph each:");
    for (int i = 0; i < 5; ++i) {
        const int bandW = w / 5;
        for (int c = 0; c < bandW; ++c) {
            const double u = w > 1 ? static_cast<double>(i * bandW + c) / (w - 1) : 0.0;
            for (int r = 0; r < 2; ++r) {
                PutShadeCell(p, y + r, x + i * bandW + c, HueRamp(u), i);
            }
        }
        Label(p, y + 2, x + i * bandW, kDim, "%d%%", i * 25);
    }
    y += 4;

    Label(p, y++, 2, kText, "B) Braille dither -- nine coverage levels from the same 2x4 cell:");
    for (int i = 0; i < 9; ++i) {
        const int bandW = w / 9;
        for (int c = 0; c < bandW; ++c) {
            const double u = w > 1 ? static_cast<double>(i * bandW + c) / (w - 1) : 0.0;
            for (int r = 0; r < 2; ++r) {
                PutCoverageCell(p, y + r, x + i * bandW + c, HueRamp(u), i / 8.0);
            }
        }
        Label(p, y + 2, x + i * bandW, kDim, "%d", i * 100 / 8);
    }
    y += 4;

    const int fieldTop = y + 1;
    const int fieldH   = (static_cast<int>(rows) - 3 - fieldTop) / 2;
    if (fieldH < 3) {
        Label(p, y, 2, kDim, "Terminal too short for the comparison fields.");
        return;
    }

    Label(p, y, 2, kText, "C) Colour across, coverage down -- this one fades into whatever is behind the window:");
    const int fieldSubTop = fieldTop * 4;
    const int fieldSubH   = fieldH * 4;
    for (int r = 0; r < fieldH; ++r) {
        for (int c = 0; c < w; ++c) {
            const double u = w > 1 ? static_cast<double>(c) / (w - 1) : 0.0;
            PutDitheredCell(p, fieldTop + r, x + c, HueRamp(u), [&](int subY, int) {
                const double v = fieldSubH > 1 ? static_cast<double>(subY - fieldSubTop) / (fieldSubH - 1) : 0.0;
                return 1.0 - v;
            });
        }
    }

    const int contrastTop = fieldTop + fieldH + 1;
    Label(p, contrastTop - 1, 2, kText,
          "D) The same field as background colours instead -- opaque, and it stops at the terminal:");
    for (int r = 0; r < fieldH; ++r) {
        for (int c = 0; c < w; ++c) {
            const double u = w > 1 ? static_cast<double>(c) / (w - 1) : 0.0;
            const double v = fieldH > 1 ? static_cast<double>(r) / (fieldH - 1) : 0.0;
            PutCell(p, contrastTop + r, x + c, Lerp(Rgb{0, 0, 0}, HueRamp(u), 1.0 - v), kText, " ");
        }
    }
}

using PageFn = void (*)(notcurses*, ncplane*, std::vector<ncplane*>&);

const PageFn kPages[] = {
    PageCapabilities,
    PageBackgroundAlpha,
    PageForegroundAlpha,
    PageOverlap,
    PageGradientDirections,
    PageOpacityGradients,
    PageVisualOpacity,
    PageColourAndOpacity,
    PageFullWindowLadder,
    PageBleedThrough,
};
const int kPageCount = static_cast<int>(sizeof(kPages) / sizeof(kPages[0]));

} // namespace

int main(int argc, char** argv) {
    notcurses_options opts{};
    opts.flags = NCOPTION_NO_QUIT_SIGHANDLERS | NCOPTION_SUPPRESS_BANNERS;

    // --termtype overrides the terminfo entry Notcurses opens instead of
    // $TERM. See PageCapabilities() for what that does and does not change.
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--termtype") == 0 && i + 1 < argc) {
            opts.termtype = argv[++i];
        }
        else {
            std::fprintf(stderr, "usage: %s [--termtype <terminfo-entry>]\n", argv[0]);
            return 2;
        }
    }

    notcurses* nc = notcurses_core_init(&opts, nullptr);
    if (nc == nullptr) {
        std::fprintf(stderr, "notcurses_core_init failed -- are you running this in a real terminal?\n");
        return 1;
    }

    ncplane* std_plane = notcurses_stdplane(nc);

    unsigned rows = 0;
    unsigned cols = 0;
    ncplane_dim_yx(std_plane, &rows, &cols);

    std::vector<ncplane*> owned;
    int                   page = 0;
    bool                  run  = true;

    while (run) {
        for (ncplane* plane : owned) {
            ncplane_destroy(plane);
        }
        owned.clear();
        ncplane_erase(std_plane);
        ncplane_set_fg_default(std_plane);
        ncplane_set_bg_default(std_plane);

        if (rows < 36 || cols < 88) {
            Label(std_plane, 1, 2, kText, "Terminal is %ux%u; this probe wants at least 88 cols x 36 rows.", cols, rows);
            Label(std_plane, 2, 2, kDim, "Resize and press any key to redraw, or q to quit.");
        }
        else {
            gPageIndex = page;
            gPageCount = kPageCount;
            kPages[page](nc, std_plane, owned);
        }

        Label(std_plane, static_cast<int>(rows) - 1, 2, kDim,
              "page %d/%d   SPACE/n/RIGHT next   p/LEFT prev   m ladder mechanism   q quit", page + 1, kPageCount);

        notcurses_render(nc);

        ncinput        ni;
        const uint32_t key = notcurses_get_blocking(nc, &ni);
        if (ni.evtype == NCTYPE_RELEASE) {
            continue;
        }
        switch (key) {
            case 'q':
            case 'Q':
            case NCKEY_ESC:
                run = false;
                break;
            case 'p':
            case 'P':
            case NCKEY_LEFT:
            case NCKEY_UP:
                page = (page + kPageCount - 1) % kPageCount;
                break;
            case 'm':
            case 'M':
                gMechanism = (gMechanism + 1) % kMechanismCount;
                break;
            case NCKEY_RESIZE:
                notcurses_refresh(nc, &rows, &cols);
                break;
            default:
                page = (page + 1) % kPageCount;
                break;
        }
    }

    for (ncplane* plane : owned) {
        ncplane_destroy(plane);
    }
    notcurses_stop(nc);
    return 0;
}
