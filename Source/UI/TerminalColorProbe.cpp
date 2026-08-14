#include "TerminalColorProbe.h"

#include <algorithm>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

namespace ned::ui {

namespace {

    std::optional<int> HexNibble(char c) {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return 10 + (c - 'a');
        if (c >= 'A' && c <= 'F')
            return 10 + (c - 'A');
        return std::nullopt;
    }

    std::optional<std::uint8_t> HexByte(std::string_view s) {
        if (s.size() < 2) {
            return std::nullopt;
        }
        const auto hi = HexNibble(s[0]);
        const auto lo = HexNibble(s[1]);
        if (!hi || !lo) {
            return std::nullopt;
        }
        return static_cast<std::uint8_t>((*hi << 4) | *lo);
    }

    // Finds `prefix` (e.g. "\x1b]11;") in buffer and, if found, parses the
    // "rgb:RRRR/GGGG/BBBB" (or shorter -- some terminals reply with 2 hex
    // digits per channel instead of 4) payload up to its BEL/ST terminator.
    // Only the first two hex digits of each channel group are used regardless
    // of the group's length, which is the usual convention for truncating a
    // wider-than-8-bit channel value down to a byte.
    std::optional<ox::TrueColor> ExtractColorReply(std::string_view buffer, std::string_view prefix) {
        const auto pos = buffer.find(prefix);
        if (pos == std::string_view::npos) {
            return std::nullopt;
        }

        std::string_view rest = buffer.substr(pos + prefix.size());
        if (!rest.starts_with("rgb:")) {
            return std::nullopt;
        }
        rest.remove_prefix(4);

        const auto  bel = rest.find('\x07');
        const auto  st  = rest.find("\x1b\\");
        std::size_t end = std::string_view::npos;
        if (bel != std::string_view::npos) {
            end = bel;
        }
        if (st != std::string_view::npos && (end == std::string_view::npos || st < end)) {
            end = st;
        }
        if (end == std::string_view::npos) {
            return std::nullopt;
        }

        const std::string_view payload = rest.substr(0, end);

        const auto slash1 = payload.find('/');
        if (slash1 == std::string_view::npos) {
            return std::nullopt;
        }
        const auto slash2 = payload.find('/', slash1 + 1);
        if (slash2 == std::string_view::npos) {
            return std::nullopt;
        }

        const auto r = HexByte(payload.substr(0, slash1));
        const auto g = HexByte(payload.substr(slash1 + 1, slash2 - slash1 - 1));
        const auto b = HexByte(payload.substr(slash2 + 1));
        if (!r || !g || !b) {
            return std::nullopt;
        }

        return ox::TrueColor{ox::RGB{*r, *g, *b}};
    }

    ox::TrueColor Tint(ox::TrueColor base, int delta) {
        const auto clamp = [delta](std::uint8_t channel) {
            return static_cast<std::uint8_t>(std::clamp(static_cast<int>(channel) + delta, 0, 255));
        };
        return ox::TrueColor{ox::RGB{clamp(base.red), clamp(base.green), clamp(base.blue)}};
    }

    // RAII: puts stdin into raw/non-canonical mode for the probe and restores
    // whatever was there before, no matter how ProbeTerminalColors returns.
    class RawModeGuard {
      public:
        RawModeGuard() {
            if (::tcgetattr(STDIN_FILENO, &original_) != 0) {
                valid_ = false;
                return;
            }
            valid_ = true;

            termios raw = original_;
            raw.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));
            raw.c_cc[VMIN]  = 0;
            raw.c_cc[VTIME] = 0;
            ::tcsetattr(STDIN_FILENO, TCSANOW, &raw);
        }

        ~RawModeGuard() {
            if (valid_) {
                ::tcsetattr(STDIN_FILENO, TCSANOW, &original_);
            }
        }

        RawModeGuard(const RawModeGuard&)            = delete;
        RawModeGuard& operator=(const RawModeGuard&) = delete;

        [[nodiscard]] bool Valid() const {
            return valid_;
        }

      private:
        termios original_{};
        bool    valid_ = false;
    };

} // namespace

