//
// Org capture templates (org-mode v2+ follow-up): a Janet-registered named
// template that a single keystroke (see UI/BufferView.cpp's
// HandleOrgCaptureKey) expands into a target file, optionally filed under a
// specific headline. Deliberately v1-scoped against real Emacs org-capture:
// "%?" is the only supported escape (marks where point lands after
// expansion; every other real-Org escape -- %U/%a/%i/timestamps -- is a
// follow-up), there's no separate finalize/discard step because expansion
// inserts straight into the real target buffer rather than a temporary
// indirect one (the caller just switches to it and keeps editing normally),
// and headline targeting is an exact (case-sensitive) title match only, the
// same contract FindHeadlineByTitle already establishes -- no fuzzy/partial
// matching, no per-file default headline.
//
// Two layers, matching Org.h's own "pure parse, then a thin Buffer-mutating
// wrapper" split: ExpandCaptureTemplate is pure (string in, string +
// optional cursor offset out); InsertCapture is the Buffer-mutating half,
// built entirely on Buffer's existing InsertAt (no new Buffer primitive,
// same precedent every other construct in Org.h follows).
//

#ifndef NED_EDITOR_ORGCAPTURE_H
#define NED_EDITOR_ORGCAPTURE_H

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "Text/Buffer.h"

namespace ned::editor::org {

// One registered template. headline empty means "no headline target -- file
// at the end of targetFile."
struct CaptureTemplate {
    char        key;
    std::string name;
    std::string targetFile;
    std::string templateText;
    std::string headline;
};

// Process-wide, mutex-guarded registry (mirrors TabWidth.h's exact pattern)
// keyed by CaptureTemplate::key -- re-registering an existing key overwrites
// it, matching CommandRegistry's own "redefining is expected use" precedent.
// Janet-only surface (ned/org-capture-register-template); no bundled
// defaults.
void RegisterCaptureTemplate(CaptureTemplate tmpl);

// In key order, for listing every available template (e.g. in the capture
// prompt's own status line).
[[nodiscard]] std::vector<CaptureTemplate> CaptureTemplates();

[[nodiscard]] std::optional<CaptureTemplate> CaptureTemplateForKey(char key);

// Splits templateText on its first "%?" -- text is templateText with that
// first "%?" removed (subsequent occurrences, if any, are left as literal
// text -- only the first is special, matching real Org's own single-
// placeholder behavior), cursorOffset is that removed placeholder's byte
// offset into `text` (nullopt if templateText has no "%?" at all, meaning
// "place point at the end of the expanded text"). Pure -- no Buffer
// involved, independently unit-tested.
struct CaptureExpansion {
    std::string                text;
    std::optional<std::size_t> cursorOffset;
};

[[nodiscard]] CaptureExpansion ExpandCaptureTemplate(const std::string& templateText);

// The Buffer-mutating half: resolves where tmpl.headline lands in target
// (ParseOutline + exact-title match, inserting at SubtreeEndLine's line
// start -- i.e. as the matched headline's own subtree's last child), or
// falls back to end-of-buffer if tmpl.headline is empty or wasn't found;
// expands tmpl.templateText via ExpandCaptureTemplate, inserts it (adding a
// leading newline first if inserting after a line that doesn't already end
// in one, the same "ensure a fresh line" handling SetProperty's own
// no-existing-drawer branch uses), and reports where the caller should place
// point.
struct CaptureResult {
    std::size_t insertedAt;    // where point should land, in target's post-insert content
    bool        headlineFound; // false when tmpl.headline was non-empty but not found (fell back to EOF)
};

CaptureResult InsertCapture(text::Buffer& target, const CaptureTemplate& tmpl);

} // namespace ned::editor::org

#endif // NED_EDITOR_ORGCAPTURE_H
