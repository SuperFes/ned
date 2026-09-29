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
| External scanners ported from the upstream grammars below (`Source/Editor/Languages/Scanners/`) | Each upstream grammar's own license | See [Language packages](#language-packages) |
| [Unicode Character Database](https://www.unicode.org/ucd/) data (`UnicodeData.txt`, `emoji-data.txt`), as the tables generated into `Source/Editor/Grammar/Compile/UnicodeTables.cpp` and the vendored `Tools/unicode/emoji-data.txt` | Unicode License v3 (notice below) | © 1991-2026 Unicode, Inc. |

## Dynamically linked (loaded at runtime, not embedded)

| Project | License | Copyright |
|---|---|---|
| [utf8proc](https://github.com/JuliaStrings/utf8proc) | MIT | © 2014-2021 Steven G. Johnson, Jiahao Chen, Tony Kelman, Jonas Fonseca, and contributors; © 2009, 2013 Public Software Group e.V. (original utf8proc). Bundled Unicode character data under the separate, also-permissive [Unicode data license](https://www.unicode.org/copyright.html). |
| [CLI11](https://github.com/CLIUtils/CLI11) | BSD-3-Clause | © 2017-2025 University of Cincinnati (Henry Schreiner, NSF Award 1414736) |
| [nlohmann/json](https://github.com/nlohmann/json) | MIT | © 2013-2025 Niels Lohmann |
| [Notcurses](https://github.com/dankamongmen/notcurses) (notcurses-core) | Apache License 2.0 | © 2019-2026 Nick Black; © 2019-2021 Marek Habersack |
| [Janet](https://github.com/janet-lang/janet) | MIT | © Calvin Rose and contributors |
| [ncurses](https://invisible-island.net/ncurses/) (terminfo) | MIT (X11-style) | © Thomas E. Dickey; © Free Software Foundation, Inc. |
| [libunistring](https://www.gnu.org/software/libunistring/) | `(LGPL-3.0-or-later OR GPL-2.0-or-later)` — dynamically linked only, per LGPL's own linking exception; never statically embedded | © Free Software Foundation, Inc. |
| [libdeflate](https://github.com/ebiggers/libdeflate) | MIT | © 2016 Eric Biggers; © 2024 Google LLC |
| [libvterm](https://github.com/neovim/libvterm) | MIT | Paul "LeoNerd" Evans |
| [RE2](https://github.com/google/re2) | BSD-3-Clause | © 2009 The RE2 Authors (Google Inc. and contributors); `util/utf.h`, `util/rune.cc` © 2002 Lucent Technologies under Lucent's permissive notice |
| [PCRE2](https://github.com/PCRE2Project/pcre2) (8-bit library) | BSD-3-Clause WITH PCRE2-exception; JIT compiler BSD-2-Clause | © 1997-2007 University of Cambridge; © 2007-2024 Philip Hazel; JIT © 2009-2024 Zoltan Herczeg |
| [libmagic](https://www.darwinsys.com/file/) (from `file`) | BSD-2-Clause-style (`file`'s own `COPYING`) | © 1985-1995 Ian F. Darwin; © 1994- Christos Zoulas |
| [Catch2](https://github.com/catchorg/Catch2) (test-only, never shipped) | Boost Software License 1.0 | Catch2 Authors |

## Language packages

Each bundled language package under `Source/Languages/<name>/` (installed to
`share/ned/languages/`) is derived from an upstream tree-sitter grammar: its
`grammar.janet` is a translation of the upstream grammar, its `corpus/` is the
upstream test corpus, `upstream/` holds the upstream queries converted to Janet,
and its external scanner, if any, is a port of the upstream `src/scanner.c`
statically linked into `ned`. Those derived files are under their upstream
grammar's license; the compiled `tables` are generated from them. Files ned
authors itself (`language.janet`, its own query files) are under ned's MIT
license. Licenses are
as each repository states them; systemd and apacheconf ship no LICENSE file
and declare MIT in `package.json`.

| Language(s) | Upstream | License | Copyright |
|---|---|---|---|
| racket | [6cdh/tree-sitter-racket](https://github.com/6cdh/tree-sitter-racket) | MIT | © 2022 6cdh |
| scheme | [6cdh/tree-sitter-scheme](https://github.com/6cdh/tree-sitter-scheme) | MIT | © 2022 6cdh |
| matlab | [acristoffers/tree-sitter-matlab](https://github.com/acristoffers/tree-sitter-matlab) | MIT | © 2023 Álan Crístoffer |
| systemd | [adamrunner/tree-sitter-systemd](https://github.com/adamrunner/tree-sitter-systemd) | MIT (per `package.json`; no LICENSE file) | adamrunner and contributors |
| powershell | [airbus-cert/tree-sitter-powershell](https://github.com/airbus-cert/tree-sitter-powershell) | MIT | © 2023 Airbus CERT |
| nim | [alaviss/tree-sitter-nim](https://github.com/alaviss/tree-sitter-nim) | MPL-2.0 | alaviss and contributors |
| swift | [alex-pinkus/tree-sitter-swift](https://github.com/alex-pinkus/tree-sitter-swift) | MIT | © 2021 alex-pinkus |
| pkl | [apple/tree-sitter-pkl](https://github.com/apple/tree-sitter-pkl) | Apache-2.0 | apple and contributors |
| awk | [Beaglefoot/tree-sitter-awk](https://github.com/Beaglefoot/tree-sitter-awk) | MIT | © 2021 Stanislav Chernov |
| ada | [briot/tree-sitter-ada](https://github.com/briot/tree-sitter-ada) | MIT | © 2023 Emmanuel Briot |
| caddy | [caddyserver/tree-sitter-caddyfile](https://github.com/caddyserver/tree-sitter-caddyfile) | MIT | © 2025 Matthew Penner |
| dockerfile | [camdencheek/tree-sitter-dockerfile](https://github.com/camdencheek/tree-sitter-dockerfile) | MIT | © 2021 Camden Cheek |
| asciidoc, asciidoc-inline | [cathaysia/tree-sitter-asciidoc](https://github.com/cathaysia/tree-sitter-asciidoc) | Apache-2.0 | cathaysia and contributors |
| proto | [coder3101/tree-sitter-proto](https://github.com/coder3101/tree-sitter-proto) | MIT | © 2024-2025 Mohammad Ashar Khan |
| crystal | [crystal-lang-tools/tree-sitter-crystal](https://github.com/crystal-lang-tools/tree-sitter-crystal) | MIT | © 2021 Gabriel Holodak |
| sql | [DerekStride/tree-sitter-sql](https://github.com/DerekStride/tree-sitter-sql) | MIT | © 2021 Derek Stride |
| elixir | [elixir-lang/tree-sitter-elixir](https://github.com/elixir-lang/tree-sitter-elixir) | Apache-2.0 | elixir-lang and contributors |
| elm | [elm-tooling/tree-sitter-elm](https://github.com/elm-tooling/tree-sitter-elm) | MIT | © 2018 Kolja Lampe |
| cue | [eonpatapon/tree-sitter-cue](https://github.com/eonpatapon/tree-sitter-cue) | MIT | eonpatapon and contributors |
| kotlin | [fwcd/tree-sitter-kotlin](https://github.com/fwcd/tree-sitter-kotlin) | MIT | © 2019 fwcd |
| gitcommit | [gbprod/tree-sitter-gitcommit](https://github.com/gbprod/tree-sitter-gitcommit) | MIT | © 2022-present GBProd (Gilles Roustan) |
| d | [gdamore/tree-sitter-d](https://github.com/gdamore/tree-sitter-d) | MIT | gdamore and contributors |
| gleam | [gleam-lang/tree-sitter-gleam](https://github.com/gleam-lang/tree-sitter-gleam) | Apache-2.0 | gleam-lang and contributors |
| earthfile | [glehmann/tree-sitter-earthfile](https://github.com/glehmann/tree-sitter-earthfile) | MIT | © 2024 Gaëtan Lehmann |
| just | [IndianBoy42/tree-sitter-just](https://github.com/IndianBoy42/tree-sitter-just) | Apache-2.0 | IndianBoy42 and contributors |
| fsharp | [ionide/tree-sitter-fsharp](https://github.com/ionide/tree-sitter-fsharp) | MIT | © 2023 Nikolaj Sidorenco |
| pascal | [Isopod/tree-sitter-pascal](https://github.com/Isopod/tree-sitter-pascal) | MIT | © 2018 Benjamin Gray |
| json5 | [Joakker/tree-sitter-json5](https://github.com/Joakker/tree-sitter-json5) | MIT | © 2021 Joaquín Andrés León Ulloa |
| solidity | [JoranHonig/tree-sitter-solidity](https://github.com/JoranHonig/tree-sitter-solidity) | MIT | © 2020 Joran Honig |
| vhdl | [jpt13653903/tree-sitter-vhdl](https://github.com/jpt13653903/tree-sitter-vhdl) | MIT | © 2024 John-Philip Taylor |
| ini | [justinmk/tree-sitter-ini](https://github.com/justinmk/tree-sitter-ini) | Apache-2.0 | justinmk and contributors |
| latex | [latex-lsp/tree-sitter-latex](https://github.com/latex-lsp/tree-sitter-latex) | MIT | © 2021 Patrick Förster |
| groovy | [murtaza64/tree-sitter-groovy](https://github.com/murtaza64/tree-sitter-groovy) | MIT | © 2024 Murtaza Javaid |
| nix | [nix-community/tree-sitter-nix](https://github.com/nix-community/tree-sitter-nix) | MIT | © 2019 Charles Strahan |
| nu | [nushell/tree-sitter-nu](https://github.com/nushell/tree-sitter-nu) | MIT | © 2019 - 2022 The Nushell Project Developers |
| nginx | [opa-oz/tree-sitter-nginx](https://github.com/opa-oz/tree-sitter-nginx) | MIT | © 2024 Vladimir Levin |
| dotenv | [pnx/tree-sitter-dotenv](https://github.com/pnx/tree-sitter-dotenv) | MIT | © 2024 Henrik Hautakoski |
| purescript | [postsolar/tree-sitter-purescript](https://github.com/postsolar/tree-sitter-purescript) | MIT | © 2014 Max Brunsfeld |
| gdscript | [PrestonKnopp/tree-sitter-gdscript](https://github.com/PrestonKnopp/tree-sitter-gdscript) | MIT | © 2016 Max Brunsfeld |
| apacheconf | [prigaux/tree-sitter-apacheconf](https://github.com/prigaux/tree-sitter-apacheconf) | MIT (per `package.json`; no LICENSE file) | prigaux and contributors |
| r | [r-lib/tree-sitter-r](https://github.com/r-lib/tree-sitter-r) | MIT | © 2025 tree-sitter-r authors |
| fish | [ram02z/tree-sitter-fish](https://github.com/ram02z/tree-sitter-fish) | Unlicense | ram02z and contributors |
| rescript | [rescript-lang/tree-sitter-rescript](https://github.com/rescript-lang/tree-sitter-rescript) | MIT | © 2021 Victor Nakoryakov |
| http | [rest-nvim/tree-sitter-http](https://github.com/rest-nvim/tree-sitter-http) | MIT | © 2021 NTBBloodbath |
| asm | [RubixDev/tree-sitter-asm](https://github.com/RubixDev/tree-sitter-asm) | MIT | © 2023 RubixDev |
| gitignore | [shunsambongi/tree-sitter-gitignore](https://github.com/shunsambongi/tree-sitter-gitignore) | MIT | © 2022 shunsambongi |
| clojure | [sogaiu/tree-sitter-clojure](https://github.com/sogaiu/tree-sitter-clojure) | CC0-1.0 | sogaiu and contributors |
| janet | [sogaiu/tree-sitter-janet-simple](https://github.com/sogaiu/tree-sitter-janet-simple) | CC0-1.0 | sogaiu and contributors |
| jsonnet | [sourcegraph/tree-sitter-jsonnet](https://github.com/sourcegraph/tree-sitter-jsonnet) | MIT | © 2022 Sourcegraph |
| fortran | [stadelmanma/tree-sitter-fortran](https://github.com/stadelmanma/tree-sitter-fortran) | MIT | stadelmanma and contributors |
| rst | [stsewd/tree-sitter-rst](https://github.com/stsewd/tree-sitter-rst) | MIT | © 2020 Santos Gallegos |
| org | [SuperFes/tree-sitter-ned-org](https://github.com/SuperFes/tree-sitter-ned-org) (fork of [nvim-orgmode/tree-sitter-org](https://github.com/nvim-orgmode/tree-sitter-org)) | MIT | © 2021-2022 Emilia Simmons |
| gitconfig | [the-mikedavis/tree-sitter-git-config](https://github.com/the-mikedavis/tree-sitter-git-config) | MIT | © 2022 Michael Davis |
| gitrebase | [the-mikedavis/tree-sitter-git-rebase](https://github.com/the-mikedavis/tree-sitter-git-rebase) | MIT | © 2021 Michael Davis |
| commonlisp | [theHamsta/tree-sitter-commonlisp](https://github.com/theHamsta/tree-sitter-commonlisp) | MIT | © 2021 Stephan Seitz |
| fennel | [TravonteD/tree-sitter-fennel](https://github.com/TravonteD/tree-sitter-fennel) | MIT | TravonteD and contributors |
| csv, psv, tsv | [tree-sitter-grammars/tree-sitter-csv](https://github.com/tree-sitter-grammars/tree-sitter-csv) | MIT | tree-sitter-grammars and contributors |
| cuda | [tree-sitter-grammars/tree-sitter-cuda](https://github.com/tree-sitter-grammars/tree-sitter-cuda) | MIT | © 2014 Max Brunsfield |
| diff | [tree-sitter-grammars/tree-sitter-diff](https://github.com/tree-sitter-grammars/tree-sitter-diff) | MIT | © 2021 Michael Davis |
| gitattributes | [tree-sitter-grammars/tree-sitter-gitattributes](https://github.com/tree-sitter-grammars/tree-sitter-gitattributes) | MIT | © 2022 ObserverOfTime |
| glsl | [tree-sitter-grammars/tree-sitter-glsl](https://github.com/tree-sitter-grammars/tree-sitter-glsl) | MIT | © 2014 Max Brunsfield |
| hcl | [tree-sitter-grammars/tree-sitter-hcl](https://github.com/tree-sitter-grammars/tree-sitter-hcl) | Apache-2.0 | tree-sitter-grammars and contributors |
| hlsl | [tree-sitter-grammars/tree-sitter-hlsl](https://github.com/tree-sitter-grammars/tree-sitter-hlsl) | MIT | © 2014 Max Brunsfield |
| kdl | [tree-sitter-grammars/tree-sitter-kdl](https://github.com/tree-sitter-grammars/tree-sitter-kdl) | MIT | tree-sitter-grammars and contributors |
| lua | [tree-sitter-grammars/tree-sitter-lua](https://github.com/tree-sitter-grammars/tree-sitter-lua) | MIT | © 2021 Munif Tanjim |
| make | [tree-sitter-grammars/tree-sitter-make](https://github.com/tree-sitter-grammars/tree-sitter-make) | MIT | © 2021 Alexandre A. Muller |
| markdown, markdown-inline | [tree-sitter-grammars/tree-sitter-markdown](https://github.com/tree-sitter-grammars/tree-sitter-markdown) | MIT | © 2021 Matthias Deiml |
| meson | [tree-sitter-grammars/tree-sitter-meson](https://github.com/tree-sitter-grammars/tree-sitter-meson) | MIT | © 2023 Decodertalkers |
| objc | [tree-sitter-grammars/tree-sitter-objc](https://github.com/tree-sitter-grammars/tree-sitter-objc) | MIT | tree-sitter-grammars and contributors |
| odin | [tree-sitter-grammars/tree-sitter-odin](https://github.com/tree-sitter-grammars/tree-sitter-odin) | MIT | tree-sitter-grammars and contributors |
| pem | [tree-sitter-grammars/tree-sitter-pem](https://github.com/tree-sitter-grammars/tree-sitter-pem) | MIT | © 2022 ObserverOfTime |
| properties | [tree-sitter-grammars/tree-sitter-properties](https://github.com/tree-sitter-grammars/tree-sitter-properties) | MIT | © 2023 ObserverOfTime |
| requirements | [tree-sitter-grammars/tree-sitter-requirements](https://github.com/tree-sitter-grammars/tree-sitter-requirements) | MIT | © 2022 ObserverOfTime |
| ron | [tree-sitter-grammars/tree-sitter-ron](https://github.com/tree-sitter-grammars/tree-sitter-ron) | MIT OR Apache-2.0 | tree-sitter-grammars and contributors |
| scss | [tree-sitter-grammars/tree-sitter-scss](https://github.com/tree-sitter-grammars/tree-sitter-scss) | MIT | tree-sitter-grammars and contributors |
| ssh_config | [tree-sitter-grammars/tree-sitter-ssh-config](https://github.com/tree-sitter-grammars/tree-sitter-ssh-config) | MIT | © 2023 ObserverOfTime |
| starlark | [tree-sitter-grammars/tree-sitter-starlark](https://github.com/tree-sitter-grammars/tree-sitter-starlark) | MIT | tree-sitter-grammars and contributors |
| svelte | [tree-sitter-grammars/tree-sitter-svelte](https://github.com/tree-sitter-grammars/tree-sitter-svelte) | MIT | tree-sitter-grammars and contributors |
| tcl | [tree-sitter-grammars/tree-sitter-tcl](https://github.com/tree-sitter-grammars/tree-sitter-tcl) | MIT | © 2022 Lewis Russell |
| thrift | [tree-sitter-grammars/tree-sitter-thrift](https://github.com/tree-sitter-grammars/tree-sitter-thrift) | MIT | tree-sitter-grammars and contributors |
| toml | [tree-sitter-grammars/tree-sitter-toml](https://github.com/tree-sitter-grammars/tree-sitter-toml) | MIT | tree-sitter-grammars and contributors |
| udev | [tree-sitter-grammars/tree-sitter-udev](https://github.com/tree-sitter-grammars/tree-sitter-udev) | MIT | © 2023 ObserverOfTime |
| vue | [tree-sitter-grammars/tree-sitter-vue](https://github.com/tree-sitter-grammars/tree-sitter-vue) | MIT | tree-sitter-grammars and contributors |
| wgsl | [tree-sitter-grammars/tree-sitter-wgsl-bevy](https://github.com/tree-sitter-grammars/tree-sitter-wgsl-bevy) | MIT | © 2022-2023 Stephan Seitz |
| xml | [tree-sitter-grammars/tree-sitter-xml](https://github.com/tree-sitter-grammars/tree-sitter-xml) | MIT | © 2023 ObserverOfTime |
| yaml | [tree-sitter-grammars/tree-sitter-yaml](https://github.com/tree-sitter-grammars/tree-sitter-yaml) | MIT | © 2024 tree-sitter-grammars contributors |
| perl | [tree-sitter-perl/tree-sitter-perl](https://github.com/tree-sitter-perl/tree-sitter-perl) | MIT | © 2025 Avishai "Veesh" Goldman |
| bash | [tree-sitter/tree-sitter-bash](https://github.com/tree-sitter/tree-sitter-bash) | MIT | © 2017 Max Brunsfeld |
| c | [tree-sitter/tree-sitter-c](https://github.com/tree-sitter/tree-sitter-c) | MIT | © 2014 Max Brunsfeld |
| csharp | [tree-sitter/tree-sitter-c-sharp](https://github.com/tree-sitter/tree-sitter-c-sharp) | MIT | © 2014-2023 Max Brunsfeld, Damien Guard, Amaan Qureshi, and contributors |
| cpp | [tree-sitter/tree-sitter-cpp](https://github.com/tree-sitter/tree-sitter-cpp) | MIT | © 2014 Max Brunsfeld |
| css | [tree-sitter/tree-sitter-css](https://github.com/tree-sitter/tree-sitter-css) | MIT | © 2018 Max Brunsfeld |
| go | [tree-sitter/tree-sitter-go](https://github.com/tree-sitter/tree-sitter-go) | MIT | © 2014 Max Brunsfeld |
| haskell | [tree-sitter/tree-sitter-haskell](https://github.com/tree-sitter/tree-sitter-haskell) | MIT | © 2014 Max Brunsfeld |
| html | [tree-sitter/tree-sitter-html](https://github.com/tree-sitter/tree-sitter-html) | MIT | © 2014 Max Brunsfeld |
| java | [tree-sitter/tree-sitter-java](https://github.com/tree-sitter/tree-sitter-java) | MIT | © 2017 Ayman Nadeem |
| javascript | [tree-sitter/tree-sitter-javascript](https://github.com/tree-sitter/tree-sitter-javascript) | MIT | © 2014 Max Brunsfeld |
| json | [tree-sitter/tree-sitter-json](https://github.com/tree-sitter/tree-sitter-json) | MIT | © 2014 Max Brunsfeld |
| julia | [tree-sitter/tree-sitter-julia](https://github.com/tree-sitter/tree-sitter-julia) | MIT | © 2018 Max Brunsfeld, GitHub |
| ocaml, ocaml-interface | [tree-sitter/tree-sitter-ocaml](https://github.com/tree-sitter/tree-sitter-ocaml) | MIT | © 2020 Max Brunsfeld and Pieter Goetschalckx |
| php | [tree-sitter/tree-sitter-php](https://github.com/tree-sitter/tree-sitter-php) | MIT | © 2017 Josh Vera, GitHub |
| python | [tree-sitter/tree-sitter-python](https://github.com/tree-sitter/tree-sitter-python) | MIT | © 2016 Max Brunsfeld |
| ruby | [tree-sitter/tree-sitter-ruby](https://github.com/tree-sitter/tree-sitter-ruby) | MIT | © 2016 Rob Rix |
| rust | [tree-sitter/tree-sitter-rust](https://github.com/tree-sitter/tree-sitter-rust) | MIT | © 2017 Maxim Sokolov |
| scala | [tree-sitter/tree-sitter-scala](https://github.com/tree-sitter/tree-sitter-scala) | MIT | © 2018 Max Brunsfeld and GitHub |
| tsx, typescript | [tree-sitter/tree-sitter-typescript](https://github.com/tree-sitter/tree-sitter-typescript) | MIT | © 2017 Max Brunsfeld |
| verilog | [tree-sitter/tree-sitter-verilog](https://github.com/tree-sitter/tree-sitter-verilog) | MIT | © 2018-2023 Aliaksei Chapyzhenka |
| typst | [uben0/tree-sitter-typst](https://github.com/uben0/tree-sitter-typst) | MIT | © 2023 Gerbais-Nief Eddie |
| dart | [UserNobody14/tree-sitter-dart](https://github.com/UserNobody14/tree-sitter-dart) | MIT | © 2020-2023 UserNobody14 and others |
| cmake | [uyha/tree-sitter-cmake](https://github.com/uyha/tree-sitter-cmake) | MIT | © 2025 Uy Ha |
| vala | [vala-lang/tree-sitter-vala](https://github.com/vala-lang/tree-sitter-vala) | LGPL-2.1 | vala-lang and contributors |
| desktop | [ValdezFOmar/tree-sitter-desktop](https://github.com/ValdezFOmar/tree-sitter-desktop) | MIT | © 2024 Omar Valdez |
| editorconfig | [ValdezFOmar/tree-sitter-editorconfig](https://github.com/ValdezFOmar/tree-sitter-editorconfig) | MIT | © 2024 Omar Valdez |
| astro | [virchau13/tree-sitter-astro](https://github.com/virchau13/tree-sitter-astro) | MIT | © 2022 Vir Chaudhury |
| v | [vlang/v-analyzer](https://github.com/vlang/v-analyzer) | MIT | © 2023 V Open Source Community Association (VOSCA) |
| erlang | [WhatsApp/tree-sitter-erlang](https://github.com/WhatsApp/tree-sitter-erlang) | Apache-2.0 | WhatsApp and contributors |

Some ned-authored query files vendor or adapt queries from
[nvim-treesitter](https://github.com/nvim-treesitter/nvim-treesitter)
(Apache-2.0, © nvim-treesitter contributors): the C, C++ and Clojure
`highlights.janet`, and Kotlin's upstream highlights, which are themselves based
on it. Each file's header names its source.

Full license texts for the short/permissive licenses above (MIT, BSD-2-Clause,
BSD-3-Clause, CC0, Unlicense, Boost-1.0) are reproduced in each project's own repository at the paths
linked; the Apache License 2.0, MPL-2.0 and LGPL/GPL texts are the standard,
unmodified upstream versions, available at
<https://www.apache.org/licenses/LICENSE-2.0>,
<https://www.mozilla.org/MPL/2.0/> and
<https://www.gnu.org/licenses/>.

## Unicode License v3

Applies to the Unicode Character Database data above; reproduced as its
license requires.

```
COPYRIGHT AND PERMISSION NOTICE

Copyright © 1991-2026 Unicode, Inc.

NOTICE TO USER: Carefully read the following legal agreement. BY
DOWNLOADING, INSTALLING, COPYING OR OTHERWISE USING DATA FILES, AND/OR
SOFTWARE, YOU UNEQUIVOCALLY ACCEPT, AND AGREE TO BE BOUND BY, ALL OF THE
TERMS AND CONDITIONS OF THIS AGREEMENT. IF YOU DO NOT AGREE, DO NOT
DOWNLOAD, INSTALL, COPY, DISTRIBUTE OR USE THE DATA FILES OR SOFTWARE.

Permission is hereby granted, free of charge, to any person obtaining a
copy of data files and any associated documentation (the "Data Files") or
software and any associated documentation (the "Software") to deal in the
Data Files or Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, and/or sell
copies of the Data Files or Software, and to permit persons to whom the
Data Files or Software are furnished to do so, provided that either (a)
this copyright and permission notice appear with all copies of the Data
Files or Software, or (b) this copyright and permission notice appear in
associated Documentation.

THE DATA FILES AND SOFTWARE ARE PROVIDED "AS IS", WITHOUT WARRANTY OF ANY
KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF
THIRD PARTY RIGHTS.

IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS INCLUDED IN THIS NOTICE
BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT OR CONSEQUENTIAL DAMAGES,
OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THE DATA
FILES OR SOFTWARE.

Except as contained in this notice, the name of a copyright holder shall
not be used in advertising or otherwise to promote the sale, use or other
dealings in these Data Files or Software without prior written
authorization of the copyright holder.
```
