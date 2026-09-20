//
// Transient mode -- a run that must leave no trace behind it.
//
// The motivating case is ned invoked as another tool's $EDITOR on a
// throwaway message file: `git commit` hands over <gitdir>/COMMIT_EDITMSG,
// waits for the process to exit, reads the file back and deletes it. Nothing
// about that run is a file someone is working in, so recording it as one is
// wrong in both directions -- the message file pollutes save-place, recent
// files, persistent undo and the backup tree, and (worst of the set, since
// it destroys real state rather than adding junk) the project session is
// rewritten at quit to contain just the message file, because git runs the
// editor from the repository root and a repository root is exactly what
// HasProjectMarker looks for.
//
// The mode is a composition of switches that already existed, not new
// gating spread through those subsystems: ApplyTransientMode turns off
// save-place, recent files, persistent undo and both backup writers, and
// main.cpp separately declines to establish a project session root or to
// run the quit-time stores. That split matters -- SaveFilePlaces(force) and
// SaveRecentFiles(force) write whatever the in-memory store holds without
// consulting their own enabled flag, so "don't record" alone would still
// rewrite the real store at quit.
//
// IsVcsEditorFile is what makes the mode automatic. Every name below was
// verified by running the tool against a probe editor rather than recalled
// from its source, and the shape of the answer is the same for all five:
// the basename carries a fixed affix and any randomness sits in an infix.
// Detection therefore keys on the basename alone and never on path shape --
// git's directory varies far more than its filenames do (worktrees put it
// under .git/worktrees/<name>/, a rebase under .git/rebase-merge/, GIT_DIR
// may point anywhere at all), and svn and fossil hand over a relative path
// while git and hg hand over an absolute one.
//
// Tools whose editor file is fully random -- crontab -e, sudoedit, cvs, and
// `gh pr create` among them -- cannot be detected this way and are the
// documented case for passing --transient explicitly.
//

#ifndef NED_EDITOR_TRANSIENTSESSION_H
#define NED_EDITOR_TRANSIENTSESSION_H

#include <filesystem>

namespace ned::editor {

// True when path's basename is one a version control system hands its
// configured editor for a message/todo file:
//
//   git      COMMIT_EDITMSG, MERGE_MSG, TAG_EDITMSG, SQUASH_MSG,
//            NOTES_EDITMSG, git-rebase-todo, addp-hunk-edit.diff
//   svn      svn-commit.tmp, svn-commit.<n>.tmp
//   hg       hg-editor-<random>.commit.hg.txt (any hg-editor-*.txt)
//   jj       editor-<random>.jjdescription (any *.jjdescription)
//   fossil   ci-comment-<hex>.txt
//
// The fossil entry is the only one narrow enough to need it: it matches a
// hex infix specifically, not a bare ci-comment-*.txt glob, because
// "ci-comment-template.txt" is a plausible real file in a repository that
// has CI configuration and this must never claim one of those.
[[nodiscard]] bool IsVcsEditorFile(const std::filesystem::path& path);

// Process-wide flag (mutex-guarded static, ScratchPad.h/TabWidth.h's
// pattern). Set once at startup from --transient or from IsVcsEditorFile
// agreeing about a path on the command line; read by the few places that
// need to ask rather than having been switched off.
void               SetTransientMode(bool enabled);
[[nodiscard]] bool TransientMode(); // default false

// Turns off every persistence switch this mode disowns: save-place, recent
// files, persistent undo, the periodic crash-recovery autosave and the
// pre-save version copy. Call after init.janet has loaded, so an explicit
// invocation-time flag wins over a config file's own ned/set-save-place --
// the same ordering --vim already follows for its own setting.
void ApplyTransientMode();

// Tests only: back to default (ResetBackupsForTesting's convention).
void ResetTransientModeForTesting();

} // namespace ned::editor

#endif // NED_EDITOR_TRANSIENTSESSION_H
