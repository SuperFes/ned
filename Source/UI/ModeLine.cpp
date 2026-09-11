#include "ModeLine.h"

#include "Paint.h"
#include "ThemePaints.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "Editor/BackgroundActivity.h"
#include "Editor/Org.h"
#include "Text/LineEnding.h"
#include "Text/Utf8.h"

namespace ned::ui {

namespace {

    // background-activity-spinner follow-up. Braille spinner frames --
    // matches the minimap's existing braille glyph vocabulary, no patched
    // font or double-width rendering risk (the same reasoning behind
    // ProjectSidebar's own glyph choices). Each frame is one multi-byte
    // UTF-8 glyph occupying exactly one cell, which is why the paint loop
    // below works in per-column cell strings rather than raw bytes.
    constexpr std::array<std::string_view, 10> kSpinnerFrames = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};

    // Translucency phase 5, state-driven mode-line fill: how wide the
    // travelling activity band is, in columns. Wide enough to read as motion
    // on a 160-column bar, narrow enough that most of the bar is never tinted
    // at any one moment.
    constexpr int kActivityBandColumns = 12;

    std::string_view CurrentSpinnerFrame() {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch());
        return kSpinnerFrames[static_cast<std::size_t>((elapsed / editor::kBackgroundActivitySpinnerInterval) % kSpinnerFrames.size())];
    }

    // Appends one `columns` entry per codepoint in text (codepoint-granular,
    // not full grapheme-cluster-aware, matching BufferView's own
    // one-codepoint-per-cell content rendering) -- replaces what used to be
    // a byte-per-column loop repeated at every one of this file's dynamic-
    // text call sites (buffer name, Org clock headline title, background-
    // activity name/detail, LSP status detail), which split any multi-byte
    // UTF-8 character in that text across as many blank-looking cells as it
    // had bytes (found live via EchoArea's identical bug, see ROADMAP.md).
    void AppendUtf8Columns(std::vector<std::string>& columns, std::string_view text) {
        std::size_t i = 0;
        while (i < text.size()) {
            const std::size_t next = text::NextCodepointBoundary(text, i);
            columns.emplace_back(text.substr(i, next - i));
            i = next;
        }
    }

    // minimum-visible-duration follow-up: see lastShownActivities_' own doc
    // comment in ModeLine.h.
    constexpr std::chrono::milliseconds kMinimumVisibleDuration{300};

} // namespace

ModeLine::ModeLine(const ActiveBuffer& activeBuffer, const editor::Mode& mode, const Theme& theme) : activeBuffer_(activeBuffer), mode_(mode), theme_(theme) {
}

