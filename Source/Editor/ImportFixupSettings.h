//
// file-rename-propagation follow-up: whether renaming or moving a file also
// rewrites the imports that named it, and the relative imports it wrote
// itself (Editor/ImportFixup.h). Same mutex-guarded-static-state shape as
// RenameReviewSettings.h and the dozens of settings modules beside it.
//
// On by default, and, like every other rename, routed through the review
// multibuffer rather than applied blind -- an unreviewed excerpt commits
// nothing, so the cost of being wrong about an import is a keystroke.
//
// A language server that answered workspace/willRenameFiles with edits of
// its own always wins: its answer is the better-informed one (clangd
// resolves an include through compile_commands.json, which no filesystem
// arithmetic can), and applying both would rewrite the same specifier
// twice. This is the no-server path, exactly as the ROADMAP scoped it.
//

#ifndef NED_EDITOR_IMPORTFIXUPSETTINGS_H
#define NED_EDITOR_IMPORTFIXUPSETTINGS_H

#include <cstddef>

namespace ned::editor {

void               SetImportFixupEnabled(bool enabled);
[[nodiscard]] bool ImportFixupEnabled(); // default true

// How many project files a single rename will ever consider (default
// 20000). A rename is an interactive operation, so the walk that feeds it
// is bounded rather than allowed to grow with a pathological tree; what was
// dropped is reported, never silently omitted.
void                      SetImportFixupMaxFiles(std::size_t limit);
[[nodiscard]] std::size_t ImportFixupMaxFiles();

} // namespace ned::editor

#endif // NED_EDITOR_IMPORTFIXUPSETTINGS_H
