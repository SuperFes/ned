//
// vim-surround-style delimiter editing (ds/cs/ys/visual-S) as pure functions over a
// Buffer, VimTextObject.h's sibling: DeleteSurroundAtPoint/ChangeSurroundAtPoint reuse
// InnerQuote/InnerBracket/InnerTag's own "around" ranges directly (the delimiter bytes
// are exactly [around.start, inner.start) and [inner.end, around.end)), and AddSurround
// is the shared insertion primitive behind ys/yss/visual-S alike.
//
// v1 cuts, documented here once: `from`/`to` accept the same alias set VimTextObject.h's
// bracket objects already use ('(' / ')' / 'b', '[' / ']', '{' / '}' / 'B', '<' / '>',
// plus the three quote chars and 't' for tag) -- not real vim-surround's fuller alias
// table (r/a for []/<>) or its interactive tag-name-entry prompt when '<'/'t' is typed as
// a *target*: '<'/'>' resolve to literal angle brackets here, matching VimTextObject's
// own i</a< convention, and 't' is only ever a valid `from` (an existing tag can be
// stripped or changed away from), never a `to` (there's no text-entry sub-session to read
// a new tag name/attributes). ys itself is wired in VimEngine.cpp only for text-object
// ranges (ysiw), yss) and the doubled current-line form (yss)) -- ys against an arbitrary
// motion (ysw), ys$), ...) and the uppercase "own line" variants (yS/ySS) are both
// unimplemented; VimEngine's own header comment carries the authoritative cut list.
//

#ifndef NED_EDITOR_VIM_VIMSURROUND_H
#define NED_EDITOR_VIM_VIMSURROUND_H

#include <cstddef>
#include <optional>
#include <string>

#include "Text/Buffer.h"

namespace ned::editor::vim {

struct SurroundDelim {
    std::string open;
    std::string close;
};

// Resolves a `to` target character to the literal delimiter text ys/cs insert. An
// opening-bracket character adds its own inner padding space (real vim-surround's own
// convention: cs"( -> "( x )"); the matching closing character or alias letter (b/B)
// never does. Quote characters never pad.
[[nodiscard]] std::optional<SurroundDelim> ResolveSurroundTarget(char32_t target);

// ds{from} -- deletes the delimiter pair identified by `from` enclosing point, if any.
// One undo step; false (buffer untouched) if no such pair encloses point.
bool DeleteSurroundAtPoint(text::Buffer& buffer, char32_t from);

// cs{from}{to} -- replaces an existing delimiter pair with a new one. One undo step;
// false (buffer untouched) if no `from` pair encloses point or `to` doesn't resolve.
bool ChangeSurroundAtPoint(text::Buffer& buffer, char32_t from, char32_t to);

// ys/yss/visual-S's shared primitive: wraps [start, end) with `to`'s delimiters. One
// undo step; `pointOut` receives where point should land afterward (the wrapped range's
// own start byte, real vim's own convention). False (buffer untouched) if `to` doesn't
// resolve.
bool AddSurround(text::Buffer& buffer, std::size_t start, std::size_t end, char32_t to, std::size_t& pointOut);

} // namespace ned::editor::vim

#endif // NED_EDITOR_VIM_VIMSURROUND_H
