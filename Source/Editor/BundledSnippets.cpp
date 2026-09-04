#include "Editor/BundledSnippets.h"

#include "Editor/SnippetRegistry.h"

namespace ned::editor {

namespace {

    struct BundledSnippet {
        const char* languageKey;
        const char* trigger;
        const char* body;
    };

    // Language keys match LanguageKeyForMode's own convention (Mode.cpp) --
    // each bundled *Mode() factory's own name with its "-mode" suffix
    // stripped. Data-format/config languages (json/yaml/toml/xml), niche
    // ones (fish/clojure/jank), and Org (its own capture-template system
    // already covers this need, see OrgCapture.h) are deliberately left out
    // -- a snippet doesn't add much over hand-typing a JSON object, and
    // duplicating Org capture would be a second, weaker mechanism for the
    // same job.
    constexpr BundledSnippet kBundledSnippets[] = {
        // --- C ---------------------------------------------------------
        {"c", "main", "int main(int argc, char *argv[]) {\n    $0\n    return 0;\n}"},
        {"c", "for", "for (int ${1:i} = 0; $1 < ${2:n}; ++$1) {\n    $0\n}"},
        {"c", "while", "while (${1:condition}) {\n    $0\n}"},
        {"c", "if", "if (${1:condition}) {\n    $0\n}"},
        {"c", "ifelse", "if (${1:condition}) {\n    $2\n} else {\n    $0\n}"},
        {"c", "fn", "${1:void} ${2:name}($3) {\n    $0\n}"},
        {"c", "struct", "struct ${1:Name} {\n    $0\n};"},
        {"c", "inc", "#include <${1:stdio.h}>"},
        {"c", "printf", "printf(\"${1:%s\\n}\", ${2:value});$0"},
        // Showcases the $TM_FILENAME_BASE variable + /upcase transform --
        // resolved once at expansion time against the buffer's own path
        // (find-file already sets Path() for a not-yet-saved new file), not
        // a live tabstop, so both occurrences always agree.
        {"c", "guard",
         "#ifndef ${TM_FILENAME_BASE/(.*)/${1:/upcase}/}_H\n#define ${TM_FILENAME_BASE/(.*)/${1:/upcase}/}_H\n\n$0\n\n#endif"},

        // --- C++ ---------------------------------------------------------
        {"cpp", "main", "int main(int argc, char *argv[]) {\n    $0\n    return 0;\n}"},
        {"cpp", "for", "for (int ${1:i} = 0; $1 < ${2:n}; ++$1) {\n    $0\n}"},
        {"cpp", "while", "while (${1:condition}) {\n    $0\n}"},
        {"cpp", "if", "if (${1:condition}) {\n    $0\n}"},
        {"cpp", "ifelse", "if (${1:condition}) {\n    $2\n} else {\n    $0\n}"},
        {"cpp", "class", "class ${1:Name} {\n  public:\n    ${1:Name}() = default;\n\n  private:\n    $0\n};"},
        {"cpp", "fn", "${1:void} ${2:name}($3) {\n    $0\n}"},
        {"cpp", "inc", "#include <${1:iostream}>"},
        {"cpp", "cout", "std::cout << ${1:value} << std::endl;$0"},
        {"cpp", "try", "try {\n    $1\n} catch (const ${2:std::exception}& ${3:e}) {\n    $0\n}"},
        {"cpp", "ns", "namespace ${1:name} {\n$0\n} // namespace $1"},
        {"cpp", "guard",
         "#ifndef ${TM_FILENAME_BASE/(.*)/${1:/upcase}/}_H\n#define ${TM_FILENAME_BASE/(.*)/${1:/upcase}/}_H\n\n$0\n\n#endif // "
         "${TM_FILENAME_BASE/(.*)/${1:/upcase}/}_H"},

        // --- Python ------------------------------------------------------
        {"python", "def", "def ${1:name}(${2:args}):\n    $0"},
        {"python", "class", "class ${1:Name}:\n    def __init__(self${2:, args}):\n        $0"},
        {"python", "for", "for ${1:item} in ${2:iterable}:\n    $0"},
        {"python", "while", "while ${1:condition}:\n    $0"},
        {"python", "if", "if ${1:condition}:\n    $0"},
        {"python", "ifelse", "if ${1:condition}:\n    $2\nelse:\n    $0"},
        {"python", "try", "try:\n    $1\nexcept ${2:Exception} as ${3:e}:\n    $0"},
        {"python", "main", "if __name__ == \"__main__\":\n    $0"},
        {"python", "print", "print(${1:value})$0"},
        {"python", "import", "import ${1:module}$0"},

        // --- JavaScript ----------------------------------------------------
        {"javascript", "func", "function ${1:name}(${2:args}) {\n    $0\n}"},
        {"javascript", "arrow", "const ${1:name} = (${2:args}) => {\n    $0\n};"},
        {"javascript", "for", "for (let ${1:i} = 0; $1 < ${2:array}.length; $1++) {\n    $0\n}"},
        {"javascript", "forof", "for (const ${1:item} of ${2:iterable}) {\n    $0\n}"},
        {"javascript", "if", "if (${1:condition}) {\n    $0\n}"},
        {"javascript", "ifelse", "if (${1:condition}) {\n    $2\n} else {\n    $0\n}"},
        {"javascript", "try", "try {\n    $1\n} catch (${2:error}) {\n    $0\n}"},
        {"javascript", "class", "class ${1:Name} {\n    constructor(${2:args}) {\n        $0\n    }\n}"},
        {"javascript", "log", "console.log(${1:value});$0"},
        {"javascript", "import", "import ${1:module} from '${2:package}';$0"},

        // --- TypeScript ----------------------------------------------------
        {"typescript", "func", "function ${1:name}(${2:args}): ${3:void} {\n    $0\n}"},
        {"typescript", "arrow", "const ${1:name} = (${2:args}): ${3:void} => {\n    $0\n};"},
        {"typescript", "interface", "interface ${1:Name} {\n    $0\n}"},
        {"typescript", "for", "for (let ${1:i} = 0; $1 < ${2:array}.length; $1++) {\n    $0\n}"},
        {"typescript", "forof", "for (const ${1:item} of ${2:iterable}) {\n    $0\n}"},
        {"typescript", "if", "if (${1:condition}) {\n    $0\n}"},
        {"typescript", "ifelse", "if (${1:condition}) {\n    $2\n} else {\n    $0\n}"},
        {"typescript", "try", "try {\n    $1\n} catch (${2:error}) {\n    $0\n}"},
        {"typescript", "class", "class ${1:Name} {\n    constructor(${2:args}) {\n        $0\n    }\n}"},
        {"typescript", "log", "console.log(${1:value});$0"},
        {"typescript", "import", "import ${1:module} from '${2:package}';$0"},

        // --- TSX -----------------------------------------------------------
        {"tsx", "rfc", "function ${1:Component}() {\n    return (\n        <div>$0</div>\n    );\n}"},
        {"tsx", "usestate", "const [${1:state}, set${2:State}] = useState(${3:initialValue});$0"},
        {"tsx", "useeffect", "useEffect(() => {\n    $0\n}, [${1:deps}]);"},
        {"tsx", "if", "if (${1:condition}) {\n    $0\n}"},
        {"tsx", "log", "console.log(${1:value});$0"},
        {"tsx", "import", "import ${1:module} from '${2:package}';$0"},

        // --- HTML ----------------------------------------------------------
        {"html", "html5",
         "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n    <meta charset=\"UTF-8\">\n    <title>${1:Document}</title>\n</head>\n<body>\n "
         "   $0\n</body>\n</html>"},
        {"html", "div", "<div class=\"${1:class}\">\n    $0\n</div>"},
        {"html", "a", "<a href=\"${1:#}\">${2:text}</a>$0"},
        {"html", "img", "<img src=\"${1:src}\" alt=\"${2:alt}\">$0"},
        {"html", "link", "<link rel=\"stylesheet\" href=\"${1:style.css}\">$0"},
        {"html", "script", "<script src=\"${1:script.js}\"></script>$0"},

        // --- CSS -------------------------------------------------------------
        {"css", "media", "@media (${1:min-width}: ${2:768px}) {\n    $0\n}"},
        {"css", "flexcenter", "display: flex;\nalign-items: center;\njustify-content: center;$0"},
        {"css", "keyframes", "@keyframes ${1:name} {\n    from {\n        $2\n    }\n    to {\n        $0\n    }\n}"},

        // --- Bash ---------------------------------------------------------
        {"bash", "shebang", "#!/usr/bin/env bash\nset -euo pipefail\n\n$0"},
        {"bash", "for", "for ${1:item} in ${2:list}; do\n    $0\ndone"},
        {"bash", "while", "while ${1:condition}; do\n    $0\ndone"},
        {"bash", "if", "if ${1:[ condition ]}; then\n    $0\nfi"},
        {"bash", "ifelse", "if ${1:[ condition ]}; then\n    $2\nelse\n    $0\nfi"},
        {"bash", "fn", "${1:name}() {\n    $0\n}"},

        // --- Markdown -------------------------------------------------------
        {"markdown", "link", "[${1:text}](${2:url})$0"},
        {"markdown", "img", "![${1:alt}](${2:url})$0"},
        {"markdown", "code", "```${1:language}\n$0\n```"},

        // --- Janet ---------------------------------------------------------
        {"janet", "defn", "(defn ${1:name} [${2:args}]\n  $0)"},
        {"janet", "fn", "(fn [${1:args}]\n  $0)"},
        {"janet", "let", "(let [${1:name} ${2:value}]\n  $0)"},
        {"janet", "for", "(for [${1:i} :range [0 ${2:n}]]\n  $0)"},
        {"janet", "each", "(each ${1:item} ${2:coll}\n  $0)"},
        {"janet", "var", "(var ${1:name} ${2:value})$0"},
    };

} // namespace

void RegisterBundledSnippets() {
    for (const BundledSnippet& entry : kBundledSnippets) {
        RegisterSnippet(entry.languageKey, entry.trigger, entry.body);
    }
}

} // namespace ned::editor
