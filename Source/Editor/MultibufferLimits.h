//
// Multibuffer-gaps follow-up: the one process-wide bound on how much work
// building a multibuffer is allowed to do, mirroring MultibufferFoldSettings.h/
// TabWidth.h's exact mutex-guarded-static-state pattern.
//
// Distinct in kind from MultibufferFoldSettings.h's three thresholds, which
// only ever change an excerpt's *initial fold state*: every excerpt is still
// read, stitched and indexed there, so a find-references hit on a very common
// identifier still pays one file read and one composite-string append per
// match no matter how many of them collapse. This is the cap on the excerpts
// themselves -- BuildMultibuffer keeps at most this many and appends a visible
// "N more not shown" note for the remainder, and a caller that pays real I/O
// per excerpt (BufferView's find-references, one file read per resolved LSP
// location) consults it directly so the reads past the cap never happen at
// all. Never a silent truncation: the note line and the command's own status
// message both name what was dropped.
//
// 0 means "no cap" -- the pre-cap behavior verbatim, for anyone who would
// rather wait than lose results.
//

#ifndef NED_EDITOR_MULTIBUFFERLIMITS_H
#define NED_EDITOR_MULTIBUFFERLIMITS_H

#include <cstddef>

namespace ned::editor {

void                      SetMultibufferMaxExcerpts(std::size_t count);
[[nodiscard]] std::size_t MultibufferMaxExcerpts(); // default 500; 0 = unlimited

} // namespace ned::editor

#endif // NED_EDITOR_MULTIBUFFERLIMITS_H
