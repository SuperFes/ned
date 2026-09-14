# Configuration

ned has no config-file format of its own — configuration *is* Janet code, run at
startup. There's no ceiling between "settings" and "scripting": the same
`ned/set-theme` call that picks a color scheme sits in the same file as a custom command
definition or a new keybinding.

## Where `init.janet` lives

ned looks for `$XDG_CONFIG_HOME/ned/init.janet`, falling back to
`~/.config/ned/init.janet` if `$XDG_CONFIG_HOME` isn't set. It's loaded once at startup,
after every builtin command and Janet binding is registered — so anything in this guide
or the [Command Reference](commands.md)/[Scripting](scripting.md) chapters is already
available by the time your init file runs.

If the file doesn't exist, ned just starts with its built-in defaults. There's no
"first run" wizard or generated starter file.

## A minimal example

```janet
# Pick a theme
(ned/set-theme "gruvbox-dark")

# Bind a key to a built-in command
(ned/define-key "C-c g" "goto-line")

# Configure a language server for C++
(ned/set-lsp-command "c" ["clangd"])
(ned/set-lsp-command "cpp" ["clangd"])

# Register a brand new command and bind it
(ned/register-command "insert-todo"
  "Insert a TODO comment at point."
  (fn [ctx] (ned/insert-string "// TODO: ")))
(ned/define-key "C-c t" "insert-todo")
```

Every `ned/set-*` binding follows the same shape: a setter with a matching Janet-side
getter, documented in the [Scripting](scripting.md) reference. There's no separate
"restart to apply" step for most of these — they take effect as soon as `init.janet`
finishes loading.

## Keybindings

`ned/define-key` takes an Emacs `kbd`-style sequence string and a command name:

```janet
(ned/define-key "C-c C-j" "some-command-name")
```

Sequences can be multiple chords long (`"C-c C-j"` is Ctrl+C then Ctrl+J), and rebinding
a key that already has a binding simply replaces it — there's no separate unbind step
needed first. See the [Command Reference](commands.md) for the full list of what you can
bind, and [Getting Started](getting-started.md) for the defaults already in place.

## Registering your own commands

`ned/register-command` takes a name, a docstring, and a Janet function. Once registered,
it behaves exactly like a built-in command: reachable from `M-x`, bindable with
`ned/define-key`, and callable from other Janet code. Re-registering an existing name
(built-in or your own) redefines it — this is expected, not an error, and is how you'd
override a bundled command's behavior if you needed to.

## Project-local configuration

A project can carry its own `.ned/init.janet`, loaded after your personal `init.janet`
and scoped to that project only — useful for per-project LSP commands, task definitions,
or keybindings that don't belong in your global config. Because this file executes
arbitrary code the moment you open the directory, ned asks for confirmation
(`yes`/`no`/`always`) the first time it sees a project's `.ned/init.janet`, keyed on the
file's content — editing it later re-prompts. A project can also drop standalone plugin
files under `.ned/plugins/*.janet` (loaded non-recursively, in lexicographic order,
before `.ned/init.janet`), which go through the same trust prompt.

## Where else ned writes files

Everything ned persists on its own — not configuration you write, but state it
remembers — follows the XDG Base Directory spec:

- `$XDG_CONFIG_HOME/ned/` — `init.janet` itself.
- `$XDG_DATA_HOME/ned/` — scratch-buffer notes.
- `$XDG_STATE_HOME/ned/` — save-place (last cursor position per file), project sessions,
  backups/crash-recovery autosaves, the project-trust registry, and a few
  editor-remembered variables (like the last theme picked interactively).

None of this ever lands as a bare dotfile directly in `$HOME`.

## Next steps

- [Scripting](scripting.md) — the complete list of `ned/*` Janet bindings.
- [Command Reference](commands.md) — the complete list of registered commands.
- [Theming](theming.md) — bundled themes and how to author your own.
