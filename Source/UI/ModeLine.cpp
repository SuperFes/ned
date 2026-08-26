#include "ModeLine.h"

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
        using Status = editor::lsp::LspManager::LspStatus;
        // mode-line-lsp-status-round-3 follow-up: same "detail text after a
        // space" shape reused by both the single-glyph and multi-glyph
        // branches below.
        const auto glyphAndDetailFor = [this](const std::string& key, std::string_view& glyph, std::string& detail) {
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
    const bool  focused       = focusProvider_ && focusProvider_();
    const Color gradientStart = focused ? theme_.modeLineFocusedGradientStart : theme_.modeLineGradientStart;
    const Color gradientEnd   = focused ? theme_.modeLineFocusedGradientEnd : theme_.modeLineGradientEnd;

    for (int x = 0; x < c.size().width; ++x) {
        Cell& cell     = c[{.x = x, .y = 0}];
        cell.character = (static_cast<std::size_t>(x) < columns.size()) ? columns[static_cast<std::size_t>(x)] : " ";

        // A left-to-right gradient across the whole row rather than a flat
        // fill -- the one "gradient" deliverable of Phase 6, chosen because
        // it's always visible without needing any animation/timer machinery.
        const float percent   = (c.size().width > 1) ? static_cast<float>(x) / static_cast<float>(c.size().width - 1) : 0.0F;
        cell.background_color = Color::Interpolate(percent, gradientStart, gradientEnd);
        cell.foreground_color = theme_.modeLineForeground;
    }
}

void ModeLine::SetFocusProvider(std::function<bool()> provider) {
    focusProvider_ = std::move(provider);
}

void ModeLine::SetLspManager(editor::lsp::LspManager* lspManager) {
    lspManager_ = lspManager;
}

void ModeLine::SetLanguageAtPointProvider(std::function<std::optional<std::string>()> provider) {
    languageAtPointProvider_ = std::move(provider);
}

} // namespace ned::ui
