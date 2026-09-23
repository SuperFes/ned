//
// change-signature follow-up: how many project files a single signature
// change will ever consider when searching for call sites and other
// same-name signatures. Same mutex-guarded-static-state shape as
// ImportFixupSettings.h, for the same reason -- change-signature's own
// candidate search is that module's own two-stage precedent (word-bounded
// SearchDirectory pass, then a real parse per candidate), so it needs the
// same bound: an interactive operation fed by a walk that must not grow
// with a pathological tree. What the cap drops is reported, never silently
// omitted.
//

#ifndef NED_EDITOR_CHANGESIGNATURESETTINGS_H
#define NED_EDITOR_CHANGESIGNATURESETTINGS_H

#include <cstddef>

namespace ned::editor {

// Default 20000, matching ImportFixupMaxFiles' own default -- the same
// search shape, the same reasonable bound for the same reason. 0 means
// unlimited.
void                      SetChangeSignatureMaxFiles(std::size_t limit);
[[nodiscard]] std::size_t ChangeSignatureMaxFiles();

} // namespace ned::editor

#endif // NED_EDITOR_CHANGESIGNATURESETTINGS_H
