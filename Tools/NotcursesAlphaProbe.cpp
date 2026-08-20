//
// A standalone, throwaway diagnostic -- not part of ned itself, not linked
// against ned_lib. Built to empirically answer a real question raised while
// building the diff gutter's line-tint feature: can Notcurses' own
// NCALPHA_BLEND genuinely composite a color against a real terminal's true
// (possibly picture/transparent) background, or does it only ever blend
// against colors Notcurses itself already knows (other planes it drew),
// falling back to something else -- an approximation, or plain opaque --
// once nothing is known to blend against?
//
// Run it directly in a real terminal (it needs a real tty, same as ned
// itself): ./build/notcurses_alpha_probe
//
// Reads one keypress, then exits and restores the terminal.
//

#include <cstdio>

#include <notcurses/notcurses.h>

namespace {

void DrawLabel(ncplane* plane, int& y, const char* text) {
    ncplane_set_fg_default(plane);
    ncplane_set_bg_default(plane);
    ncplane_putstr_yx(plane, y, 2, text);
    y += 1;
}

void FillRow(ncplane* plane, int y, int startX, int width, const nccell& proto) {
    for (int x = startX; x < startX + width; ++x) {
        ncplane_putc_yx(plane, y, x, &proto);
    }
}

} // namespace

int main() {
    // Same init flags EventLoop.cpp uses for the real editor -- see that
    // file's own header comment for why each one matters.
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
    const int rowWidth = static_cast<int>(cols) > 44 ? 40 : static_cast<int>(cols) - 4;

    int y = 1;

    DrawLabel(std_plane, y, "Notcurses alpha/background probe -- what you see below answers a real question:");
    DrawLabel(std_plane, y, "can NCALPHA_BLEND composite against a truly transparent terminal, or not?");
    y += 1;

    // A) Sanity check: plain opaque true-color background. If this doesn't
    // look like a solid, correct green, something more basic than alpha is
    // broken (true-color support itself, or this probe's own terminal).
    DrawLabel(std_plane, y, "A) Opaque true-color green background (sanity check -- should look solid green):");
    {
        nccell c = NCCELL_TRIVIAL_INITIALIZER;
        nccell_set_bg_rgb8(&c, 0, 180, 0);
        nccell_set_bg_alpha(&c, NCALPHA_OPAQUE);
        nccell_set_fg_rgb8(&c, 255, 255, 255);
        nccell_load(std_plane, &c, " ");
        FillRow(std_plane, y, 2, rowWidth, c);
        ncplane_set_fg_rgb8(std_plane, 255, 255, 255);
        ncplane_set_bg_rgb8(std_plane, 0, 180, 0);
        ncplane_putstr_yx(std_plane, y, 4, "some code text here");
        nccell_release(std_plane, &c);
    }
    y += 2;

    // B) NCALPHA_BLEND with nothing else known beneath it on the stdplane
    // itself -- the exact case the diff-gutter tint hit against
    // Color::Default. If your terminal's real (transparent/picture)
    // background shows through here, blended with green, Notcurses CAN do
    // real terminal-level alpha and the earlier "not possible" conclusion
    // was wrong. If this instead looks either fully opaque green or some
    // fixed gray/black blended with green, it's compositing against a
    // fallback color, not your real backdrop.
    DrawLabel(std_plane, y, "B) NCALPHA_BLEND green @ ~50% directly on the standard plane (nothing else drawn under it):");
    {
        nccell c = NCCELL_TRIVIAL_INITIALIZER;
        nccell_set_bg_rgb8(&c, 0, 200, 0);
        nccell_set_bg_alpha(&c, NCALPHA_BLEND);
        nccell_set_fg_rgb8(&c, 255, 255, 255);
        nccell_set_fg_alpha(&c, NCALPHA_BLEND);
        nccell_load(std_plane, &c, " ");
        FillRow(std_plane, y, 2, rowWidth, c);
        nccell_release(std_plane, &c);
    }
    ncplane_set_bg_default(std_plane);
    ncplane_set_fg_rgb8(std_plane, 255, 255, 255);
    ncplane_putstr_yx(std_plane, y, 4, "some code text here");
    y += 2;

    // C) NCALPHA_BLEND with a KNOWN dark-gray plane placed underneath it
    // (a real, separate ncplane Notcurses genuinely knows the color of) --
    // the control case. This should visibly average toward gray-green
    // regardless of your terminal's own real background, since there IS a
    // known color for Notcurses to blend against this time.
    DrawLabel(std_plane, y, "C) NCALPHA_BLEND green @ ~50% over a KNOWN dark-gray plane placed beneath it (control case):");
    {
        ncplane_options popts{};
        popts.y    = y;
        popts.x    = 2;
        popts.rows = 1;
        popts.cols = static_cast<unsigned>(rowWidth);
        ncplane* under = ncplane_create(std_plane, &popts);
        ncplane_set_bg_rgb8(under, 30, 30, 30);
        for (int x = 0; x < rowWidth; ++x) {
            ncplane_putchar_yx(under, 0, x, ' ');
        }

        nccell c = NCCELL_TRIVIAL_INITIALIZER;
        nccell_set_bg_rgb8(&c, 0, 200, 0);
        nccell_set_bg_alpha(&c, NCALPHA_BLEND);
        nccell_set_fg_rgb8(&c, 255, 255, 255);
        nccell_set_fg_alpha(&c, NCALPHA_BLEND);
        nccell_load(std_plane, &c, " ");
        FillRow(std_plane, y, 2, rowWidth, c);
        nccell_release(std_plane, &c);
    }
    ncplane_set_fg_rgb8(std_plane, 255, 255, 255);
    ncplane_putstr_yx(std_plane, y, 4, "some code text here");
    y += 2;

    // D) NCALPHA_TRANSPARENT -- pure pass-through, the same thing
    // Color::Default already does today via ncplane_set_bg_default. Shown
    // for direct comparison against B.
    DrawLabel(std_plane, y, "D) NCALPHA_TRANSPARENT (pure pass-through -- same as today's Color::Default):");
    {
        nccell c = NCCELL_TRIVIAL_INITIALIZER;
        nccell_set_bg_alpha(&c, NCALPHA_TRANSPARENT);
        nccell_set_fg_rgb8(&c, 255, 255, 255);
        nccell_load(std_plane, &c, " ");
        FillRow(std_plane, y, 2, rowWidth, c);
        nccell_release(std_plane, &c);
    }
    ncplane_set_bg_default(std_plane);
    ncplane_set_fg_rgb8(std_plane, 255, 255, 255);
    ncplane_putstr_yx(std_plane, y, 4, "some code text here");
    y += 3;

    ncplane_set_fg_default(std_plane);
    ncplane_set_bg_default(std_plane);
    ncplane_putstr_yx(std_plane, y, 2, "Press any key to exit...");

    notcurses_render(nc);

    ncinput ni;
    notcurses_get_blocking(nc, &ni);

    notcurses_stop(nc);
    return 0;
}
