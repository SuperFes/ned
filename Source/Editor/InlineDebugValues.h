//
// debug-panel: one process-wide on/off switch for the debugger's inline
// values -- the `name = value` annotation BufferView draws after a line that
// mentions a local, while the debuggee is stopped in that file --
// and the rule that decides which locals a given line actually mentions.
// InlineDiagnostics.h's exact pattern for the switch (mutex-guarded static,
// default on, set from Janet via ned/set-inline-debug-values and toggled
// live via toggle-inline-debug-values).
//
// The switch is purely a display switch: the values themselves
// (Dap::Manager::FrameLocals, the debug panel's Variables section, the
// *debug* buffer) are unaffected either way. It exists because the
// annotation is genuinely intrusive on a dense line, and because a frame
// with many locals annotates nearly every line of it.
//
// EndOfLine is the only style: an inline value never costs a screen row, for
// the same reason InlineDiagnosticStyle::EndOfLine is the diagnostics
// default -- a value appearing or changing while stepping must not shove
// the code below it up and down.
//
// ResolveInlineDebugValues below is pure and buffer-free, the same shape
// Editor/LocalScopes.h itself takes: it works over the buffer text, a flat
// locals capture list and a name->value map, so the rule is unit-testable
// with no Screen, no Buffer and no live debug adapter. BufferView owns only
// the painting and the cache.
//

#ifndef NED_EDITOR_INLINEDEBUGVALUES_H
#define NED_EDITOR_INLINEDEBUGVALUES_H

#include <cstddef>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Mode.h"

namespace ned::editor {

void               SetInlineDebugValuesEnabled(bool enabled);
[[nodiscard]] bool InlineDebugValuesEnabled();

// Which rule decides that a line mentions a local.
//
// Scoped is the real one and needs the language's own locals query
// (Mode::localScopes): a candidate is an actual definition/reference
// capture, resolved through locals::ResolveBindingAt to the binding it
// refers to, and kept only when that binding is the one the stopped frame
// can see. That is what keeps the value off a name in a comment or a
// string, off the `count` in `foo.count`, off a same-spelled variable in
// another scope, and -- the case a textual match gets wrong on every
// screen -- off the lines of every OTHER function in the stopped file.
//
// Textual is the fallback for a mode with no locals query at all (the
// standing "empty function means not configured" convention): a whole-word
// text match, which is what this feature shipped with. Recall over
// precision, deliberately -- for a language ned cannot resolve bindings in,
// a sometimes-wrong annotation is still better than none, and the two tiers
// are chosen per buffer rather than globally so no language loses what it
// already had.
enum class InlineDebugValueTier {
    Scoped,
    Textual,
};

// One buffer line to consider, as the byte range of its own text.
//
// The caller supplies the ranges rather than the resolver deriving them,
// because the caller (a painter walking screen rows) already has them and
// deriving them here would mean a line-offset scan of the whole buffer to
// answer a question about forty lines of it. `line` is opaque: it is
// echoed back on every annotation and nothing here interprets it.
struct InlineDebugValueLine {
    std::size_t line;
    std::size_t startByte;
    std::size_t endByte;
};

// One annotation to draw: `value` is the adapter's own string, untruncated
// and unformatted. Assembling the displayed text is the caller's business,
// since how much of a value fits is a question about columns.
struct InlineDebugValue {
    std::size_t line;
    std::string name;
    std::string value;
};

// Every annotation for `lines`, in the order `lines` were given and by name
// within a line, capped at `maxPerLine` per line (a dense line mentioning
// six locals would otherwise annotate itself into unreadability).
//
// `captures` is the locals query's output for `text` and is ignored under
// InlineDebugValueTier::Textual. `stopByte` is where the debuggee is
// stopped -- the offset whose enclosing scopes decide which binding of a
// shadowed name the frame's values actually belong to; under Textual it is
// ignored too, which is precisely that tier's blind spot.
[[nodiscard]] std::vector<InlineDebugValue>
ResolveInlineDebugValues(std::string_view text, std::span<const LocalCapture> captures, InlineDebugValueTier tier,
                         const std::map<std::string, std::string>& frameLocals, std::size_t stopByte,
                         std::span<const InlineDebugValueLine> lines, std::size_t maxPerLine = 3);

} // namespace ned::editor

#endif // NED_EDITOR_INLINEDEBUGVALUES_H
