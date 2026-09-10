//
// Standalone diagnostic, sibling to the Notcurses*Probe tools -- but this
// one deliberately links against nothing at all, Notcurses included, and
// writes escape sequences straight to stdout.
//
// It exists because of a specific claim worth testing rather than
// believing: that Konsole's alpha pipeline honours real per-pixel alpha
// when an image arrives over *iTerm2's* inline-image protocol, even though
// the same image through other paths comes out opaque or hard-cut.
// Notcurses cannot answer that question -- 3.0.17 declares NCPIXEL_ITERM2
// in its enum but never selects or implements it, so no `--termtype` and no
// environment variable makes it emit an iTerm2 image (see
// NotcursesGradientProbe.cpp's capabilities page). Nothing about that is
// specific to Konsole; it is simply a protocol Notcurses does not speak.
//
// So this probe speaks the three graphics protocols itself:
//
//   iterm   OSC 1337 File=inline=1 carrying a PNG with a real alpha channel
//   kitty   APC _G a=T,f=32 carrying raw RGBA
//   sixel   DCS q, whose format has no partial alpha at all -- included as
//           the control case, since it is the one Notcurses would actually
//           use here
//
// Each protocol draws the same two test images: an alpha *staircase* (one
// colour at 0/25/50/75/100% alpha, so partial compositing is readable as
// five distinct steps rather than guessed at) and a continuous field whose
// colour ramps along x while alpha ramps along y. Each is drawn twice --
// once on the terminal's own background, once over a printed band of
// coloured cells -- because those answer different questions: the first
// shows whether alpha reaches the terminal's real backdrop (a translucent
// window, a desktop wallpaper), the second whether it composites against
// what the terminal itself has already drawn.
//
// The PNG is written by hand (stored-deflate zlib stream, CRC32/Adler32
// computed here) rather than pulling in libpng or stb: the encoder is ~60
// lines for RGBA8 and keeps this tool dependency-free.
//
// Run it in the terminal under test -- it is a plain stdout program, no
// raw mode, no alternate screen, so it just prints and exits:
//     ./build/terminal_image_alpha_probe            # all three protocols
//     ./build/terminal_image_alpha_probe --only iterm
//
// It cannot tell you what happened: reading the result is a human job.
//

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

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

// The same four-stop ramp NotcursesGradientProbe uses, so the two probes'
// colour fields are directly comparable.
Rgb HueRamp(double t) {
    static const Rgb kStops[] = {
        Rgb{40, 90, 220},
        Rgb{40, 200, 190},
        Rgb{230, 190, 60},
        Rgb{225, 60, 110},
    };
    const double scaled = t * 3.0;
    int          i      = static_cast<int>(scaled);
    if (i >= 3) {
        i = 2;
    }
    return Lerp(kStops[i], kStops[i + 1], scaled - i);
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

    const uint8_t* At(int x, int y) const {
        return &px[(static_cast<std::size_t>(y) * w + x) * 4];
    }
};

// ------------------------------------------------------------- PNG ------

uint32_t Crc32(const uint8_t* data, std::size_t len, uint32_t crc = 0xffffffffu) {
    static uint32_t table[256];
    static bool     built = false;
    if (!built) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1) != 0 ? 0xedb88320u ^ (c >> 1) : c >> 1;
            }
            table[i] = c;
        }
        built = true;
    }
    for (std::size_t i = 0; i < len; ++i) {
        crc = table[(crc ^ data[i]) & 0xff] ^ (crc >> 8);
    }
    return crc;
}

uint32_t Adler32(const uint8_t* data, std::size_t len) {
    uint32_t a = 1;
    uint32_t b = 0;
    for (std::size_t i = 0; i < len; ++i) {
        a = (a + data[i]) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}

void PushBe32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v >> 24));
    out.push_back(static_cast<uint8_t>(v >> 16));
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v));
}

void PushChunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
    PushBe32(out, static_cast<uint32_t>(data.size()));
    const std::size_t crcStart = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    const uint32_t crc = Crc32(out.data() + crcStart, out.size() - crcStart) ^ 0xffffffffu;
    PushBe32(out, crc);
}

