#include "Scanners.h"

#include <utility>

// One entry per ported scanner: the language name it registers under and
// the namespace its file defines `kScanner` in.
#define NED_BUNDLED_SCANNERS(X)           \
    X("bash", bash)                       \
    X("csharp", csharp)                   \
    X("cmake", cmake)                     \
    X("cpp", cpp)                         \
    X("css", css)                         \
    X("dockerfile", dockerfile)           \
    X("fish", fish)                       \
    X("gitcommit", gitcommit)             \
    X("hcl", hcl)                         \
    X("html", html)                       \
    X("janet", janet)                     \
    X("javascript", javascript)           \
    X("kotlin", kotlin)                   \
    X("lua", lua)                         \
    X("markdown", markdown)               \
    X("markdown-inline", markdown_inline) \
    X("nix", nix)                         \
    X("org", org)                         \
    X("php", php)                         \
    X("python", python)                   \
    X("r", r)                             \
    X("ruby", ruby)                       \
    X("rust", rust)                       \
    X("sql", sql)                         \
    X("toml", toml)                       \
    X("tsx", tsx)                         \
    X("typescript", typescript)           \
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
