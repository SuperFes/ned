# Third-party licenses

Ned (MIT-licensed, see `LICENSE`) is built on the open-source projects below.
Everything in the first table is statically linked into the `ned` binary
itself; everything in the second is a separate shared library ned loads at
runtime, never bundled or statically embedded. Versions match whatever
`CMakeLists.txt` currently pins — check there for the exact tag if a license
determination ever depends on it.

## Statically linked (embedded in the `ned` binary)

| Project | License | Copyright |
|---|---|---|
| [utf8proc](https://github.com/JuliaStrings/utf8proc) | MIT | © 2014-2021 Steven G. Johnson, Jiahao Chen, Tony Kelman, Jonas Fonseca, and contributors; © 2009, 2013 Public Software Group e.V. (original utf8proc). Bundled Unicode character data under the separate, also-permissive [Unicode data license](https://www.unicode.org/copyright.html). |
| [CLI11](https://github.com/CLIUtils/CLI11) | BSD-3-Clause | © 2017-2025 University of Cincinnati (Henry Schreiner, NSF Award 1414736) |
| [nlohmann/json](https://github.com/nlohmann/json) | MIT | © 2013-2025 Niels Lohmann |
| [tree-sitter](https://github.com/tree-sitter/tree-sitter) (core) | MIT | © 2018-2024 Max Brunsfeld |
| tree-sitter-{json,c,cpp,php,javascript,typescript,tsx,html,css,python,bash} | MIT | © Max Brunsfeld and the tree-sitter project (each grammar's own repo) |
| [tree-sitter-janet-simple](https://github.com/sogaiu/tree-sitter-janet-simple) | CC0 1.0 (public domain) | sogaiu |
| [tree-sitter-clojure](https://github.com/sogaiu/tree-sitter-clojure) | CC0 1.0 (public domain) | sogaiu |
| tree-sitter-markdown, tree-sitter-markdown-inline | MIT | © 2021 Matthias Deiml |
| [tree-sitter-toml](https://github.com/tree-sitter-grammars/tree-sitter-toml) | MIT | © Ika |
| [tree-sitter-yaml](https://github.com/tree-sitter-grammars/tree-sitter-yaml) | MIT | © 2024 tree-sitter-grammars contributors; © 2019-2021 Ika |
| tree-sitter-ned-org (forked from [tree-sitter-org](https://github.com/nvim-orgmode/tree-sitter-org)) | MIT | © 2021-2022 Emilia Simmons |
| [libvterm](https://github.com/neovim/libvterm) | MIT | Paul "LeoNerd" Evans |
| [Catch2](https://github.com/catchorg/Catch2) (test-only, never shipped) | Boost Software License 1.0 | Catch2 Authors |

## Dynamically linked (loaded at runtime, not embedded)

| Project | License |
|---|---|
| [Notcurses](https://github.com/dankamongmen/notcurses) (notcurses-core) | Apache License 2.0 |
| [Janet](https://github.com/janet-lang/janet) | MIT |
| [ncurses](https://invisible-island.net/ncurses/) (terminfo) | MIT (X11-style) |
| [libunistring](https://www.gnu.org/software/libunistring/) | `(LGPL-3.0-or-later OR GPL-2.0-or-later)` — dynamically linked only, per LGPL's own linking exception; never statically embedded |
| [libdeflate](https://github.com/ebiggers/libdeflate) | MIT |

Full license texts for the short/permissive licenses above (MIT, BSD-3-Clause,
CC0, Boost-1.0) are reproduced in each project's own repository at the paths
linked; the Apache License 2.0 and LGPL/GPL texts are the standard,
unmodified upstream versions, available at
<https://www.apache.org/licenses/LICENSE-2.0> and
<https://www.gnu.org/licenses/>.