// RGBA8, filter type 0 on every scanline, zlib stream made of stored
// (uncompressed) deflate blocks -- no compressor needed, and every decoder
// must accept it.
std::vector<uint8_t> EncodePng(const RgbaImage& img) {
    std::vector<uint8_t> raw;
    raw.reserve(static_cast<std::size_t>(img.h) * (1 + img.w * 4));
    for (int y = 0; y < img.h; ++y) {
        raw.push_back(0);
        raw.insert(raw.end(), img.At(0, y), img.At(0, y) + static_cast<std::size_t>(img.w) * 4);
    }

    std::vector<uint8_t> z;
    z.push_back(0x78);
    z.push_back(0x01);
    std::size_t offset = 0;
    while (offset < raw.size()) {
        const std::size_t block = raw.size() - offset > 65535 ? 65535 : raw.size() - offset;
        const bool        final = offset + block >= raw.size();
        z.push_back(final ? 1 : 0);
        z.push_back(static_cast<uint8_t>(block & 0xff));
        z.push_back(static_cast<uint8_t>(block >> 8));
        z.push_back(static_cast<uint8_t>(~block & 0xff));
        z.push_back(static_cast<uint8_t>((~block >> 8) & 0xff));
        z.insert(z.end(), raw.begin() + offset, raw.begin() + offset + block);
        offset += block;
    }
    PushBe32(z, Adler32(raw.data(), raw.size()));

    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    std::vector<uint8_t> ihdr;
    PushBe32(ihdr, static_cast<uint32_t>(img.w));
    PushBe32(ihdr, static_cast<uint32_t>(img.h));
    ihdr.push_back(8); // bit depth
    ihdr.push_back(6); // colour type: truecolour with alpha
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    PushChunk(png, "IHDR", ihdr);
    PushChunk(png, "IDAT", z);
    PushChunk(png, "IEND", {});
    return png;
}

std::string Base64(const std::vector<uint8_t>& data) {
    static const char* kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string        out;
    out.reserve((data.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < data.size(); i += 3) {
        const uint32_t a = data[i];
        const uint32_t b = i + 1 < data.size() ? data[i + 1] : 0;
        const uint32_t c = i + 2 < data.size() ? data[i + 2] : 0;
        const uint32_t v = (a << 16) | (b << 8) | c;
        out.push_back(kAlphabet[(v >> 18) & 63]);
        out.push_back(kAlphabet[(v >> 12) & 63]);
        out.push_back(i + 1 < data.size() ? kAlphabet[(v >> 6) & 63] : '=');
        out.push_back(i + 2 < data.size() ? kAlphabet[v & 63] : '=');
    }
    return out;
}

// -------------------------------------------------------- protocols -----

void EmitIterm(const RgbaImage& img, int cellsW, int cellsH) {
    const std::vector<uint8_t> png = EncodePng(img);
    const std::string          b64 = Base64(png);
    std::printf("\033]1337;File=inline=1;size=%zu;width=%d;height=%d;preserveAspectRatio=0:%s\a", png.size(), cellsW,
                cellsH, b64.c_str());
}

// a=T transmits and displays in one go; f=32 is raw RGBA, so the alpha
// channel arrives untouched by any image format's own conventions.
void EmitKitty(const RgbaImage& img, int cellsW, int cellsH) {
    const std::string b64    = Base64(img.px);
    const std::size_t kChunk = 4096;
    for (std::size_t offset = 0; offset < b64.size(); offset += kChunk) {
        const std::size_t len  = b64.size() - offset > kChunk ? kChunk : b64.size() - offset;
        const bool        more = offset + len < b64.size();
        if (offset == 0) {
            std::printf("\033_Ga=T,f=32,s=%d,v=%d,c=%d,r=%d,m=%d;%.*s\033\\", img.w, img.h, cellsW, cellsH, more ? 1 : 0,
                        static_cast<int>(len), b64.data() + offset);
        }
        else {
            std::printf("\033_Gm=%d;%.*s\033\\", more ? 1 : 0, static_cast<int>(len), b64.data() + offset);
        }
    }
}

// Sixel has no partial alpha: a pixel is either painted or left alone. P2=1
// is what makes "left alone" mean "whatever was already there" rather than
// "background colour", so this is sixel's entire answer to transparency.
void EmitSixel(const RgbaImage& img) {
    struct PaletteEntry {
        Rgb colour;
        int index;
    };
    std::vector<PaletteEntry> palette;
    std::vector<int>          indices(static_cast<std::size_t>(img.w) * img.h, -1);

    auto Quantize = [](int v) { return (v / 8) * 8; };

    for (int y = 0; y < img.h; ++y) {
        for (int x = 0; x < img.w; ++x) {
            const uint8_t* p = img.At(x, y);
            if (p[3] < 128) {
                continue; // sixel's only transparency: don't paint it
            }
            const Rgb colour{Quantize(p[0]), Quantize(p[1]), Quantize(p[2])};
            int       found = -1;
            for (const auto& entry : palette) {
                if (entry.colour.r == colour.r && entry.colour.g == colour.g && entry.colour.b == colour.b) {
                    found = entry.index;
                    break;
                }
            }
            if (found < 0 && palette.size() < 250) {
                found = static_cast<int>(palette.size());
                palette.push_back(PaletteEntry{colour, found});
            }
            indices[static_cast<std::size_t>(y) * img.w + x] = found;
        }
    }

    std::printf("\033P0;1;0q\"1;1;%d;%d", img.w, img.h);
    for (const auto& entry : palette) {
        std::printf("#%d;2;%d;%d;%d", entry.index, entry.colour.r * 100 / 255, entry.colour.g * 100 / 255,
                    entry.colour.b * 100 / 255);
    }
    for (int band = 0; band * 6 < img.h; ++band) {
        bool wroteColour = false;
        for (const auto& entry : palette) {
            std::string row;
            bool        any = false;
            for (int x = 0; x < img.w; ++x) {
                int bits = 0;
                for (int r = 0; r < 6; ++r) {
                    const int y = band * 6 + r;
                    if (y < img.h && indices[static_cast<std::size_t>(y) * img.w + x] == entry.index) {
                        bits |= 1 << r;
                    }
                }
                any = any || bits != 0;
                row.push_back(static_cast<char>(63 + bits));
            }
            if (!any) {
                continue;
            }
            if (wroteColour) {
                std::printf("$");
            }
            std::printf("#%d%s", entry.index, row.c_str());
            wroteColour = true;
        }
        std::printf("-");
    }
    std::printf("\033\\");
}

// ------------------------------------------------------------ images ----

RgbaImage AlphaStaircase(int w, int h) {
    RgbaImage img(w, h);
    const Rgb colour{80, 220, 140};
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int step  = (x * 5) / w; // 0..4
            const int alpha = step * 255 / 4;
            img.Set(x, y, colour, alpha);
        }
    }
    return img;
}

