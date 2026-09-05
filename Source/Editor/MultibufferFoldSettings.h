//
// Auto-collapse-on-build follow-up: three process-wide thresholds governing
// which excerpts BuildMultibuffer (Editor/Multibuffer.h) marks collapsed by
// default, mirroring CodeFoldSettings.h/TabWidth.h's exact mutex-guarded-
// static-state pattern. An excerpt whose own body passes either the
// line-count or byte-length threshold collapses regardless of its position
// (catches a single huge/minified hunk -- a generated or minified file
// showing up in a diff); once the running excerpt count within one
// multibuffer passes the count threshold, every remaining excerpt collapses
// regardless of its own size (catches a plain-large result set, e.g.
// project-find-references on a very common identifier). None of these
// override an already-toggled marker or affect a buffer that isn't a
// multibuffer at all -- BuildMultibuffer only ever sets the *initial* state;
// a later manual code-fold-toggle/unfold-all from there is an entirely
// ordinary FoldMarker change, same as any other fold.
//

#ifndef NED_EDITOR_MULTIBUFFERFOLDSETTINGS_H
#define NED_EDITOR_MULTIBUFFERFOLDSETTINGS_H

#include <cstddef>

namespace ned::editor {

void                      SetMultibufferAutoCollapseLineThreshold(std::size_t lines);
[[nodiscard]] std::size_t MultibufferAutoCollapseLineThreshold(); // default 40

void                      SetMultibufferAutoCollapseByteThreshold(std::size_t bytes);
[[nodiscard]] std::size_t MultibufferAutoCollapseByteThreshold(); // default 2000

void                      SetMultibufferAutoCollapseExcerptCap(std::size_t count);
[[nodiscard]] std::size_t MultibufferAutoCollapseExcerptCap(); // default 100

} // namespace ned::editor

#endif // NED_EDITOR_MULTIBUFFERFOLDSETTINGS_H
