//
// internal-project-search follow-up: the worker-thread cap for
// ProjectSearch.cpp's internal, multi-threaded file-scanning engine (the
// replacement for the old `rg` shell-out). One process-wide setting, same
// "mutex-guarded static + ned/set-* binding" shape as TabWidth.h. Deliberately
// small by default -- scanning a project is I/O-bound (waiting on the
// filesystem/page cache), not CPU-bound, so more threads than there are
// real disks/cores to service them mostly just adds contention rather than
// throughput.
//

#ifndef NED_EDITOR_SEARCHSETTINGS_H
#define NED_EDITOR_SEARCHSETTINGS_H

namespace ned::editor {

void              SetProjectSearchThreads(int threads);
[[nodiscard]] int ProjectSearchThreads();

} // namespace ned::editor

#endif // NED_EDITOR_SEARCHSETTINGS_H
