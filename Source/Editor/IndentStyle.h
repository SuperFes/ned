//
// smart-indentation follow-up. The character/width Indent.h's engine writes
// when it reindents a line -- spaces vs. tabs, and how many columns one
// indent level occupies. One process-wide default (mirroring TabWidth.h's
// exact mutex-guarded-static shape), with an optional per-mode override
// table (mirroring WrapOverrides.h's own override-table shape) since indent
// convention is a per-language-convention preference (PEP8 spaces, gofmt
// tabs, a Makefile's required literal tabs) rather than a per-document one
// -- keyed by Mode::name, not filename/extension like WrapOverrides.h:
// unlike word-wrap, a user essentially never wants two files in the same
// language indented differently, and Mode::name is already a stable
// identity every *Mode() factory sets, unlike an extension pattern that can
// be shared ambiguously across languages.
//
// Configured from Janet (ned/set-indent-style).
//

#ifndef NED_EDITOR_INDENTSTYLE_H
#define NED_EDITOR_INDENTSTYLE_H

#include <string>

#include "Text/IndentStyle.h"

namespace ned::text {
class Buffer;
}

namespace ned::editor {

using IndentStyle = text::IndentStyle;

// Process-wide default.
void                      SetIndentStyle(IndentStyle style);
[[nodiscard]] IndentStyle DefaultIndentStyle();

// Per-mode-name override (e.g. "python-mode" -- Mode::name verbatim).
void SetIndentStyleForMode(const std::string& modeName, IndentStyle style);

// modeName's own override if one is configured, else the process-wide
// default -- same "caller falls back gracefully" convention
// WrapLinesForFileOverride/EffectiveWrapLines already established.
[[nodiscard]] IndentStyle EffectiveIndentStyle(const std::string& modeName);

// The buffer's own style (Buffer::LocalIndentStyle -- what the file itself
// says) when it has one, else EffectiveIndentStyle(modeName).
[[nodiscard]] IndentStyle EffectiveIndentStyle(const text::Buffer& buffer, const std::string& modeName);

} // namespace ned::editor

#endif // NED_EDITOR_INDENTSTYLE_H
