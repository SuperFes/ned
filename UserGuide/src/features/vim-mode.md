# Vim Mode

ned includes a full Vim modal-editing emulation, off by default:

```janet
(ned/set-vim-mode true)
```

This is a genuine reimplementation of Vim's grammar — Normal, Insert, Visual, Replace,
and command-line modes, motions, text objects, and operators — not a thin remapping of
ned's own Emacs-style keymap.

## What's included

- **Modes**: Normal, Insert, Visual (character/line/block), Replace, and `:`
  command-line mode.
- **Motions and text objects**: the standard Vim vocabulary —
  `w`/`b`/`e`, `f`/`t`/`F`/`T`, paragraph/sentence motions, `iw`/`aw`, `i"`/`a"`,
  `i(`/`a(`, `it`/`at` (tag content), and more — composable with operators the usual way
  (`diw`, `ca"`, `2dj`, ...).
- **vim-surround**: `ds`/`cs`/`ys`, and the visual-mode `S`, for adding/changing/deleting
  a surrounding pair.
- **Ex commands**: `:w`, `:42` (jump to a 1-based line — ned's own line numbers are
  0-based internally, but `:42` means what it means in Vim), `:%s/foo/bar/g`,
  `:'<,'>d`, and others.
- **Registers**: Vim's own unnamed/named/append/numbered-delete-ring/blackhole/clipboard
  routing — a genuinely different shape from
  [Emacs-style registers](../key-concepts/editing-essentials.md#registers), not a reuse
  of the same mechanism, since the two systems don't behave alike.
- **Marks**: buffer-local `a`-`z` and `'<`/`'>`, plus cross-file `A`-`Z` global marks.
- **Jump list**: `C-o`/`C-i` to walk backward/forward through recent jump locations.
- **'magic' regex translation**: Vim's default `'magic'` escaping convention
  (`( ) | + ? = { }` need escaping to be literal, the reverse of most regex engines) is
  translated transparently, so a `:s///` pattern behaves the way it would in real Vim.

## Insert mode still runs on ned's own keymap

One deliberate design choice: while Vim's Normal/Visual/Replace/command-line dispatch is
entirely Vim's own grammar, Insert mode is still ned's ordinary Emacs-bound keymap
underneath. This means auto-pairing brackets/quotes, snippet expansion, and LSP
completion all keep working exactly as documented elsewhere in this guide while you're
typing in Insert mode — Vim mode changes how you *navigate and operate*, not the typing
experience itself.

## Next steps

- [Editing Essentials](../key-concepts/editing-essentials.md) — for the Emacs-style
  vocabulary Vim mode's Insert mode still rides on.
- [Configuration](../configuration.md)