RgbaImage ColourAndAlphaField(int w, int h) {
    RgbaImage img(w, h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const double u = w > 1 ? static_cast<double>(x) / (w - 1) : 0.0;
            const double v = h > 1 ? static_cast<double>(y) / (h - 1) : 0.0;
            img.Set(x, y, HueRamp(u), ClampByte((1.0 - v) * 255.0));
        }
    }
    return img;
}

// ------------------------------------------------------------ layout ----

enum class Protocol { Iterm,
                      Kitty,
                      Sixel };

const char* ProtocolName(Protocol protocol) {
    switch (protocol) {
        case Protocol::Iterm:
            return "iTerm2 (OSC 1337, PNG with alpha channel)";
        case Protocol::Kitty:
            return "kitty (APC _G, raw RGBA)";
        default:
            return "sixel (DCS q, no partial alpha by design)";
    }
}

void Emit(Protocol protocol, const RgbaImage& img, int cellsW, int cellsH) {
    switch (protocol) {
        case Protocol::Iterm:
            EmitIterm(img, cellsW, cellsH);
            break;
        case Protocol::Kitty:
            EmitKitty(img, cellsW, cellsH);
            break;
        default:
            EmitSixel(img);
            break;
    }
}

// Draws one image block, optionally over a band of coloured cells.
//
// The ordering here is load-bearing and was got wrong the obvious way
// first: save-cursor / print band / restore-cursor / draw image puts the
// image *below* the band whenever printing the band scrolled the screen,
// because DECSC stores an absolute row and a scroll moves the content out
// from under it. So the rows are reserved up front (the only step allowed
// to scroll), every move after that is relative, the band is painted with
// cursor-down rather than newlines so it cannot scroll, and the save/
// restore pair brackets only the image emission -- by which point no
// further scrolling can happen. The restore is still needed because the
// protocols disagree about whether displaying an image moves the cursor.
void DrawBlock(Protocol protocol, const RgbaImage& img, int cellsW, int cellsH, bool withBand) {
    for (int i = 0; i < cellsH; ++i) {
        std::printf("\n");
    }
    std::printf("\033[%dA\r", cellsH);

    if (withBand) {
        for (int r = 0; r < cellsH; ++r) {
            std::printf("\033[48;2;150;40;90m");
            for (int c = 0; c < cellsW; ++c) {
                std::printf(" ");
            }
            std::printf("\033[0m");
            if (r + 1 < cellsH) {
                std::printf("\033[1B\r");
            }
        }
        std::printf("\033[%dA\r", cellsH - 1);
    }

    std::printf("\0337");
    Emit(protocol, img, cellsW, cellsH);
    std::printf("\0338\033[%dB\r", cellsH);
}

