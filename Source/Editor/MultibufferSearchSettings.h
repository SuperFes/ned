//
// multibuffer-scoped-search follow-up: the one process-wide toggle for
// whether isearch/query-replace inside a multibuffer confine themselves to
// excerpt *bodies*, mirroring MultibufferLimits.h/MultibufferFoldSettings.h/
// TabWidth.h's exact mutex-guarded-static-state pattern.
//
// On (the default) is what makes searching a review buffer behave like
// searching the files it shows: header paths, rule lines and the blank
// separators between excerpts stop matching, and query-replace stops
// offering a replacement inside chrome that Buffer::CanInsertAtExcerpt would
// then silently refuse. Off restores the plain whole-composite search, which
// is the honest answer for "find the excerpt whose header names Foo.cpp" --
// a real use, just not the common one.
//
// Applies only to a buffer that actually carries excerpts
// (multibuffer::ExcerptBodyRanges non-empty). Every ordinary buffer is
// unaffected by this setting in either position.
//

#ifndef NED_EDITOR_MULTIBUFFERSEARCHSETTINGS_H
#define NED_EDITOR_MULTIBUFFERSEARCHSETTINGS_H

namespace ned::editor {

void               SetMultibufferScopedSearch(bool enabled);
[[nodiscard]] bool MultibufferScopedSearch(); // default true

} // namespace ned::editor

#endif // NED_EDITOR_MULTIBUFFERSEARCHSETTINGS_H