void ModeLine::Paint(Canvas c) {
    const text::Buffer& buffer    = activeBuffer_.Get();
    const auto&         content   = buffer.Content();
    const std::size_t   point     = buffer.Point();
    const std::size_t   line      = content.ByteOffsetToLine(point);
    const std::size_t   lineStart = content.LineToByteOffset(line);
    const std::size_t   col       = content.ByteOffsetToCodepointOffset(point) - content.ByteOffsetToCodepointOffset(lineStart);

    const std::string modifiedMarker = buffer.Modified() ? "*" : " "; // fixed width -- keeps L/C from jittering
    // large-file-async-load follow-up: while the buffer is still filling in
    // from a background AsyncFileLoader, show that instead of the mode name
    // -- there's nothing meaningful to report for L/C or a mode against
    // content that isn't fully there yet, and this is the one existing,
    // already-per-frame-refreshed place a "this is still loading" signal
    // can surface without any new plumbing (buffer.IsLoading() is a plain
    // Buffer query, same as Modified()/Point() just above).
    // large-file-async-load polish: a live percentage when the loader
    // published one (Buffer::CurrentLoadProgress; totalBytes 0 means the
    // size query failed -- fall back to the old plain indicator rather than
    // dividing by it). bytesRead can momentarily exceed totalBytes if the
    // file grew after the size query, hence the clamp.
    std::string loadingText = "   Loading...";
    if (buffer.IsLoading()) {
        if (const text::LoadProgress* progress = buffer.CurrentLoadProgress();
            progress != nullptr && progress->totalBytes > 0) {
            const std::uintmax_t read    = progress->bytesRead.load(std::memory_order_relaxed);
            const std::uintmax_t percent = std::min<std::uintmax_t>(100, read * 100 / progress->totalBytes);
            loadingText += " " + std::to_string(percent) + "%";
        }
    }

    // embedded-language-documents follow-up: shown next to the mode name
    // only while point sits inside an embedded region (e.g. "[javascript]"
    // inside an HTML <script> block) -- nothing extra for the ordinary
    // single-language case, no visual noise for the common path.
    std::string embeddedLanguageSuffix;
    if (!buffer.IsLoading() && languageAtPointProvider_) {
        if (const std::optional<std::string> language = languageAtPointProvider_()) {
            embeddedLanguageSuffix = " [" + *language + "]";
        }
    }

    // crlf-handling follow-up: always shown (not gated behind a Set*
    // provider like embeddedLanguageSuffix above) -- ModeLine already has
    // direct buffer access for everything else on this line, and unlike an
    // embedded language this is meaningful for every buffer, not just a
    // rare per-point case.
    const std::string lineEndingSuffix = std::string("  ") + text::LineEndingName(buffer.LineEndingKind());

    const std::string text = buffer.IsLoading() ? "  " + buffer.Name() + loadingText
                                                : "  " + modifiedMarker + buffer.Name() + "   L" + std::to_string(line + 1) +
                                                      ":C" + std::to_string(col + 1) + "  (" + mode_.name + ")" + embeddedLanguageSuffix +
                                                      lineEndingSuffix;

    // background-activity-spinner follow-up: one column-per-entry cell list
    // instead of the raw byte string above, so the spinner's multi-byte
    // braille glyph occupies exactly one cell, and (AppendUtf8Columns above)
    // so does every other multi-byte UTF-8 character in the dynamic text
    // built up below (a non-ASCII buffer name, an Org headline title, an
    // activity/LSP detail string, ...).
    std::vector<std::string> columns;
    columns.reserve(text.size() + 32);
    AppendUtf8Columns(columns, text);

    // org-clock-display follow-up: a live "clocked in on X since HH:MM"
    // indicator, scoped to the active buffer only -- clock state isn't
    // tracked globally across buffers in this codebase (Editor/Org.h's own
    // top comment, item 8), so switching away from the clocked-in buffer
    // simply stops showing it, the same buffer-scoped posture
    // ClockInAtPoint/ClockOut themselves already have. Recomputed fresh
    // every Paint() call, same direct now()-read CurrentSpinnerFrame()
    // above already is -- no timer, no cached elapsed value.
    if (mode_.name == "org-mode" && !buffer.IsLoading()) {
        if (const auto running = editor::org::CurrentlyRunningClock(buffer.Text())) {
            const long long    elapsed = editor::org::ElapsedMinutes(running->start).count();
            std::ostringstream out;
            out << (elapsed / 60) << ':' << std::setfill('0') << std::setw(2) << (elapsed % 60);
            columns.emplace_back(" ");
            columns.emplace_back(" ");
            columns.emplace_back("⏱");
            columns.emplace_back(" ");
            AppendUtf8Columns(columns, running->headline.title);
            columns.emplace_back(" ");
            AppendUtf8Columns(columns, out.str());
        }
    }

    // minimum-visible-duration follow-up: fall back to the last non-empty
    // snapshot for a little while after the real list goes empty -- see
    // lastShownActivities_'s own doc comment in ModeLine.h.
    std::vector<editor::BackgroundActivity> activities = editor::ActiveBackgroundActivities();
    const auto                              now        = std::chrono::steady_clock::now();
    if (!activities.empty()) {
        lastShownActivities_   = activities;
        lastShownActivitiesAt_ = now;
    }
    else if (!lastShownActivities_.empty() && now - lastShownActivitiesAt_ < kMinimumVisibleDuration) {
        activities = lastShownActivities_;
    }
    else {
        lastShownActivities_.clear();
    }
    bool lspActivityShown = false;
    if (!activities.empty()) {
        const std::string_view frame = CurrentSpinnerFrame();
        for (const editor::BackgroundActivity& activity : activities) {
            columns.emplace_back(" ");
            columns.emplace_back(" ");
            AppendUtf8Columns(columns, activity.name);
            columns.emplace_back(" ");
            columns.emplace_back(frame);
            if (!activity.detail.empty()) {
                columns.emplace_back(" ");
                AppendUtf8Columns(columns, activity.detail);
            }
            lspActivityShown = lspActivityShown || activity.name == editor::lsp::kLspActivityName;
        }
    }
    // mode-line-lsp-status-round-2 follow-up: beyond "running, idle" (a
    // plain filled dot, deliberately static so it reads as visually distinct
    // from actually-in-flight work at a glance), also surface a spawn
    // failure and a disconnected/crashed server -- previously both silently
    // indistinguishable from "no LSP configured at all." Only drawn when the
    // request-driven block above didn't already draw an "LSP" entry (busy
    // takes priority over any of these, same entry, no duplicate); "not
    // configured" draws nothing, unchanged from before this follow-up.
    if (!lspActivityShown && lspManager_) {
        using Status = editor::lsp::Manager::Status;
        // mode-line-lsp-status-round-3 follow-up: same "detail text after a
        // space" shape reused by both the single-glyph and multi-glyph
        // branches below.
        // LSP multi-root follow-up: every status latch is keyed by connection,
        // so a plain server key is resolved against this buffer's own root
        // first -- two same-language servers against different roots each
        // report their own state instead of shadowing each other's.
        const auto glyphAndDetailFor = [this, &buffer](const std::string& serverKey, std::string_view& glyph, std::string& detail) {
            const std::string key = lspManager_->ConnectionKeyForBuffer(buffer, serverKey);
            switch (lspManager_->StatusForLanguage(key)) {
                case Status::Running:
                    glyph = "●";
                    break;
                case Status::SpawnFailed:
                    glyph  = "✕";
                    detail = lspManager_->SpawnFailureDetail(key);
                    break;
                case Status::Disconnected:
                    glyph  = "○";
                    detail = lspManager_->DisconnectReason(key);
                    break;
                case Status::NotConfigured:
                    break;
            }
        };

        // embedded-language-documents follow-up: every server key currently
        // synced for this buffer (host language, kProseLanguageKey if that's
        // synced too, any embedded keys) -- iterated only when there's more
        // than one, so the ordinary single-language case renders byte-for-
        // byte identically to before this feature existed.
        const std::vector<std::string> activeKeys = lspManager_->ActiveServerKeysForBuffer(buffer);

        if (activeKeys.size() <= 1) {
            const std::string languageKey = editor::LanguageKeyForMode(mode_);
            std::string_view  glyph;
            std::string       detail;
            glyphAndDetailFor(languageKey, glyph, detail);
            if (!glyph.empty()) {
                columns.emplace_back(" ");
                columns.emplace_back(" ");
                columns.emplace_back("L");
                columns.emplace_back("S");
                columns.emplace_back("P");
                columns.emplace_back(" ");
                columns.emplace_back(glyph);
                if (!detail.empty()) {
                    columns.emplace_back(" ");
                    AppendUtf8Columns(columns, detail);
                }
            }
        }
        else {
            // More than one server is active for this buffer -- one
            // "<key> <glyph>[ <detail>]" segment per key, host language
            // first, then every other key (kProseLanguageKey, embedded
            // languages) sorted for a stable order.
            const std::string        hostKey = editor::LanguageKeyForMode(mode_);
            std::vector<std::string> remainder;
            for (const std::string& key : activeKeys) {
                if (key != hostKey) {
                    remainder.push_back(key);
                }
            }
            std::sort(remainder.begin(), remainder.end());
            std::vector<std::string> ordered;
            if (std::find(activeKeys.begin(), activeKeys.end(), hostKey) != activeKeys.end()) {
                ordered.push_back(hostKey);
            }
            ordered.insert(ordered.end(), remainder.begin(), remainder.end());

            for (const std::string& key : ordered) {
                std::string_view glyph;
                std::string      detail;
                glyphAndDetailFor(key, glyph, detail);
                if (glyph.empty()) {
                    continue;
                }
                columns.emplace_back(" ");
                columns.emplace_back(" ");
                AppendUtf8Columns(columns, key);
                columns.emplace_back(" ");
                columns.emplace_back(glyph);
                if (!detail.empty()) {
                    columns.emplace_back(" ");
                    AppendUtf8Columns(columns, detail);
                }
            }
        }
    }

    // Chrome-redesign follow-up: the focused pane's gradient pulls toward
    // the theme accent so which split has the keyboard is visible at a
    // glance -- see SetFocusProvider.
    //
    // Translucency follow-up (Docs/Translucency.md phase 5): the row is a
    // themed Surface now rather than a hand-rolled interpolation. Its
    // derived default is the same left-to-right gradient between the same
    // two theme colours, so an unthemed editor is unchanged; a theme that
    // sets "modeline"/"modeline.focused" gets any paint at all -- including
    // a translucent one, which dithers through to the desktop rather than
    // painting an opaque bar.
    const bool    focused = focusProvider_ && focusProvider_();
    const Surface surface = SurfaceFor(theme_, focused ? "modeline.focused" : "modeline");

    // Paint order: clear, fill, glyphs, then any text fade. The clear
    // matters because a translucent or patterned fill deliberately leaves
    // gaps, and cells persist between frames.
    ClearCanvas(c, ChromeBackdrop(theme_));
    Fill(c, surface.fill);
    PaintActivitySweep(c, !activities.empty());

    for (int x = 0; x < c.size().width; ++x) {
        const Point at{.x = x, .y = 0};
        Cell        glyph;
        glyph.character        = (static_cast<std::size_t>(x) < columns.size()) ? columns[static_cast<std::size_t>(x)] : " ";
        glyph.foreground_color = TextColourAt(surface, c, at, theme_.modeLineForeground);
        c.Blend(at, glyph);
    }

    ApplyTextFade(c, surface);
}

