//
// Which comment syntax applies at a position. A buffer's own mode says what
// its language comments with, but a line inside an injected region (an HTML
// <script>, a Markdown code fence, a Vue <style>) is written in the injected
// language and has to be commented in that one instead.
//

#ifndef NED_EDITOR_COMMENTSYNTAX_H
#define NED_EDITOR_COMMENTSYNTAX_H

#include <cstddef>
#include <string>
#include <string_view>

#include "Mode.h"

namespace ned::editor {

// `prefix` alone is a line comment; with `suffix` it is a block comment each
// line is wrapped in. Both empty: the language at the position has no
// comment syntax. `language` names whose syntax this is -- the injected
// language's, or empty for the host mode's own.
struct CommentSyntax {
    std::string prefix;
    std::string suffix;
    std::string language;
};

// The comment syntax of the innermost injected region containing `position`
// that is written in a real language (one with a file type of its own --
// Markdown's paragraph-level markdown-inline layer is not), else the host
// mode's own. A line comment is preferred over a block comment.
[[nodiscard]] CommentSyntax CommentSyntaxAt(const Mode& host, std::string_view text, std::size_t position);

} // namespace ned::editor

#endif // NED_EDITOR_COMMENTSYNTAX_H
