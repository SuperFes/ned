# Installation

There's no packaged release yet — see the project's `ROADMAP.md` for the state of that
effort. For now, building from source is the only path, and it's a normal CMake build
once the dependencies are in place.

## Dependencies

ned links against a handful of system libraries rather than vendoring most of its
dependencies:

| Dependency | Purpose |
|---|---|
| [Janet](https://janet-lang.org/) | The scripting language throughout the editor |
| [Notcurses](https://github.com/dankamongmen/notcurses) (`notcurses-core`, exactly v3.0.17) | Terminal rendering |
| `libutf8proc` | Unicode grapheme-cluster segmentation |
| [CLI11](https://github.com/CLIUtils/CLI11) | Command-line argument parsing |
| [nlohmann/json](https://github.com/nlohmann/json) | JSON, mainly for the LSP/DAP/ACP clients |
| `libvterm` | The embedded terminal panel's VT100/xterm emulation |
| RE2 | Project-wide search |
| PCRE2 | In-file regex replace (`query-replace-regexp`, project replace) |
| Catch2 (v3) | Tests only |

Everything **except** Janet and Notcurses is a normal system package on most
distributions. Janet isn't packaged widely and has to be built from its pinned version
(v1.32.1 — the codebase has documented, version-specific workarounds for a couple of
its behaviors, so don't substitute a different version). Notcurses needs to be *exactly*
v3.0.17 with three small input-handling patches ned carries — see
[Notcurses specifics](#notcurses-specifics) below.

### Debian/Ubuntu

```sh
sudo apt-get install clang lld pkg-config libunistring-dev libdeflate-dev \
    libncurses-dev libcli11-dev libre2-dev libpcre2-dev libvterm-dev \
    libutf8proc-dev nlohmann-json3-dev catch2 libstdc++-14-dev cmake
```

`libstdc++-14-dev` matters even when building with Clang: ned uses C++23 `<chrono>`
parsing (`std::chrono::parse`), which needs a standard library newer than the one
Ubuntu 24.04 ships by default (GCC 13's libstdc++). Installing GCC 14's libstdc++
alongside is enough — Clang picks up the newer one automatically.

### Gentoo

A `dev-cpp/notcurses` package built against the pinned tag with ned's patches, plus
ordinary system packages for the rest, is the intended path — see
`Patches/notcurses/README.md` in the repository for the patches themselves and an
in-flight ebuild.

### Any distribution

Janet and Notcurses need to be built from source if your distribution doesn't already
carry them at the right version:

```sh
# Janet (exact version — don't substitute a different one)
git clone --depth 1 --branch v1.32.1 https://github.com/janet-lang/janet.git
make -C janet -j"$(nproc)"
sudo make -C janet install
sudo ldconfig
```

#### Notcurses specifics

ned needs Notcurses v3.0.17 exactly (an ABI-sensitive enum changed between patch
versions) plus three small patches for input handling that upstream Notcurses doesn't
have yet — legacy-terminal Ctrl+Space, SGR mouse wheel-right decoding, and bracketed
paste as real events rather than a burst of individual keystrokes. The patches live in
`Patches/notcurses/` in the repository as both `git am`-able commits and plain
`patch -p1` files:

```sh
git clone --branch v3.0.17 --depth 50 https://github.com/dankamongmen/notcurses.git
cd notcurses
git am /path/to/ned/Patches/notcurses/*.patch
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    -DUSE_MULTIMEDIA=none -DUSE_DOCTEST=OFF -DUSE_PANDOC=OFF -DUSE_DOXYGEN=OFF \
    -DBUILD_EXECUTABLES=OFF -DBUILD_FFI_LIBRARY=OFF -DUSE_POC=OFF -DUSE_QRCODEGEN=OFF \
    -DUSE_STATIC=OFF
cmake --build build -j"$(nproc)"
sudo cmake --install build
sudo ldconfig
```

The flags above trim Notcurses down to just what ned uses (no multimedia/doctest/
pandoc/FFI extras) — the exact recipe the project's own CI runs.

## Building ned

Once the dependencies are in place:

```sh
git clone https://github.com/SuperFes/ned.git
cd ned
cmake --preset default
cmake --build build
./build/ned
```

`cmake --preset default` configures with Clang + LLD via `CMakePresets.json` — a bare
`cmake -S . -B build` also works but falls back to whatever `cc`/`c++` resolve to on
your system rather than pinning the toolchain.

Every language ned bundles is a package under `Source/Languages/<name>/` -- its grammar
is `grammar.janet`, compiled to parse tables by ned itself during the build -- so this
build touches no network at all beyond the initial `git clone`.

### Running the test suite

```sh
ctest --test-dir build
```

or directly:

```sh
./build/ned_tests
```

A handful of tests exercise a real system-wide tree-sitter grammar `.so` if one happens
to be installed on your machine, and skip themselves cleanly otherwise — a "skipped"
result there is expected on most machines, not a problem.

## Next steps

Once `ned` builds and runs, continue to [Getting Started](getting-started.md).
