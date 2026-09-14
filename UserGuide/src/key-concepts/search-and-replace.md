# Search and Replace

ned has three tiers of search-and-replace, scoped from a single buffer up to a whole
project, and the two replace operations both go through the same kind of reviewable
buffer rather than a blind, all-at-once rewrite.

## Incremental search

`C-s` / `C-r` start an incremental search forward/backward: as you type, point jumps to
the next match live, no need to press Enter first. Repeating `C-s`/`C-r` while a search
is active moves to the next/previous match; `C-g` cancels and returns point to where the
search started.

## Query-replace

`M-%` (`query-replace-regexp`) prompts for a pattern, then a replacement, then walks
each match asking you to confirm, skip, or replace-all-remaining, one at a time — real
Emacs `query-replace` behavior, with a real regex engine underneath (PCRE2 — full
lookaround and backreferences, not just literal/glob matching).

## Project-wide search and replace

- **`project-search`** searches every file under the current directory recursively for a
  regex pattern, opening the matches in a results buffer. Results honor `.gitignore` the
  same way `git` itself would, and an open buffer with unsaved changes is searched
  *live* — you see your actual pending edits, not the stale on-disk version.
- **`search-in-results`** narrows a search to just the files a results buffer or review
  buffer already references — useful for refining a broad first pass.
- **`project-replace`** is the project-scope counterpart to `query-replace-regexp`, but
  it works differently on purpose: instead of confirming match-by-match across
  potentially hundreds of files, it builds one **review buffer** — an editable
  multibuffer with one excerpt per match, each already rewritten with the replacement
  applied as a preview. You look at the whole batch at once, and:
  - Editing an excerpt back to its original text is how you exclude that one match —
    there's no separate "skip" gesture, just edit or don't.
  - `multibuffer-revert-excerpt` / `multibuffer-revert-file` do the same thing without
    hand-editing: restore one excerpt, or every excerpt from one file, back to its
    original text.
  - `multibuffer-commit-changes` (`C-c C-c`) applies everything still showing the
    replacement into the real source buffers (opening any that aren't already open) as
    one undoable step — nothing touches disk yet, your own save does that.
  - `multibuffer-commit-to-disk` writes straight to the files instead, skipping the
    buffer step entirely (a file with unsaved edits open in a buffer is still applied
    into that buffer rather than overwritten behind it).
  - `multibuffer-commit-file` / `multibuffer-commit-file-to-disk` do either of the above
    for just the one file under point, leaving the rest of the batch pending.

This same review-buffer mechanism is reused for other batch operations that produce
several file-spanning edits — a language server's rename, an import-path fixup after
moving a file — so learning the excerpt/revert/commit vocabulary here pays off in more
than one feature. See [Language Intelligence](../features/language-intelligence.md) for
where rename's own review differs slightly (an extra "include" key, since a rename
starts everything excluded rather than everything included).

## Next steps

- [Windows, Buffers, and Sessions](windows-and-buffers.md)
- [Language Intelligence](../features/language-intelligence.md) — for rename-symbol's
  own review-buffer flow.
