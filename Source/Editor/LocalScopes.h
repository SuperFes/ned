//
// scope-aware-rename follow-up: resolving a name at point to the binding it
// actually refers to, and to every other occurrence of that same binding,
// using nothing but a language's own locals.scm captures (Mode::localScopes,
// Editor/Mode.h).
//
// This is the piece that makes "rename this parameter" correct without a
// language server: a textual scan renames every same-spelled token in the
// file, which is wrong the moment a name is shadowed or reused; a binding
// resolved through enclosing scopes renames exactly the one the cursor is
// on. Deliberately locals only -- a name whose binding is not in this file
// is not this file's business, and ResolveBindingAt says so (see
// LocalBinding::scopeIsFile) rather than guessing at a project-wide answer
// a real language server is far better placed to give.
//
// Pure and buffer-free, the same shape Text/ThreeWayMerge.h and
// Editor/Snippet.h's parse half take: it works over a flat capture list plus
// the text those captures index into, so it is unit-testable with no Parser,
// no Buffer and no Screen. Nesting is recovered from byte containment alone
// -- see LocalCapture's own doc comment for why the Mode capability hands
// back a flat list rather than a tree.
//
// The one scoping rule worth knowing, because it is the difference between
// a safe result and a corrupted file: a use resolves to the innermost
// enclosing scope that owns a same-named definition STARTING AT OR BEFORE
// that use. Languages with declaration-point scoping (C, C++, Rust, Go,
// Java, C#, Kotlin) are then exactly right. Languages with whole-scope
// binding (Python's function scope, JavaScript's `var`/function hoisting)
// can bind a use that textually precedes its own definition, which this
// rule instead resolves outward -- so it MISSES such an occurrence rather
// than renaming a different variable, and the miss is reported rather than
// silent: LocalBinding::usedBeforeDefinition is set whenever a same-named
// use inside the binding's own scope resolved to an ENCLOSING binding
// because of this rule, which is the caller's cue to refuse the local
// rename and defer to a language server.
//
// The position test only ever disambiguates BETWEEN bindings, so it is
// skipped entirely for a name nothing else in the file binds -- a use that
// precedes the only definition of its name can only mean that definition.
// Without that, a Python comprehension (`[n * n for n in xs]`, which reads
// n twice before binding it) would be unrenameable for no good reason.
//

#ifndef NED_EDITOR_LOCALSCOPES_H
#define NED_EDITOR_LOCALSCOPES_H

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Mode.h"

namespace ned::editor::locals {

// A byte range [first, second) into the buffer text the captures came from.
using Range = std::pair<std::size_t, std::size_t>;

// One resolved binding: which name, where it is bound, what scope owns it,
// and every place that scope's binding is written or read.
struct LocalBinding {
    // The identifier text itself, exactly as it appears in the buffer.
    std::string name;
    // The definition capture's own dotted qualifier ("parameter", "var",
    // ...) -- see LocalCapture::qualifier. Taken from the canonical
    // definition below; empty when the query captured a bare
    // "@local.definition".
    std::string qualifier;
    // The binding site. A scope can hold several same-named definition
    // captures for one binding (Python's `x = 1` ... `x = 2`, both
    // assignments to the same local), in which case this is the FIRST of
    // them and the rest appear in occurrences like any other use.
    Range definition;
    // The scope that owns the binding -- nullopt when nothing encloses it,
    // i.e. the name is bound at file level. A file-level binding can be
    // referenced from other files, so it is exactly the case a caller must
    // NOT rename on its own; scopeIsFile below is the same fact as a bool
    // for a caller that doesn't need the range.
    std::optional<Range> scope;
    bool                 scopeIsFile = false;
    // Every occurrence of this binding, sorted by start offset and
    // non-overlapping -- the definition site(s) and every reference that
    // resolves to them. This is the complete edit list for a rename; there
    // is nothing else in the file to change.
    std::vector<Range> occurrences;
    // Set when a same-named use inside this binding's own scope resolved
    // outward to an enclosing binding purely because it textually precedes
    // this definition -- see this header's own doc comment. The occurrence
    // list is then possibly incomplete for a whole-scope-binding language,
    // and a caller renaming anyway would leave that use behind.
    bool usedBeforeDefinition = false;
};

// Resolves the name at `point` against `captures` (one language's locals.scm
// output for `bufferText`, in any order), or nullopt when there is nothing
// to resolve: no definition or reference capture covers point, the covered
// token isn't a plain identifier, or no enclosing scope -- and not file
// level either -- binds that name anywhere. `point` may sit at either edge
// of a token, matching how point behaves after moving to a word's end.
[[nodiscard]] std::optional<LocalBinding> ResolveBindingAt(std::span<const LocalCapture> captures, std::string_view bufferText,
                                                           std::size_t point);

} // namespace ned::editor::locals

#endif // NED_EDITOR_LOCALSCOPES_H
