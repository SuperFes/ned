# Debugging

ned speaks the Debug Adapter Protocol (DAP) — the same protocol VS Code's debuggers use
— to drive a real debug session: breakpoints, stepping, variable inspection, and more,
without leaving the editor. Unlike LSP, DAP is deliberately one session at a time:
debugging is inherently modal in a way editing several files at once isn't.

## Configuring an adapter

As with LSP, ned bundles no debug adapters — you point it at one already installed:

```janet
(ned/set-dap-adapter "cpp" ["lldb-dap"])
(ned/set-dap-launch "cpp" {:program "./build/my-program"})
```

See your language's debug adapter documentation for the exact launch configuration shape
it expects (`program`, `args`, `cwd`, etc. — passed through as-is).

## Breakpoints

| Command | Effect |
|---|---|
| `dap-toggle-breakpoint` | Toggle a plain breakpoint on the current line |
| `dap-toggle-function-breakpoint` | Break on entry to a named function |
| `dap-set-breakpoint-condition` | Only break when a condition expression is true |
| `dap-set-breakpoint-hit-condition` | Only break once a hit count is satisfied (e.g. `"> 5"`) |
| `dap-set-breakpoint-log-message` | Turn a breakpoint into a logpoint — logs a message but never halts |
| `dap-select-exception-breakpoints` | Choose which of the adapter's exception filters halt execution |

Breakpoints persist across sessions and are pushed live to an already-running debug
session the moment you set one.

## Running and stepping

| Command | Effect |
|---|---|
| `dap-continue` | Start a session, or continue a stopped one |
| `dap-pause` / `dap-stop` | Pause / stop the debuggee |
| `dap-step-over` / `dap-step-into` / `dap-step-out` | Standard stepping |
| `dap-run-to-cursor` | Run until execution reaches the current line |
| `dap-step-back` / `dap-reverse-continue` | Step or continue *backwards* (needs adapter support — not every debugger implements reverse execution) |
| `dap-jump-to-line` | Move execution directly to the current line without running the skipped code (adapter support required) |
| `dap-restart-frame` | Restart execution from the top of a stack frame |
| `dap-attach` | Attach to an already-running process instead of launching a new one |

## Inspecting state

`dap-show-debug` opens a `*debug*` buffer with the stopped session's stack and variables.
From there:

| Command | Effect |
|---|---|
| `dap-expand-variable` | Expand a composite variable in place |
| `dap-set-variable` | Edit a variable's value directly |
| `dap-toggle-hex-format` | Toggle hex display for a variable or watch |
| `dap-add-watch` / `dap-remove-watch` | Manage watch expressions, re-evaluated on every stop |
| `dap-toggle-watch-graph` | Show a sparkline (scalar history) or bar chart (numeric array) for a watch |
| `dap-evaluate` | Evaluate an arbitrary expression in the stopped frame |
| `dap-line-inspect` | Evaluate every sub-expression on the current source line at once |
| `dap-select-thread` / `dap-toggle-threads` | Pick a thread, or keep a live-updating thread panel open |
| `dap-show-disassembly` | View instructions around the current program counter |
| `dap-show-memory-at-point` | Hex-dump memory for a variable |
| `dap-show-memory-image-at-point` | Render memory as a grayscale image — repeating structures, zero-fill, and embedded text are visible at a glance |
| `dap-show-pointer-graph` | Browse a composite variable as an expandable pointer/field graph |
| `dap-toggle-console` | Show or hide the debug console (a REPL into the stopped session) |
| `dap-ask-agent` | Send the current stack and variables to an active [ACP agent](terminal-and-agents.md#agent-client-protocol-acp) as a prompt |

## Next steps

- [Tasks and Tests](tasks-and-tests.md) — running builds and test suites, the other half
  of a typical edit/build/debug loop.