std::string BuildColorQuery() {
    std::string out;
    out += "\x1b]10;?\x07";
    out += "\x1b]11;?\x07";
    for (int i = 0; i < 16; ++i) {
        out += "\x1b]4;" + std::to_string(i) + ";?\x07";
    }
    return out;
}

DetectedColors ParseColorReplies(std::string_view buffer) {
    DetectedColors result;
    result.foreground = ExtractColorReply(buffer, "\x1b]10;");
    result.background = ExtractColorReply(buffer, "\x1b]11;");
    for (int i = 0; i < 16; ++i) {
        result.palette[static_cast<std::size_t>(i)] = ExtractColorReply(buffer, "\x1b]4;" + std::to_string(i) + ";");
    }
    return result;
}

DetectedColors ProbeTerminalColors(std::chrono::milliseconds timeout) {
    RawModeGuard raw;
    if (!raw.Valid()) {
        return {}; // not backed by a real terminal (e.g. stdin redirected) -- nothing to probe
    }

    const std::string query = BuildColorQuery();
    if (::write(STDOUT_FILENO, query.data(), query.size()) < 0) {
        return {};
    }

    constexpr int kExpectedReplies = 18; // OSC 10 + OSC 11 + OSC 4;0..15

    std::string buffer;
    const auto  deadline = std::chrono::steady_clock::now() + timeout;

    while (true) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            break;
        }

        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        pollfd     pfd{.fd = STDIN_FILENO, .events = POLLIN, .revents = 0};
        const int  ready = ::poll(&pfd, 1, static_cast<int>(remaining.count()));
        if (ready <= 0) {
            break; // timeout or error -- stop waiting and parse whatever arrived
        }

        char          chunk[256];
        const ssize_t n = ::read(STDIN_FILENO, chunk, sizeof(chunk));
        if (n <= 0) {
            break;
        }
        buffer.append(chunk, static_cast<std::size_t>(n));

        // Every reply this probe expects is BEL-terminated (it queried with
        // BEL terminators); a well-behaved terminal that answered
        // everything lets us stop well before the timeout instead of always
        // waiting the full window.
        if (std::count(buffer.begin(), buffer.end(), '\x07') >= kExpectedReplies) {
            break;
        }
    }

    return ParseColorReplies(buffer);
}

Theme BuildDetectedTheme(const DetectedColors& detected, const Theme& fallback) {
    Theme result = fallback;

    if (detected.background) {
        result.background          = *detected.background;
        result.echoArea.background = *detected.background;
    }
    if (detected.foreground) {
        result.defaultForeground = *detected.foreground;
    }

    // Same semantic slots DarkTheme() already references symbolically via
    // ox::XColor (whose indices match the standard ANSI palette numbering
    // OSC 4 uses): 2=green, 3=yellow, 4=blue, 5=magenta, 8=bright black,
    // 11=bright yellow, 15=bright white.
    if (detected.palette[8]) {
        result.commentForeground    = *detected.palette[8];
        result.lineNumberForeground = *detected.palette[8]; // gutter reads as muted, like comments
    }
    if (detected.palette[2]) {
        result.stringForeground = *detected.palette[2];
    }
    if (detected.palette[4]) {
        result.keywordForeground   = *detected.palette[4];
        result.selectionBackground = *detected.palette[4];
    }
    if (detected.palette[5]) {
        result.numberForeground = *detected.palette[5];
    }
    if (detected.palette[15]) {
        result.modeLineForeground          = *detected.palette[15];
        result.currentLineNumberForeground = *detected.palette[15];
    }
    if (detected.palette[11]) {
        result.echoArea.foreground = *detected.palette[11];
    }
    if (detected.palette[3]) {
        result.isearchMatchBackground = *detected.palette[3];
    }

    // No ANSI-palette slot corresponds to "mode-line gradient", so derive it
    // as two tints of the detected background instead of leaving it at
    // fallback's fixed values -- keeps a detected theme visually coherent
    // rather than half-detected.
    if (detected.background) {
        result.modeLineGradientStart = Tint(*detected.background, 30);
        result.modeLineGradientEnd   = Tint(*detected.background, -20);
    }

    return result;
}

} // namespace ned::ui
