# Language Intelligence

Language support in ned comes from two independent sources that work together but serve
different purposes: **tree-sitter parsing**, compiled in for every bundled language and
requiring no setup at all, and **Language Server Protocol (LSP)** integration, which
needs a real language server installed and pointed at from your configuration. Roughly:
tree-sitter gives you syntax awareness of the file you have open right now; LSP gives you
project-wide, semantically-accurate intelligence, at the cost of running (and
configuring) an external server process.

## What comes for free: tree-sitter

Every bundled language — see [Language Support](../language-support.md) for the current
list — gets syntax highlighting, code folding, smart indentation, matching-bracket
navigation, and (for most languages) a symbol gutter and sticky-scroll breadcrumbs, all
from parsing the file itself. None of this needs any configuration or external process:

- `goto-matching-bracket` jumps between a bracket and its partner, aware enough to
  ignore a brace sitting inside a string or comment.
- `indent-region` reindents the current selection using the language's real structural
  rules, not just "copy the previous line's indent."
- Folding and the symbol/breadcrumb gutters are driven by the same parse, so they stay
  live as you type rather than needing a manual refresh.

## What needs a server: LSP

Nothing here is bundled — ned never installs or auto-detects a language server, only
runs one you've told it about:

```janet
(ned/set-lsp-command "cpp" ["clangd"])
(ned/set-lsp-command "python" ["pylsp"])
(ned/set-lsp-command "typescript" ["typescript-language-server" "--stdio"])
```

See [Language Support](../language-support.md) for a recommended server per bundled
language. Once configured, a server is spawned lazily the first time you open a matching
file, and reused across every buffer of that language (and, for a monorepo, per
sub-project root it detects independently).

### Navigation

| Command | Effect |
|---|---|
| `lsp-goto-definition` | Jump to where the symbol at point is defined |
| `lsp-goto-declaration` | Jump to its declaration (distinct from definition in C/C++, etc.) |
| `lsp-goto-implementation` | Jump to an interface/abstract member's concrete implementation |
| `lsp-goto-type-definition` | Jump to the *type* of the symbol at point |
| `lsp-peek-definition` | Preview a definition inline, without leaving the buffer |
| `lsp-goto-symbol` | Jump to a symbol within the current file |
| `lsp-workspace-symbol` | Search for a symbol across the whole project |
| `lsp-call-hierarchy-incoming` / `-outgoing` | Who calls this, or what this calls |
| `lsp-type-hierarchy-subtypes` / `-supertypes` | Walk a type's inheritance in either direction |

### Editing

| Command | Effect |
|---|---|
| `lsp-complete` | Request completion candidates at point |
| `lsp-hover` | Show hover documentation for the symbol at point |
| `lsp-signature-help` | Show the current function call's parameter info |
| `lsp-code-action` | List available quick fixes/refactors at point |
| `lsp-quick-fix` | Apply the code action at point immediately, no menu |
| `lsp-document-highlight` | Highlight every occurrence of the symbol at point, in this file |
| `lsp-linked-editing-range` | Live-mirror edits across linked ranges (e.g. a markup tag's opening/closing name) |
| `format-buffer` | Reformat the whole buffer — see [Formatting](formatting.md) |
| `lsp-run-code-lens-at-point` | Run (resolving first if needed) the code lens at point |

Completion appears as an anchored popup as you type, ranked by fuzzy match against the
server's own ordering, with a documentation preview alongside it — accept with Tab/Enter,
or a server-declared commit character (like `.` or `(`) if one applies.

### Renaming

`rename-symbol` is the one to reach for by default: it's **scope-aware**, meaning it
first checks whether the name at point is a local binding this file fully owns (a loop
variable, a local function parameter). If so, it renames every occurrence in this buffer
alone, instantly, with no server round-trip at all — a loop variable shouldn't cost a
network request. If the name isn't a purely local binding (or no such analysis is
available for the language), it falls through to a full LSP-backed rename across every
file the server reports, exactly what `lsp-rename` does directly.

A cross-file rename doesn't rewrite files blindly — it opens a review buffer, the same
kind [project-replace](../key-concepts/search-and-replace.md#project-wide-search-and-replace)
uses, with one addition: renaming starts with the identified references already applied
in the review, plus a scan for comment/string occurrences of the old name that the
server's rename never touches — matching JetBrains' "search in comments and strings."
`M-a` toggles one of those extra occurrences into the batch (the inverse of `M-r`,
reverting one); the rest of the review vocabulary (`M-n`/`M-p` to step excerpts,
`C-c C-c` to commit) is identical to a project replace.

### Diagnostics

`lsp-diagnostics-buffer` stitches every open buffer's diagnostics (errors, warnings,
hints) into one navigable list. Diagnostics also show inline in the gutter and, for the
line at point, as inline text — no separate "problems panel" to open first.

## Next steps

- [Debugging](debugging.md) — the DAP client, for actually running and stepping through
  code rather than just analyzing it statically.
- [Language Support](../language-support.md) — recommended LSP servers per language.
