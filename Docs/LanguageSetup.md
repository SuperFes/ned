# Per-language setup recipes

`java`, `kotlin`, `csharp`, `go`, `rust`, `bash`, `lua`, and `cmake` all get syntax
highlighting, folding, and indentation with no setup at all (see
[Language Intelligence](features/language-intelligence.md)). Language
server, debugger, and test-runner integration is a separate, explicit step: ned never
bundles or auto-detects any of those, so this page is one recipe per language to paste
into `init.janet`.

Every recommendation below is the community-standard tool for that ecosystem, not a
ned-specific choice; verify a binary is actually on your `$PATH` before wiring it in.
Where no clean answer exists for a language's debugger or test runner, that's stated
plainly rather than papered over with something untested.

## Root markers

A root marker is the filename ned looks for when deciding which ancestor directory to
treat as a language's project root (for a monorepo, that lets a subpackage get its own
LSP server instead of everything sharing one pinned to the outer repo). Most bundled
languages already carry sensible defaults; `ned/set-lsp-root-markers` overrides them.
Current defaults for the languages on this page:

- `java`/`kotlin` — `pom.xml`, `build.gradle`, `build.gradle.kts`,
  `settings.gradle`, `settings.gradle.kts` (already bundled).
- `csharp` — `global.json`, `*.csproj`, `*.sln` (already bundled).
- `go` — `go.mod` (already bundled).
- `rust` — `Cargo.toml` (already bundled).
- `lua` — `.luarc.json`, `.luarc.jsonc` (added alongside this doc).
- `bash`, `cmake` — deliberately none. Bash scripts have no fixed project-marker
  convention to walk for; a CMake marker of `CMakeLists.txt` would match
  trivially at a nested file's own directory instead of finding the real top.
  Both fall back to ned's ordinary project root, which is already correct for
  the common case (a repo with one `CMakeLists.txt`/script tree and a single root).

## Java

```janet
(ned/set-lsp-command "java" ["jdtls"])
```

