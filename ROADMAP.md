# Ned Roadmap

An Emacs-class terminal editor in modern C++23, with Janet filling Elisp's role: every
capability is a named command reachable from keybindings, `M-x`, and Janet alike. It
renders directly on Notcurses and parses every language with its own engine.

✅ shipped · 🔜 planned · 💭 exploring

Working lists: [`TODO.md`](TODO.md) holds committed work and known issues, and
[`MAYBE.md`](MAYBE.md) holds ideas that aren't committed yet plus what we've decided
against.

---

## Editing

- ✅ **Emacs editing vocabulary**: kill ring, registers, rectangles, narrowing, isearch,
  query-replace, and fill-paragraph.
- ✅ **Undo tree**, persisted across sessions and restored when the file on disk
  matches.
- ✅ **Multiple cursors**, including adding cursors and removing the last one added.
- ✅ **Structural editing**: sexp motion, expand/shrink selection, auto-paired brackets
  and quotes.
- ✅ **Snippets**: TextMate/LSP syntax with nested tabstops, variables, choices, and
  macro replay.
- ✅ **Huge files**: memory-mapped, windowed highlighting, streaming search and save,
  and crash-recovery backups.
- ✅ **Safe file handling**: async large-file save, auto-revert and three-way auto-merge
  on external change, and charset detection that preserves bytes rather than guessing.
- ✅ **Bracketed paste and drag-and-drop.**
- 🔜 Variables inside snippet placeholder defaults.
- 💭 Multi-cursor paste distribution, and a mixed tabs-and-spaces indent style.

## Keymaps

- ✅ **Emacs-style default keymap**, fully rebindable from Janet, with
  `describe-bindings`.
- ✅ **Vim emulation** (opt-in): surround, marks, jumplist, magic regex, window
  commands, and `:` commands with completion.
- ✅ **Modern keymap** in the VS Code/JetBrains style.
- 💭 `Keymap::Bind` refusing bindings that can never be typed.

## Search & Navigation

- ✅ **Project search and replace**, reading live buffers before disk, with PCRE2
  regex.
- ✅ **Search everywhere**: one palette (double-tap Shift) over commands, files,
  buffers, symbols, and text, with a preview.
- ✅ **Multibuffers**: excerpts from many files, editable in place.
- ✅ **Bookmarks, recent files, and save-place.**
- 💭 Excerpt-scoped search for `next-error`, dabbrev, and Vim `/`.

## Interface

- ✅ **From-scratch Notcurses UI**: recursive window splits, a tab bar, a project
  sidebar, and bottom/left panel docks with a rail.
- ✅ **Translucent themes**: bundled and clonable themes, a theme authoring loop, and
  per-capture styling.
- ✅ **Pixel minimap**, sticky-scroll breadcrumbs, and wrap-continuation markers.
- ✅ **Gutter columns**: VCS diff, blame, symbol kind, breakpoints, quick-fix hints,
  tests, and unseen log output.
- ✅ **Colour swatches and a colour picker.**
- ✅ **Reusable widgets**: `TableView` (sortable record tables) and `TreeView`.
- ✅ **Mouse support** layered over keyboard commands, never replacing them.
- 🔜 A setting for terminal application titles, and a resize border for a right-docked
  agent panel.
- 💭 Styled spans in list and table rows, remembered table sort order, configurable
  indicator glyphs, and a settings UI.

## Languages

- ✅ **120 bundled languages and file formats**, each shipped as a package: a grammar,
  queries, and an optional scanner. All of them run on ned's own parser generator and
  engine, with no tree-sitter anywhere.
- ✅ **`ned --import-language`**, which brings in any tree-sitter grammar repository as a
  package.
- ✅ **Highlighting with language injections**, incremental reparsing, and
  changed-range query caching.
- ✅ **Folding, smart indentation, locals, outlines, and imports**, per language, with
  no language server required. See `Docs/LanguageMatrix.md`.
- ✅ **Mode detection** from filenames, libmagic, and modelines.
- 💭 Language-specific misses (listed in `MAYBE.md`), Godot's TCP language server, and
  Carbon once it reaches 0.1.

## Formatting

- ✅ **A native formatter**: per-language rule engines with style defaults taken from
  each language's official guide.
- ✅ **`format.janet`/`style.janet`**, for per-project and personal overrides.
- ✅ **Naming-convention checker** with a fixer that reuses rename.
- ✅ **Scoped format-on-save**, an LSP formatting tier, and whitespace hygiene.
- ✅ **`ned-format`**, a command-line formatter that also reindents huge files by
  streaming.
- 💭 Full parity with gofmt, black, rustfmt, and Prettier edge cases.

## Refactoring

- ✅ **Scope-aware rename** without a language server, plus a review multibuffer that
  also offers matches in comments and strings.
