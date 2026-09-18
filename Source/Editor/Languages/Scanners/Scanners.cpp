#include "Scanners.h"

#include <utility>

// One entry per ported scanner: the language name it registers under and
// the namespace its file defines `kScanner` in.
#define NED_BUNDLED_SCANNERS(X)           \
    X("asciidoc", asciidoc)               \
    X("asciidoc-inline", asciidoc_inline) \
    X("astro", astro)                     \
    X("bash", bash)                       \
    X("cmake", cmake)                     \
    X("cpp", cpp)                         \
    X("crystal", crystal)                 \
    X("csharp", csharp)                   \
    X("css", css)                         \
    X("cuda", cuda)                       \
    X("d", d)                             \
    X("dart", dart)                       \
    X("dockerfile", dockerfile)           \
    X("dotenv", dotenv)                   \
    X("earthfile", earthfile)             \
    X("editorconfig", editorconfig)       \
    X("elixir", elixir)                   \
    X("elm", elm)                         \
    X("erlang", erlang)                   \
    X("fish", fish)                       \
    X("fortran", fortran)                 \
    X("fsharp", fsharp)                   \
    X("gdscript", gdscript)               \
    X("gitcommit", gitcommit)             \
    X("gleam", gleam)                     \
    X("haskell", haskell)                 \
    X("hcl", hcl)                         \
    X("hlsl", hlsl)                       \
    X("html", html)                       \
    X("janet", janet)                     \
    X("javascript", javascript)           \
    X("julia", julia)                     \
    X("just", just)                       \
    X("kdl", kdl)                         \
    X("kotlin", kotlin)                   \
    X("latex", latex)                     \
    X("lua", lua)                         \
    X("markdown", markdown)               \
    X("markdown-inline", markdown_inline) \
    X("matlab", matlab)                   \
    X("nim", nim)                         \
    X("nix", nix)                         \
    X("nu", nu)                           \
    X("ocaml", ocaml)                     \
    X("ocaml-interface", ocaml_interface) \
    X("odin", odin)                       \
    X("org", org)                         \
    X("perl", perl)                       \
    X("php", php)                         \
    X("powershell", powershell)           \
    X("properties", properties)           \
    X("purescript", purescript)           \
    X("python", python)                   \
    X("r", r)                             \
    X("racket", racket)                   \
    X("rescript", rescript)               \
    X("ron", ron)                         \
    X("rst", rst)                         \
    X("ruby", ruby)                       \
    X("rust", rust)                       \
    X("scala", scala)                     \
    X("scss", scss)                       \
    X("sql", sql)                         \
    X("starlark", starlark)               \
    X("svelte", svelte)                   \
    X("swift", swift)                     \
    X("tcl", tcl)                         \
    X("toml", toml)                       \
    X("tsx", tsx)                         \
    X("typescript", typescript)           \
    X("typst", typst)                     \
    X("vhdl", vhdl)                       \
    X("vue", vue)                         \
    X("xml", xml)                         \
    X("yaml", yaml)

namespace ned::editor::languages::scanners {

#define NED_DECLARE_SCANNER(name, ns)               \
    namespace ns {                                  \
        extern const parse::ScannerVTable kScanner; \
    }
NED_BUNDLED_SCANNERS(NED_DECLARE_SCANNER)
#undef NED_DECLARE_SCANNER

const parse::ScannerVTable* FindBundledScanner(std::string_view language) {
    static const std::pair<std::string_view, const parse::ScannerVTable*> kScanners[] = {
#define NED_SCANNER_ENTRY(name, ns) {name, &ns::kScanner},
        NED_BUNDLED_SCANNERS(NED_SCANNER_ENTRY)
#undef NED_SCANNER_ENTRY
    };
    for (const auto& [name, scanner] : kScanners)
        if (name == language)
            return scanner;
    return nullptr;
}

} // namespace ned::editor::languages::scanners
