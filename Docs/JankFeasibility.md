# Jank as a Replacement for Janet — Feasibility

Whether ned's scripting layer could move from [Janet](https://janet-lang.org) to
[jank](https://github.com/jank-lang/jank), a Clojure dialect on LLVM.

Status: **researched, not committed.** Investigated 2026-09-08 against a real local
install (`jank-0.1-alpha`, `~/.local`, Clang/LLVM 23). Everything below is measured on
that machine, not read off documentation -- a from-scratch host program that initializes
the runtime, loads `clojure.core`, evals new source at runtime and calls jank functions
from C++ was built to produce these numbers.

**Verdict: an embedded jank with a working JIT is real and already builds today.** The
blockers are not "can it be embedded" -- they are memory footprint, a
garbage-collector/threading contract ned currently violates 39 times over, and 0.1-alpha
API ergonomics.

Tracked as a Maybelist entry in `ROADMAP.md`; this file is the evidence behind it.

## The findings

Replacing ned's internal scripting representation with
[jank](https://github.com/jank-lang/jank). Feasibility was investigated against a
real local install on 2026-09-08 (`jank-0.1-alpha`, `~/.local`, Clang/LLVM 23);
everything below is measured on this machine, not read off documentation. **Verdict:
an embedded jank with a working JIT is real and already builds today** — a
from-scratch host program that initializes the runtime, loads `clojure.core`, evals
new source at runtime, and calls jank functions from C++ took an afternoon. The
blockers are not "can it be embedded"; they are memory footprint, a
garbage-collector/threading contract that ned currently violates 39 times over, and
0.1-alpha API ergonomics.

**The central fork: static vs. dynamic runtime.** The install ships two archives.
`libjank-static-runtime.a` links no LLVM at all (a hello-world embed is 2.7 MB, 0.5
ms to init) but refuses at runtime with *"The 'eval' feature is unsupported in a
static runtime"* — it is AOT-only. `libjank-dynamic-runtime.a` carries the codegen
and C++-interop objects (`cpp_*`, `builder`, `clang`) and does eval, at the cost of
dynamically linking `libclang-cpp.so.23` (84 MB) and `libLLVM.so.23` (182 MB). Ned's
whole scripting model — `init.janet` evaluated at startup, `ned/register-command`
defining commands at runtime, a REPL panel, project-local plugins — is eval. **Ned
needs the dynamic runtime, and therefore ships a hard dependency on a matching
Clang/LLVM.** That is the single largest difference from Janet, which is a ~1 MB
library with no toolchain dependency at all.

**Measured, embedded, on this machine** (host program in
`git log --grep=jank-feasibility` if kept; recipe below):

| | |
|---|---|
| PCH read (68 MB, warm page cache) | ~20 ms |
| `runtime::context` construction | ~80 ms |
| `clojure.core` load (AOT-compiled into the archive) | ~25 ms |
| first eval — runtime usable | **~170 ms** |
| first `defn` after that (one-time JIT warm-up) | ~85 ms |
| every subsequent form | **0.1–0.2 ms** |
| 50-form config file (the `init.janet` shape) | ~5 ms |
| 100k C++→jank calls | ~19 ms (0.19 µs/call) |
| process wall time, start to exit | 0.32 s |
| **peak RSS** | **~237 MB** |

Startup and call cost are fine — a realistic config file is ~5 ms, and the
per-form cost after warm-up is negligible. **RSS is the problem**: ned with a file
open is currently ~28 MB resident. Embedding jank makes the floor ~10× that before
a single buffer is loaded, which sits badly beside the huge-file work that went in
specifically to keep memory bounded.

**Boehm GC and ned's threads — smaller than it first looks.** The GC is not a choice:
BDWGC (`pthread_support.c.o`, `mark.c.o`, `thread_local_alloc.c.o`, ...) is statically
baked into both jank archives, and jank's object model is built on it
(`new (UseGC) context{}`, `IMMER_HAS_LIBGC=1`). Swapping in another GC library is not
an available move; ned can only manage its *interaction* with the one jank brings.

Upstream bdwgc already ships the fix, and ned would invent nothing — but its
*zero-effort* mechanism doesn't reach us. `gc_pthread_redirects.h` macro-redirects
`pthread_create` → `GC_pthread_create` (present in the archive, along with
`GC_pthread_join`/`_detach`/`_exit`/`_cancel`/`_sigmask`), which auto-registers the
thread with no application code at all. Verified: a raw `pthread_create` through
that macro survives allocation + collection cleanly. Equally verified: **the same
build with `std::thread` still aborts**, because libstdc++ makes the `pthread_create`
call inside an already-compiled `.so` where no macro of ned's can reach it.
`-Wl,--wrap=pthread_create` (bdwgc's `GC_USE_LD_WRAP` path) doesn't rescue it either
— same reason, and this jank build ships no `__wrap_pthread_create` anyway.

For `std::thread` the answer is bdwgc's explicit API, which is all the "wrapper"
anyone needs — `GC_get_stack_base` + `GC_register_my_thread` on entry,
`GC_unregister_my_thread` on exit, about eight lines of RAII over the library's own
functions. Verified under load: four `std::thread`s doing 500 allocations and 40
collections each, concurrently, all clean. `gc_cpp.h` is no help here; it covers
allocation (`class gc`, `gc_cleanup`), not threads.

Three thread classes, each measured:

| thread does | result |
|---|---|
| never touches jank | **safe unregistered** — 50 main-thread collections, 15k spins, fine |
| allocates / calls into jank | **hard abort** — `Collecting from unknown thread`, core dump |
| merely *holds* a jank ref | **silent corruption** — object collected and its memory reused under load |

That third row is the dangerous one, and it only shows up under pressure: with light
load the held value looks intact, and it takes ~400k allocations plus 200 collections
on another thread before the contents turn to garbage. So the rule is not "threads
that allocate" but **"any thread that touches a jank object at all must be
registered"** — there is no safe read-only tier.

The good news is that ned already satisfies this almost for free. Scripting is
main-thread-only *by existing design*: every background subsystem marshals through
`EventLoop::Post`, and the contract is written down (`TestRunner.h`: "including a
Janet-backed parser -- always runs on the main thread"; `Vcs/Runner.h` the same, which
is precisely why a Janet VCS provider splits argv-building from subprocess-running).
None of the four files including a Janet header creates a thread. BDWGC registers the
main thread itself at `GC_init`. So the initial port needs **zero** thread
registration — the 39 `std::thread`/`std::jthread` in the tree are all in the
"oblivious" row.

What that buys is an invariant to *defend*, not a migration to perform: the
eight-line RAII scope above, kept on the shelf for the day some thread legitimately
needs to touch scripting, plus a debug-build assertion in the binding layer that it is
running on the registered thread. Worth having early, because the failure it guards is
silent corruption rather than a crash.

**What ned would actually have to change.** Smaller than it looks: only four files
outside `Source/Janet/` include a Janet header (`Editor/JanetSymbolComplete.cpp`,
`UI/JanetReplPanel.cpp`, `UI/BufferView.cpp`, `main.cpp`). `ScriptingSession`'s
language-agnostic seam did its job — the other ~117 files mentioning Janet only name
`ned/set-*` bindings in comments. The real work is the 149 `env.Register<>` bindings
in `EditorBindings.cpp` (3.2k lines across `Source/Janet/`), `JanetVcsProvider`, the
bundled `vcs-git.janet` plugin, ~1.2k lines of Janet-specific tests, and every
user's `init.janet`.

One genuine architectural *win* to weigh against all this: `jank_closure_create`
takes a `void *context`. Janet's `JanetCFunction` has a fixed signature with nowhere
to put one, which is the entire reason `ScriptingSession`'s
`ScriptingSessionScope`/`CommandContextScope` current-session globals exist — the
codebase's one deliberate piece of global-ish state. Closures with context delete
that workaround outright. (Partially verified: a native closure interned as
`ned/insert` *is* invoked from jank source, but neither shape tried for retrieving
the context inside the callback worked, and both failed by crashing rather than
erroring — representative of the alpha's rough edges.)

**Things not yet raised that will need answers:**
- **`clojure.core` cannot be bootstrapped through the C API.** `jank/c_api.h` has no
  module loader; `main.cpp`'s sequence (`__rt_ctx = new (UseGC) context{}`,
  `jank_load_clojure_core_native()`, `module_loader.add_load_fn`, `load_module`) is
  C++-only. Ned must adopt jank's C++ headers, not just the C shim.
- **Inversion of control.** `jank_init_dynamic(argc, argv, ..., fn)` calls *your*
  function; jank wants to own the outermost frame. Ned's `main.cpp` composition root
  and `EventLoop::Run` would have to move inside that callback.
- **`-rdynamic` is mandatory and conflicts with `--gc-sections`.** The JIT resolves
  symbols against the host executable's dynamic symbol table; without it, evaluating
  anything fails with `Symbols not found: [...jank::runtime::obj::small_integer...]`.
  Verified both ways. Ned's link line and binary size are both affected.
- **PCH as a deployment artifact.** The dynamic runtime needs a 68 MB
  `incremental.pch`, generated per install into
  `~/.cache/jank/<target-triple>-<binary-version-hash>/`. Ned would have to locate,
  version-match, and possibly generate it — and decide what happens when it's absent
  or stale.
- **Sanitizers.** ned's suite is expected to stay clean under ASan/UBSan and treats
  findings as real bugs. An ASan build of the embed links and runs, but
  LeakSanitizer reports jank's own allocations (`jit::processor` `strdup`s and
  similar). A suppression file becomes a permanent fixture, which weakens the
  guarantee the current policy rests on.
- **`jank print-cflags` is not yet a usable build integration.** It emits
  `-I/Development/Jank/compiler+runtime/...` build-tree paths from wherever jank was
  compiled, an unresolved `-Ljank_lib_link_dirs_prop-NOTFOUND`, and omits the
  LLVM/clang/crypto libs the dynamic runtime actually needs (`-lclang-cpp -lLLVM
  -lcrypto` had to be added by hand). It also emits `-l` flags mixed with `-I`, so
  argument order matters. A CMake integration would be hand-rolled today.
- **Invasive global compile flags.** jank's flags include `-femulated-tls`,
  `-fno-stack-protector`, `-D_FORTIFY_SOURCE=0`, `-fwrapv`, `-DPOINTER_MASK=...`,
  `-DGC_THREADS`, `-D_GLIBCXX_USE_CXX11_ABI=1`. How far these have to propagate
  through `ned_lib` (versus being confined to the TUs that include jank) is
  unresolved and matters for the rest of the codebase's codegen.
- **C++23 is fine.** jank ships `-std=c++20`, but its headers compile clean in a
  C++23 TU — ned does not have to downgrade. (Verified.)
- **Two languages at once, or a hard cut?** `ScriptingSession` could host both
  during a transition, at the cost of carrying two runtimes (and two GCs) in one
  process. Worth deciding early; it changes everything downstream.
- **User-facing cost.** Every `init.janet` in the world becomes an `init.jank`.
  Ned's own `vcs-git.janet` plugin, the Janet REPL panel, `JanetSymbolComplete`'s
  binding-aware completion, and the `ned/register-test-parser` callback convention
  all get rewritten.

**Suggested sequencing** (nothing here commits to the swap):
1. Add a debug assertion that scripting entry points run on the registered thread,
   and a small RAII `GcThread` for future use. Cheap, and it turns the
   main-thread-only rule from an undocumented convention into something enforced
   before it can be broken.
2. Stand up a throwaway `ned_lib`-linked spike that embeds jank beside Janet and
   exposes ~5 real bindings through it — enough to feel the closure-context
   ergonomics and confirm the RSS number in a real ned process rather than a toy.
3. Only then decide static-vs-dynamic, dual-runtime-vs-hard-cut, and whether 237 MB
   of baseline RSS is a price this editor is willing to pay.

**Reproduction recipe** (what worked, after several that didn't): compile with
`jank print-cflags`, source file *before* the flags (they contain `-l`s), swap
`-ljank-static-runtime` → `-ljank-dynamic-runtime`, drop `-Wl,--gc-sections`, add
`-rdynamic -L/usr/lib/llvm/23/lib64 -lclang-cpp -lLLVM -lcrypto`, read
`~/.cache/jank/$(jank print-binary-version)/incremental.pch` into memory and hand it
to `jank_init_dynamic`, then do `main.cpp`'s `clojure.core` bootstrap inside the
callback. Note `jank print-cflags` must not be word-split by a shell that doesn't
(fish keeps it as one argument — build the argv programmatically).

Upstream is reportedly making embedded builds easier in the coming weeks (noted
2026-09-08); several rough edges above — `print-cflags` build-tree leakage, the
C-API module-loading gap, PCH distribution — are exactly the kind of thing that
might disappear on their own. Worth re-checking before investing in workarounds.
