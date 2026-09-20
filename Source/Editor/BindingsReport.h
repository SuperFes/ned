//
// describe-bindings: the "*bindings*" report buffer -- every key sequence
// that is actually reachable in the live KeymapStack, grouped by the layer
// that owns it, each row carrying the command's own docstring.
//
// CaseViolationsBuffer.cpp's shape (a read-only, wholesale-rewritten results
// buffer) reused, minus the diagnostics: a binding row points at a command,
// not at a source location, so nothing here feeds next-error.
//
// Three things it shows that Emacs' own describe-bindings does not: the
// docstring beside each chord (ned pays for one per command already -- see
// Docs/Commands.md), the commands with no reachable binding at all, and the
// bindings that exist but can never fire because something shadows them
// (KeymapStack::ShadowedBindings -- a real class of bug in a hand-written
// init.janet, and otherwise invisible).
//

#ifndef NED_EDITOR_BINDINGSREPORT_H
#define NED_EDITOR_BINDINGSREPORT_H

#include <string>

#include "Command.h"
#include "Keymap.h"

namespace ned::text {
class Buffer;
class BufferList;
} // namespace ned::text

namespace ned::editor {

[[nodiscard]] std::string BindingsBufferName();

// Pure: the whole report as text. `majorModeName` names the buffer the
// report was taken from in the header -- the "Major mode" layer's bindings
// are whichever mode was active, and a report read later should still say
// which one that was.
[[nodiscard]] std::string RenderBindingsReport(const KeymapStack& keymaps, const CommandRegistry& registry,
                                               const std::string& majorModeName);

// Finds-or-creates the read-only "*bindings*" buffer and rewrites it from
// the report above, point at offset 0.
text::Buffer& RebuildBindingsBuffer(text::BufferList& bufferList, const KeymapStack& keymaps, const CommandRegistry& registry,
                                    const std::string& majorModeName);

} // namespace ned::editor

#endif // NED_EDITOR_BINDINGSREPORT_H