// The question an editor actually has to answer: can a translucent image act
// as a *background* for real text? A highlight band that shows the desktop
// through it is only useful if the line's own glyphs still render on top.
//
// Both orderings are drawn, because which one wins is the whole point: the
// image first and text after is what an editor would do (background, then
// content), while text first and image after is the control that shows
// whether an image simply claims the cells it covers.
void DrawTextOverImage(Protocol protocol, const RgbaImage& band, int cellsW, int cellsH, bool imageFirst) {
    static const char* kLine = "    if (v <= 0.0) {   // real text over a 50% band";

    for (int i = 0; i < cellsH; ++i) {
        std::printf("\n");
    }
    std::printf("\033[%dA\r", cellsH);

    if (imageFirst) {
        std::printf("\0337");
        Emit(protocol, band, cellsW, cellsH);
        std::printf("\0338");
        std::printf("\033[0m%s", kLine); // default background: nothing of our own painted
        std::printf("\r\033[%dB", cellsH);
    }
    else {
        std::printf("\033[0m%s\r", kLine);
        std::printf("\0337");
        Emit(protocol, band, cellsW, cellsH);
        std::printf("\0338\033[%dB", cellsH);
    }
    std::printf("\r");
}

void RunProtocol(Protocol protocol, int cellsW, int cellsH, int pxW, int pxH) {
    const RgbaImage staircase = AlphaStaircase(pxW, pxH);
    const RgbaImage field     = ColourAndAlphaField(pxW, pxH);

    std::printf("\n\033[1m== %s ==\033[0m\n\n", ProtocolName(protocol));

    std::printf("  a) alpha staircase 0/25/50/75/100%%, on the terminal's own background:\n");
    DrawBlock(protocol, staircase, cellsW, cellsH, false);

    std::printf("  b) the same staircase over a solid magenta band of cells:\n");
    DrawBlock(protocol, staircase, cellsW, cellsH, true);

    std::printf("  c) colour ramping across, alpha ramping down, over the same band:\n");
    DrawBlock(protocol, field, cellsW, cellsH, true);

    // A uniform half-alpha band, which is what a current-line highlight would
    // be: one colour, 50% everywhere, and text expected to survive on top.
    RgbaImage band(pxW, pxH);
    for (int y = 0; y < band.h; ++y) {
        for (int x = 0; x < band.w; ++x) {
            band.Set(x, y, Rgb{90, 120, 220}, 128);
        }
    }

    std::printf("  d) a 50%% band drawn FIRST, then text written over those cells:\n");
    DrawTextOverImage(protocol, band, cellsW, 1, true);
    std::printf("     (text legible AND the band still tinting behind it => a translucent highlight is possible)\n");

    std::printf("  e) the same band drawn AFTER the text (control -- does an image claim its cells?):\n");
    DrawTextOverImage(protocol, band, cellsW, 1, false);

    std::printf("  (three blank boxes above = this terminal ignored or dropped the %s sequences)\n",
                protocol == Protocol::Iterm ? "iTerm2" : protocol == Protocol::Kitty ? "kitty"
                                                                                     : "sixel");
}