[Eclipse JDT Language Server](https://github.com/eclipse-jdtls/eclipse.jdt.ls)
is the de facto standard. Check your distribution's `jdtls` wrapper script:
some pick a per-project `-data` workspace directory automatically (keyed on
cwd), others require passing `-data <dir>` explicitly — sharing one workspace
across unrelated projects corrupts jdtls's index, so this is worth getting
right once rather than debugging a confused server later.

**DAP:** intentionally not recommended here. `java-debug` is a jdtls *plugin*,
not a standalone binary `ned/set-dap-adapter` can spawn on its own — wiring it
up means jdtls bootstrapping the debug server itself via a
`workspace/executeCommand`, which doesn't fit ned's "spawn one adapter
process" debugger model. Recording this as a real gap rather than shipping an
unverified command.

**Test runner:** Maven/Gradle's own output isn't one of the seven built-in
formats, and their JUnit XML reports (`target/surefire-reports/`,
`build/test-results/test/`) are written as *one file per test class* —
`ned/set-test-results-file` names exactly one file, so it doesn't compose
cleanly for anything but a single-class project. `ned/register-test-parser`
with a small Janet function that globs and merges those XML files is the real
path here; not done in this pass.

## Kotlin

```janet
(ned/set-lsp-command "kotlin" ["kotlin-language-server"])
(ned/set-dap-adapter "kotlin" ["kotlin-debug-adapter"])
(ned/set-dap-launch "kotlin" `{"mainClass": "MainKt", "projectRoot": "."}`)
```

[kotlin-language-server](https://github.com/fwcd/kotlin-language-server) and
[kotlin-debug-adapter](https://github.com/fwcd/kotlin-debug-adapter) are the
same author's pair, both standalone stdio processes — unlike Java, the debug
adapter here genuinely is spawnable on its own. `mainClass`/`projectRoot` are
that adapter's own launch keys; confirm against its README for your build
layout before relying on it.

**Test runner:** same Gradle multi-file JUnit-XML gap as Java above.

## C#

```janet
(ned/set-lsp-command "csharp" ["csharp-ls"])
(ned/set-dap-adapter "csharp" ["netcoredbg" "--interpreter=vscode"])
(ned/set-dap-launch "csharp" `{"program": "${workspaceFolder}/bin/Debug/net8.0/MyApp.dll"}`)
```

[csharp-ls](https://github.com/razzmatazz/csharp-language-server) is the
simpler single-binary option; OmniSharp (`OmniSharp --languageserver`) is the
heavier alternative for large multi-project solutions.
[netcoredbg](https://github.com/Samsung/netcoredbg) is purpose-built as a
VS-Code-DAP-compatible stdio adapter — `--interpreter=vscode` is its
documented, standard flag. `program` needs your actual build output path (no
`${workspaceFolder}` substitution happens on ned's side — replace it by hand,
or with whatever your build always produces).

**Test runner:** `dotnet test`'s human-readable output isn't a built-in
format. The community `JunitXml.TestLogger` NuGet package
(`dotnet test --logger "junit;LogFilePath=results.xml"`) produces one file for
a single-project solution, which *does* fit `ned/set-test-results-file` +
`"junit-xml"` — but it's an added dependency, not something `dotnet test` does
out of the box, and a multi-project solution is back to the same
multiple-files problem as Java/Kotlin.

## Go

```janet
(ned/set-lsp-command "go" ["gopls"])
(ned/set-dap-adapter "go" ["dlv" "dap"])
(ned/set-dap-launch "go" `{"mode": "auto", "program": "."}`)
(ned/set-test-command ["go" "test" "-json" "./..."] "go-json")
(ned/set-test-filter-command ["go" "test" "-json" "-run" "^{test}$" "./..."])
```

The cleanest of this batch — [gopls](https://pkg.go.dev/golang.org/x/tools/gopls)
and [Delve](https://github.com/go-delve/delve) are both the unambiguous
standard, and `go test -json` is a direct, first-class fit for the built-in
`"go-json"` parser (it exists precisely for this).

## Rust

```janet
(ned/set-lsp-command "rust" ["rust-analyzer"])
(ned/set-dap-adapter "rust" ["lldb-dap"])
(ned/set-dap-launch "rust" `{"program": "${cargo:program}"}`)
(ned/set-test-command ["cargo" "test"] "cargo")
(ned/set-test-filter-command ["cargo" "test" "{test}"])
```

[rust-analyzer](https://rust-analyzer.github.io/) is unambiguous.
[`lldb-dap`](https://lldb.llvm.org/use/map.html) (older LLVM releases name it
`lldb-vscode`) is recommended over VS Code's bundled `codelldb` here
specifically because it speaks plain stdio DAP the way `ned/set-dap-adapter`
expects — `codelldb` is typically driven over a TCP port the VS Code extension
manages, and ned doesn't (yet) support that transport.
Replace `${cargo:program}` with your actual built binary path — that
substitution is VS Code's own, not ned's. `cargo test` maps directly onto the
built-in `"cargo"` format.

## Bash

```janet
(ned/set-lsp-command "bash" ["bash-language-server" "start"])
```

[bash-language-server](https://github.com/bash-lsp/bash-language-server)'s
binary requires the `start` subcommand.

**DAP:** no recommendation. No widely-used Bash debugger speaks DAP;
`bashdb` predates the protocol and has no adapter shim.

**Test runner:** [bats](https://github.com/bats-core/bats-core) is the closest
thing to a standard, but its TAP-shaped output doesn't match any of the seven
built-in formats. A `ned/register-test-parser` TAP parser would close this —
worth doing if bats usage shows up, not speculatively.

## Lua

```janet
(ned/set-lsp-command "lua" ["lua-language-server"])
```

[lua-language-server](https://github.com/LuaLS/lua-language-server) (sumneko)
is the standard. Root markers now default to `.luarc.json`/`.luarc.jsonc`.

**DAP:** no single de facto standalone adapter — Lua debugging tooling is
generally embedded in a specific runtime (LÖVE2D, a game engine, Neovim's own
config), not one binary that fits every project. Wire up per-runtime rather
than picking one here.

**Test runner:** [busted](https://lunarmodules.github.io/busted/) is the
standard framework; its output format isn't one of the seven built-in parsers.
Same `ned/register-test-parser` path as Bash/bats, not done here.

## CMake

```janet
(ned/set-lsp-command "cmake" ["cmake-language-server"])
```

[cmake-language-server](https://github.com/regen100/cmake-language-server)
(Python) is the more established option;
[neocmakelsp](https://github.com/Decodetalkers/neocmakelsp) is a newer Rust
alternative. No DAP or test-runner entry — CMake is a build-configuration
language, not something run or tested on its own (`ned`'s own CMake project is
tested via CTest, which is a C++ test-runner concern, not a CMake-language
one).

## Formatter: the one process-wide limitation worth knowing

`ned/set-format-command` is a single, process-wide command — not per-language,
a deliberate v1 scope cut. It also receives only the
buffer's text over stdin/stdout, no filename — there's no way for a dispatch
script to know which language it's formatting. For any of these languages the
usual per-ecosystem formatter still works fine standalone
(`gofmt`/`rustfmt`/`google-java-format`/`ktfmt`/`csharpier`/`shfmt`/`stylua`/
`cmake-format`), but wiring more than one into `ned/set-format-command`
genuinely isn't possible today for a project mixing languages. Worth reopening
if per-language format-on-save becomes a recurring ask.
