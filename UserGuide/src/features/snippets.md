# Snippets

ned supports TextMate-style snippets — the same tabstop syntax VS Code and Sublime Text
use — both hand-registered and received live from a language server's completion
responses.

## How expansion works

Type a trigger word, then `TAB` (or run `expand-snippet` via `M-x` in a mode whose
keymap claims `TAB` for something else): the snippet's body is inserted as one undo
step, and if it has tabstops, ned enters a live tabstop session — `TAB`/`S-TAB` hop
between fields, a field with a placeholder starts pre-selected so typing immediately
replaces it, a repeated tabstop index mirrors what you type across every copy of it
live, and `$0` (or the end of the snippet, if none is given) is where point lands when
you tab past the last field. `ESC` ends the session early.

## Registering your own

```janet
(ned/register-snippet "cpp" "for"
  "for (int ${1:i} = 0; $1 < ${2:n}; ++$1) {\n    $0\n}")
```

- `${1:i}` is tabstop 1 with a default placeholder text of `i`.
- `$1` (bare, no placeholder) elsewhere in the body *mirrors* tabstop 1 — typing in one
  copy updates every copy live.
- `$0` marks where point ends up after the last real tabstop.
- `\$` inserts a literal `$`.

The language key matches the same string `ned/set-lsp-command` uses (`"cpp"`,
`"python"`, ...); an empty string (`""`) registers the snippet for every language.
Registering an existing trigger again overwrites it; registering an empty body clears
it. `ned/snippet-triggers` lists every trigger visible to a given language key, if you
want to check what's already registered before adding your own.

## Snippets from your language server

A completion candidate whose `insertText` is snippet-shaped (an LSP server declaring
`insertTextFormat: Snippet`) expands through this exact same mechanism when accepted —
tabstops, mirrors, and all — not inserted as flat text. See
[Language Intelligence](language-intelligence.md) for completion in general.

## Next steps

- [Language Intelligence](language-intelligence.md)
- [Configuration](../configuration.md)
