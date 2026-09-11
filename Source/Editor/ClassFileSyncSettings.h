//
// class-file-sync follow-up: whether ned OFFERS, unprompted, to keep a
// file's name and the single type declared inside it in agreement -- after a
// symbol rename (rename the file to match) and after a file rename (rename
// the type to match). Same mutex-guarded-static-state shape as
// RenameReviewSettings.h/TabWidth.h and the dozens of settings modules
// beside them.
//
// On by default: both offers are a y/n in the echo area over an edit that is
// already reviewable and undoable, and both fire only on the narrow,
// evidence-backed case Editor/ClassFileSync.h's strict tier describes -- the
// file was demonstrably named after this type a moment ago and no longer is.
//
// This gates ONLY the automatic offers. The explicit commands
// (rename-file-to-match-type, rename-type-to-match-file) always work, since
// turning the prompting off means "stop volunteering", never "refuse when I
// ask". That asymmetry is the whole point of the setting.
//

#ifndef NED_EDITOR_CLASSFILESYNCSETTINGS_H
#define NED_EDITOR_CLASSFILESYNCSETTINGS_H

namespace ned::editor {

void               SetClassFileSync(bool enabled);
[[nodiscard]] bool ClassFileSyncEnabled(); // default true

} // namespace ned::editor

#endif // NED_EDITOR_CLASSFILESYNCSETTINGS_H
