# Tasks and Tests

## Task runner

Run any shell command from `init.janet`, with output streamed into a live buffer:

```janet
(ned/set-task-command "build" ["cmake" "--build" "build"])
```

`run-task` runs it, finding or creating a `*task: build*` buffer for the output; running
the same task again appends to the same buffer with a `--- re-run ---` separator rather
than opening a new one. There's no framing or protocol here — it's a plain subprocess
whose combined stdout/stderr you watch scroll by.

## Test runner

The test runner is the structured sibling of the task runner: it understands a handful
of common test-output formats and turns them into a navigable, gutter-annotated result
set rather than just raw scrollback.

```janet
(ned/set-test-command ["ctest" "--test-dir" "build"] "ctest")
```

The second argument names the output format. Built in: `"ctest"`, `"catch2"`,
`"pytest"`, `"go-json"`, `"cargo"`, `"junit-xml"`, and `"phpunit"`. If your test runner
writes results to a file instead of stdout (JUnit XML is the common case):

```janet
(ned/set-test-command ["pytest" "--junitxml" "/tmp/results.xml"] "junit-xml")
(ned/set-test-results-file "/tmp/results.xml")
```

| Command | Effect |
|---|---|
| `run-tests` | Run the full configured test command, streaming into `*test output*` and parsing into `*test results*` plus per-test gutter marks |
| `cancel-tests` | Cancel a run in progress |
| `show-test-results` | Show the parsed failures from the last run |

### Running one test at a time

If your test framework supports filtering to a single test by name, configure a
template and ned can run just the test under point:

```janet
(ned/set-test-filter-command ["ctest" "--test-dir" "build" "-R" "^{test}$"])
```

`{test}`/`{file}` in the template are substituted per-element (never through a shell).
With this configured:

| Command | Effect |
|---|---|
| `run-test-at-point` | Run only the test definition containing point |
| `rerun-failed-tests` | Re-run every currently-failed test, one filtered run per test, merging results |

For a bundled language whose tree-sitter grammar has test-discovery support, the gutter
shows a `▸` marker next to a discovered-but-unrun test — clicking it runs that one test
directly, the mouse equivalent of `run-test-at-point`.

### A format ned doesn't already know

`ned/register-test-parser` registers your own parser as a Janet function receiving the
raw output (or results-file contents) and returning result tables — see its entry in the
[Scripting](../scripting.md) reference for the exact shape. A registered name can
override a built-in one, if you need to change how an existing format is interpreted.

## Next steps

- [Debugging](debugging.md) — for stepping through a failing test rather than just
  seeing that it failed.
- [Version Control](version-control.md)