// Whether it *works* is only half the question: a current-line highlight
// repaints on every cursor motion, so the cost of one band has to be
// affordable per keystroke. This reports what an editor would actually pay --
// encode plus base64 for one band, and the bytes that reach the terminal.
void ReportBandCost(int cellsW, int cellPixelW, int cellPixelH) {
    const int pxW = cellsW * cellPixelW;
    const int pxH = cellPixelH;

    RgbaImage band(pxW, pxH);
    for (int y = 0; y < band.h; ++y) {
        for (int x = 0; x < band.w; ++x) {
            band.Set(x, y, Rgb{90, 120, 220}, 128);
        }
    }

    constexpr int kRuns = 50;
    const auto    start = std::chrono::steady_clock::now();
    std::size_t   bytes = 0;
    for (int i = 0; i < kRuns; ++i) {
        const std::vector<std::uint8_t> png = EncodePng(band);
        const std::string               b64 = Base64(png);
        bytes                               = b64.size();
    }
    const auto   elapsed = std::chrono::steady_clock::now() - start;
    const double perBandMs =
        std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(elapsed).count() / kRuns;

    std::printf("\n\033[1m== cost of one %d-cell band (%dx%d px) ==\033[0m\n", cellsW, pxW, pxH);
    std::printf("  encode + base64: %.2f ms   payload: %zu bytes\n", perBandMs, bytes);
    std::printf("  at 60 fps that is %.1f MB/s to the terminal; a whole SGR text frame is a few KB.\n",
                (static_cast<double>(bytes) * 60.0) / (1024.0 * 1024.0));
    std::printf("  (this encoder uses stored deflate -- no compression at all. Measured against zlib level 6,\n"
                "   the same uniform band is 126 bytes rather than 51216: about 400x smaller, since a flat\n"
                "   colour is exactly what deflate is good at. So the payload is an artefact of this probe,\n"
                "   not of the approach -- a real implementation would compress and pay CPU instead.)\n");
}

void PrintUsage(const char* argv0) {
    std::fprintf(stderr, "usage: %s [--only iterm|kitty|sixel] [--cells WxH]\n", argv0);
}

} // namespace

int main(int argc, char** argv) {
    bool wantIterm = true;
    bool wantKitty = true;
    bool wantSixel = true;
    int  cellsW    = 40;
    int  cellsH    = 6;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--only") == 0 && i + 1 < argc) {
            const char* which = argv[++i];
            wantIterm         = std::strcmp(which, "iterm") == 0;
            wantKitty         = std::strcmp(which, "kitty") == 0;
            wantSixel         = std::strcmp(which, "sixel") == 0;
            if (!wantIterm && !wantKitty && !wantSixel) {
                PrintUsage(argv[0]);
                return 2;
            }
        }
        else if (std::strcmp(argv[i], "--cells") == 0 && i + 1 < argc) {
            if (std::sscanf(argv[++i], "%dx%d", &cellsW, &cellsH) != 2 || cellsW < 4 || cellsH < 2) {
                PrintUsage(argv[0]);
                return 2;
            }
        }
        else {
            PrintUsage(argv[0]);
            return 2;
        }
    }

    // Cell pixel geometry is unknown without querying the terminal, and the
    // protocols scale to the requested cell box anyway -- so the source is
    // sized generously and scaled down rather than guessed exactly.
    const int pxW = cellsW * 8;
    const int pxH = cellsH * 16;

    std::printf("Terminal image alpha probe -- TERM=%s COLORTERM=%s KONSOLE_VERSION=%s\n",
                std::getenv("TERM") != nullptr ? std::getenv("TERM") : "(unset)",
                std::getenv("COLORTERM") != nullptr ? std::getenv("COLORTERM") : "(unset)",
                std::getenv("KONSOLE_VERSION") != nullptr ? std::getenv("KONSOLE_VERSION") : "(unset)");
    if (std::getenv("TMUX") != nullptr) {
        std::printf("\033[1mTMUX is set: graphics escapes are very likely to be swallowed. Run this outside tmux.\033[0m\n");
    }
    std::printf("Each image is %dx%d source pixels displayed in a %dx%d cell box (sixel ignores the box and\n"
                "renders at source size, so its blocks may be smaller than the magenta band behind them).\n",
                pxW, pxH, cellsW, cellsH);
    std::printf("What to look for: five distinct steps in (a)/(b) means real per-pixel alpha; five identical\n");
    std::printf("blocks means alpha ignored; blocks missing entirely means alpha thresholded to on/off.\n");

    if (wantIterm) {
        RunProtocol(Protocol::Iterm, cellsW, cellsH, pxW, pxH);
    }
    if (wantKitty) {
        RunProtocol(Protocol::Kitty, cellsW, cellsH, pxW, pxH);
    }
    if (wantSixel) {
        RunProtocol(Protocol::Sixel, cellsW, cellsH, pxW, pxH);
    }

    ReportBandCost(cellsW, pxW / cellsW, pxH / cellsH);

    std::printf("\n(nothing above is a claim -- report which blocks rendered and how they looked)\n");
    return 0;
}
