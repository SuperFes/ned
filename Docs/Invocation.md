# Invocation

Every flag `ned` accepts. Generated from the same option table `ned --help` prints
from, so it cannot drift from the binary you have installed.

```sh
ned [options] [paths...]
```

Four of the startup modes are also installed as their own executables, so a build tool can
call one by name instead of remembering a flag. `ned-format` is `ned --format`, `ned-langc`
is `ned --compile-language`, `ned-import-language` is `ned --import-language`, and
`ned-test-language` is `ned --test-language`. Dispatch is on `argv[0]`'s own basename, and
an explicit startup-mode flag on the command line still wins.

For what ned returns to a caller -- `EDITOR=ned` and a version control system reading
the result -- see [Exit codes](getting-started.md#exit-codes).

## Startup modes

Each of these makes `ned` do one job and exit instead of starting the editor. They are
mutually exclusive -- passing two is a usage error, not a silent win for whichever came
first.

### `--lsp-broker`

Run the headless LSP broker daemon and exit

### `--lsp-broker-stop`

Stop a running LSP broker daemon and exit

### `--foreground`

Run the LSP broker daemon in the foreground, never self-exiting when idle -- for a systemd --user service (see Packaging/systemd/ned-server.service) or any other real process supervisor. An ordinary broker already holding the socket is shut down first and waited for; a second --foreground instance refuses to start, since restarting the service is what a supervisor is for

### `--mcp-stdio-relay TEXT`

Relay stdio to a running ned process's ACP MCP bridge socket, then exit (spawned by an ACP agent, not meant to be run by hand)

### `--format`

Format the given files headlessly and exit (External formatter, falling back to a native per-language reindent -- no LSP tier, no init.janet; format.janet only)

### `--compile-language`

Compile each given language directory's grammar.janet to its parse tables (written as `tables` beside it) and exit -- what the build runs for every bundled language, and the authoring loop for a hand-written grammar

### `--import-language`

Turn a tree-sitter grammar repository (a git URL or a checkout) into a ned language package: grammar.janet, upstream queries, corpus, staged scanner, and a language.janet skeleton -- then compile it and run its corpus

### `--test-language`

Run each given language package's corpus against its grammar and exit

## Options

### `-h, --help`

Print this help message and exit

### `--version`

Print ned's version and exit

### `--force-huge`

With --format, reindent a file over the huge-file threshold via the lexical streaming engine (Native reindent only -- no external formatter, no space/break/wrap/blank rules) instead of skipping it

Requires `--format`.

### `-o, --output TEXT`

With --compile-language (or as ned-langc) and a single language, write the tables to this file instead

### `--name TEXT`

With --import-language: the language name (default: the grammar's own)

### `--subdir TEXT`

With --import-language: the grammar's directory inside a multi-grammar repository

### `--ref TEXT`

With --import-language and a git URL: the tag, branch or full commit hash to clone

### `--into TEXT`

With --import-language: the languages root to write the package under (default: $XDG_CONFIG_HOME/ned/languages)

### `--bless`

With --test-language: rewrite each failing case's expected tree to what the grammar parses now

### `--force-binary`

Open files that look binary anyway, without an interactive confirmation

### `--no-restore`

Don't restore the project's saved session (open buffers, breakpoints, sidebar state)

### `--transient`

Store nothing about this run: no project session (neither restored nor saved), no save-place, no recent-files entry, no persistent undo, no backups. For running ned as another tool's $EDITOR -- it is applied automatically for a file a version control system names (git's COMMIT_EDITMSG and friends, hg, svn, jj, fossil)

### `--no-transient`

Record this run normally even if the file opened is one a version control system names -- the override for --transient's own automatic detection

### `--keymap-style TEXT:{emacs,vim,modern}`

Start with this keybinding convention: "emacs" (default), "vim", or "modern" (the same setting ned/set-keymap-style controls; applied after init.janet loads, so this flag wins over any ned/set-keymap-style call there)

## Positional arguments

### `paths TEXT`

Files or directories to open