// Translucency phase 5: the state-driven half of "something is working".
// A band of the "modeline.activity" surface travelling along the bar, over
// the mode line's own fill and under its glyphs -- so it tints the bar rather
// than the text, and needs no cooperation from anything that writes to it.
//
// It advances one column per kBackgroundActivitySpinnerInterval, deliberately
// the same clock and cadence CurrentSpinnerFrame reads: the sweep and the
// spinner are two views of one fact, and deriving them from different timers
// is how they end up visibly disagreeing. No animation machinery of its own
// either -- the composition root already re-arms a timer while any activity
// is live, for the spinner.
//
// Gated on the same `activities` the spinner is, which means it also inherits
// the minimum-visible-duration hold: a burst of work too short to see still
// shows both, and neither outlives the other.
void ModeLine::PaintActivitySweep(Canvas& c, bool active) const {
    if (!active) {
        return;
    }
    const Surface sweep = SurfaceFor(theme_, "modeline.activity");
    if (!PaintsColour(sweep.fill)) {
        return;
    }
    const int width = c.size().width;
    if (width <= 0) {
        return;
    }

    const int  band    = std::min(kActivityBandColumns, width);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch());
    const auto ticks   = static_cast<long long>(elapsed / editor::kBackgroundActivitySpinnerInterval);
    // Wraps within the bar rather than sweeping in from off-screen and out
    // the far side. Two reasons, and the second is the real one: motion stays
    // continuous (the band re-enters on the left as it leaves on the right,
    // with no dead interval where nothing is moving), and exactly `band`
    // columns are tinted at every instant, so the effect is observable
    // without a test having to control the clock.
    const int   start  = static_cast<int>(ticks % width);
    const Point origin = c.Origin();

    for (int i = 0; i < band; ++i) {
        const int    x      = (start + i) % width;
        const double u      = band > 1 ? static_cast<double>(i) / (band - 1) : 0.0;
        const Color  colour = PaintColourAt(sweep.fill, u, 0.0, origin.x + x, origin.y);
        if (colour.alpha == 0) {
            continue;
        }
        // Empty character: a space would replace the bar's own glyph. See
        // Overlay.cpp's PaintScrim for the same trap, and what it cost.
        Cell wash;
        wash.character.clear();
        wash.background_color = colour;
        c.Blend({.x = x, .y = 0}, wash);
    }
}

void ModeLine::SetFocusProvider(std::function<bool()> provider) {
    focusProvider_ = std::move(provider);
}

void ModeLine::SetLspManager(editor::lsp::Manager* lspManager) {
    lspManager_ = lspManager;
}

void ModeLine::SetLanguageAtPointProvider(std::function<std::optional<std::string>()> provider) {
    languageAtPointProvider_ = std::move(provider);
}

} // namespace ned::ui
