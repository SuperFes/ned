# Terminal and Agents

## Embedded terminal

`toggle-terminal` shows and focuses a real terminal drawer — a full VT100/xterm
emulation on a real pty, running your shell. While it's focused, nearly every key
forwards straight through to the shell (including `C-c`/`C-z`/`C-s`), so the drawer
doesn't fight your shell's own keybindings. The one reserved key is the toggle chord
itself; a title-row minimize/maximize/close set of mouse buttons is also available as a
keyboard-independent way to dismiss it on a terminal where the toggle chord doesn't
arrive cleanly. A shell that exits leaves its output visible with `[process exited]`
appended — press Enter to respawn it.

## Language-specific REPLs

Beyond the general-purpose terminal, `run-repl` opens a REPL for a specific language,
configured the same way LSP/DAP commands are:

```janet
(ned/set-repl-command "python" ["python3" "-i"])
(ned/set-repl-command "php" ["php" "-a"])
```

This runs the REPL as a real interactive CLI on its own pty (the same mechanism as the
embedded terminal), shown exactly as it would appear in a real terminal — there's no
send-region-to-REPL or prompt-detection layer on top.

## The built-in Janet REPL

`toggle-janet-repl` (`C-c j`) needs no configuration — it evaluates directly against the
running editor's own Janet environment, in-process. This is the fastest way to try out a
binding from the [Scripting](../scripting.md) reference or poke at editor state without
editing and reloading `init.janet`.

## Agent Client Protocol (ACP)

ACP is Zed's open standard for editor/agent communication — the same idea as LSP, but
for a conversational coding agent instead of static language intelligence. Configure an
agent:

```janet
(ned/set-acp-agent "claude-code" ["claude-code-acp"])
```

| Command | Effect |
|---|---|
| `acp-start-session` | Start a session with a configured agent |
| `acp-send-prompt` | Send a message to the active session |
| `acp-stop-session` | Stop the active session |
| `acp-toggle-panel` | Show, focus, or hide the ACP chat panel |
| `acp-rewind` | Rewind the conversation and its file edits to before an earlier turn |
| `ask-agent-about-line` | Ask the active agent about the diagnostic/test-failure line at point |

The chat panel renders the conversation structurally — plain messages, tool-call lines,
a plan checklist, and permission prompts with their options listed — rather than as a
raw text log, and docks at the bottom or right edge
(`ned/set-acp-panel-dock`). `acp-toggle-panel` is bound to `C-c c`; session commands sit
on `C-c A` (shifted "A") rather than a plain `C-c a`, since `C-c a` already runs
`org-agenda`.

An agent can also ask *ned* questions back, through the same connection: a small,
read-only set of MCP tools exposes things like reading the current buffer or diagnostics
to the connected agent, so it can ground its answers in what's actually open rather than
guessing from the prompt alone.

## Next steps

- [Configuration](../configuration.md)