- ✅ **Change signature** across 68 languages.
- ✅ **File rename/move propagation**, which fixes imports in both directions.
- ✅ **Class/file name sync.**
- ✅ **Multi-file LSP edits with project-wide undo.**
- 💭 Import fixup off the main thread, and project-wide detection of files moved
  outside ned.

## Language Servers (LSP)

- ✅ **Full client**: diagnostics, completion (with resolve and merged sources), hover,
  signature help, highlights, inlay hints, semantic tokens (range and delta), code
  lens, document links, and colours.
- ✅ **Navigation**: definition, declaration, type definition, implementation,
  references, and call/type hierarchies.
- ✅ **Code actions** with a quick-fix gutter marker, and formatting.
- ✅ **Workspace edits**: resource operations, server-pushed edits, and file-rename
  notifications.
- ✅ **Results anchored to the buffer**, so positions survive edits made while a request
  is in flight.
- ✅ **Multi-root workspaces**, server-initiated refresh, and a project-wide problem list.
- ✅ **LSP broker daemon**, which shares warm servers across ned processes (and can run
  under systemd).
- ✅ **Prose checking** through harper-ls.
- 🔜 Percent-encoded outgoing URIs.
- 💭 Answers to server questions (`showMessageRequest`), broker pre-warming, and
  servers for Markdown/Org code blocks.

## Debugging (DAP)

- ✅ **Breakpoints**: line, conditional, hit-count, function, exception, and data.
- ✅ **Stepping**: run to cursor, jump to line, restart frame, reverse execution, and
  attach.
- ✅ **Debug panel**: scopes, watches, threads, and a console with completion.
- ✅ **Inline variable values**, resolved through each language's scope rules.
- ✅ **Pointer-graph view**, memory hex view, and disassembly.
- ✅ **Sanitizer and Valgrind output parsing**, a Massif heap graph, and lcov
  coverage.
- 🔜 Live verification of the newer requests against lldb-dap and debugpy.
- 💭 A shared variables cache, and inline values for fields and watches.

## Version Control

- ✅ **Provider-agnostic VCS layer**, with git bundled as a Janet plugin.
- ✅ **Diff gutter with per-hunk stage, unstage, and revert**, plus status and side
  panels.
- ✅ **Commit, history, blame, and branches**; ned works as `$EDITOR` for git, hg, svn,
  jj, and fossil (transient mode).
- ✅ **Merge-conflict resolution mode** and a side-by-side merge view.
- 🔜 Showing git's clean auto-merges in the merge view (from index stages).
- 💭 A base pane in the merge view.

## Tasks, Tests & Terminal

- ✅ **Task runner and test runner**: run-at-point, results in the gutter, and output
  parsers.
- ✅ **Embedded terminal** (libvterm): scrollback search and selection, mouse
  forwarding, and OSC 52.
- ✅ **REPLs**: language REPLs plus the built-in Janet REPL.

## AI Agents

- ✅ **Agent Client Protocol panel**: multiple sessions, forking, resume, per-turn
  review and rewind, inline images, logins, questions, and notifications.
- ✅ **MCP bridge**, giving agents ned's diagnostics, definitions, search, rename, code
  actions, and navigation.
- ✅ **Tested with Claude, opencode, and Antigravity**, with nothing agent-specific in
  ned.
- 💭 Subagent sessions, client-run terminals (`terminal/*`), review comments fed back
  to the agent, and next-edit prediction.

## Org & Markdown

- ✅ **Org mode**: outline, TODO states, checkboxes, scheduling with recurrence,
  clocking, properties, capture templates, tables, links, and a project-wide agenda.
- ✅ **Markdown**: GFM table editing at parity with Org.

## Projects & Integrations

- ✅ **Named projects** with a switcher, per-project sessions, and trust-gated
  project-local settings.
- ✅ **Issue trackers**: Jira and GitHub panels, issue buffers, transitions,
  assignment, comments, worklogs, issue keys in branches and commits, and key
  completion.
- 💭 Board panels (GitHub Projects, Jira agile).

## Scripting

- ✅ **Janet throughout**: commands, keymaps, modes, settings, VCS and tracker providers,
  and language packages.
- 🔜 **A Janet widget API**: declarative panels where Janet supplies the model and C++
  paints it.

---

## Horizon

- 🔜 **Remote development over SSH**: edit files on a remote host locally, with an
  optional thin agent for search and toolchain-native language servers.
- 🔜 **A client/server protocol** for the remote agent and an `emacsclient`-style
  attach mode.
- 🔜 **Remote debugging**, e.g. attaching to live PHP.
- 🔜 **Documentation**: Doxygen developer docs, a `ned-setup` first-run generator, and
  debugger cookbook entries.
- 🔜 **1.0**: a frozen Janet API, no known data-loss paths, release artifacts, and a
  published docs site.
- 💭 **Jupyter notebooks**, a **native Windows port**, **real-time collaborative
  editing**, and **Jank** as the scripting language.
