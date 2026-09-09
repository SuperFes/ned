//
// The collaborators every part of BufferView works against, in one place --
// see Docs/BufferViewDecomposition.md.
//
// BufferView is being taken apart into a set of focused classes (a gutter
// model, a viewport, a renderer, a prompt controller, one broker per external
// tool). Nearly every one of them needs the same handful of things: the buffer
// currently shown, the buffer list, the shared status line, the active mode and
// theme, and whichever managers happen to be wired up. Passing those
// individually would give each extracted class a fifteen-parameter constructor
// and make adding a collaborator a change to all of them.
//
// Two kinds of member, for two different lifetimes:
//
//   - The nine collaborators BufferView is constructed with are plain
//     references. They are fixed for the life of the view.
//   - The managers arrive later, through BufferView's Set* hooks, and any of
//     them may stay null forever (most test-constructed views wire none). They
//     are held as references *to the pointers*, not copies of them, so a
//     SetLspManager long after this struct was built is visible through it with
//     no second update path to keep in step.
//
// That second kind is why a BufferView must not be moved: the struct binds to
// its own sibling members, so a moved-from view's context would point into the
// wrong object. BufferView is already non-copyable and declares no move, and
// there is a static_assert on that in BufferView.h to keep it that way.
//
// Deliberately an aggregate with no behaviour. It is the set of things a part
// may reach for, not an interface to them -- what each part does with them is
// the part's own business.
//

#ifndef NED_UI_BUFFERVIEW_EDITORCONTEXT_H
#define NED_UI_BUFFERVIEW_EDITORCONTEXT_H

#include <string>

namespace ned::text {
class BufferList;
class KillRing;
} // namespace ned::text

namespace ned::editor {
class Dispatcher;
class ProjectUndoManager;
class PromptHistory;
class RegisterTable;
struct Mode;
namespace acp {
    class Manager;
}
namespace dap {
    class Manager;
}
namespace lsp {
    class Manager;
}
namespace tasks {
    class TaskRunner;
}
namespace testrun {
    class TestRunner;
}
namespace vcs {
    class Runner;
}
} // namespace ned::editor

namespace ned::janet {
class Environment;
}

namespace ned::ui {
class ActiveBuffer;
class EventLoop;
struct Theme;
} // namespace ned::ui

namespace ned::ui::bufferview {

struct EditorContext {
    // Fixed for the life of the view.
    ActiveBuffer&          activeBuffer;
    text::KillRing&        killRing;
    editor::RegisterTable& registers;
    editor::PromptHistory& promptHistory;
    text::BufferList&      bufferList;
    editor::Dispatcher&    dispatcher;
    std::string&           statusMessage;
    const editor::Mode&    mode;
    const Theme&           theme;

    // Wired later, and any of them may be null -- check before use.
    editor::lsp::Manager*&     lspManager;
    editor::dap::Manager*&     dapManager;
    editor::acp::Manager*&     acpManager;
    editor::vcs::Runner*&      vcsRunner;
    editor::tasks::TaskRunner*&   taskRunner;
    editor::testrun::TestRunner*& testRunner;
    editor::ProjectUndoManager*&  projectUndo;
    EventLoop*&                   eventLoop;
    const janet::Environment*&    janetEnv;
};

} // namespace ned::ui::bufferview

#endif // NED_UI_BUFFERVIEW_EDITORCONTEXT_H
