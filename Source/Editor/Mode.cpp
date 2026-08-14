#include "Mode.h"

#include <memory>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "TreeSitter/Languages.h"
#include "TreeSitter/Parser.h"
#include "TreeSitter/Queries.h"
#include "TreeSitter/Query.h"
#include "TreeSitter/Tree.h"

namespace ned::editor {

namespace {

    // The "leaf" tree-sitter/Neovim capture names this project recognizes,
    // mapped to a SyntaxClass -- deliberately not exhaustive (there are
    // dozens more highly-specific ones across the whole grammar ecosystem,
    // e.g. "function.builtin.static"); SyntaxClassForCapture below resolves
    // anything not listed here by walking up to its nearest recognized
    // ancestor via the dotted-name convention itself, not straight to
    // Default. Grouped by SyntaxClass, with every alternate/older spelling a
    // real grammar might use (e.g. bare "conditional"/"repeat" predate the
    // now-standard "keyword.conditional"/"keyword.repeat") listed alongside
    // the canonical one.
    const std::unordered_map<std::string_view, SyntaxClass>& CaptureTable() {
        static const std::unordered_map<std::string_view, SyntaxClass> table = {
            {"comment", SyntaxClass::Comment},
            {"comment.documentation", SyntaxClass::DocComment},

            {"string", SyntaxClass::String},
            {"string.special", SyntaxClass::String},
            {"string.special.key", SyntaxClass::String},
            {"string.regexp", SyntaxClass::String},
            {"string.escape", SyntaxClass::StringEscape},
            {"escape", SyntaxClass::StringEscape},

            {"number", SyntaxClass::Number},
            {"float", SyntaxClass::Number},

            {"keyword", SyntaxClass::Keyword},
            {"keyword.function", SyntaxClass::Keyword},
            {"keyword.operator", SyntaxClass::Operator},
            {"keyword.import", SyntaxClass::Keyword},
            {"keyword.storage", SyntaxClass::Keyword},
            {"keyword.modifier", SyntaxClass::Keyword},
            {"keyword.type", SyntaxClass::Keyword},
            {"keyword.control", SyntaxClass::ControlKeyword},
            {"keyword.control.conditional", SyntaxClass::ControlKeyword},
            {"keyword.control.repeat", SyntaxClass::ControlKeyword},
            {"keyword.control.return", SyntaxClass::ControlKeyword},
            {"keyword.control.import", SyntaxClass::Keyword},
            {"keyword.conditional", SyntaxClass::ControlKeyword},
            {"keyword.repeat", SyntaxClass::ControlKeyword},
            {"keyword.return", SyntaxClass::ControlKeyword},
            {"conditional", SyntaxClass::ControlKeyword},
            {"repeat", SyntaxClass::ControlKeyword},
            {"include", SyntaxClass::Keyword},
            {"preproc", SyntaxClass::Keyword},
            {"label", SyntaxClass::Keyword},

            {"function", SyntaxClass::Function},
            {"function.call", SyntaxClass::Function},
            {"function.method", SyntaxClass::Function},
            {"function.method.call", SyntaxClass::Function},
            {"function.macro", SyntaxClass::Function},
            {"function.builtin", SyntaxClass::FunctionBuiltin},
            {"method", SyntaxClass::Function},
            {"constructor", SyntaxClass::Function},

            {"type", SyntaxClass::Type},
            {"type.definition", SyntaxClass::Type},
            {"type.builtin", SyntaxClass::TypeBuiltin},

            {"constant", SyntaxClass::Constant},
            {"constant.macro", SyntaxClass::Constant},
            {"constant.builtin", SyntaxClass::ConstantBuiltin},
            {"boolean", SyntaxClass::ConstantBuiltin},

            {"variable", SyntaxClass::Variable},
            {"variable.builtin", SyntaxClass::VariableBuiltin},
            {"variable.parameter", SyntaxClass::Parameter},
            {"parameter", SyntaxClass::Parameter},

            {"property", SyntaxClass::Property},
            {"variable.member", SyntaxClass::Property},
            {"field", SyntaxClass::Property},

            {"operator", SyntaxClass::Operator},

            {"punctuation", SyntaxClass::Punctuation},
            {"punctuation.bracket", SyntaxClass::Punctuation},
            {"punctuation.delimiter", SyntaxClass::Punctuation},
            {"punctuation.special", SyntaxClass::Punctuation},
            {"delimiter", SyntaxClass::Punctuation},

            {"tag", SyntaxClass::Tag},
            {"tag.delimiter", SyntaxClass::Punctuation},

            {"attribute", SyntaxClass::Attribute},

            {"module", SyntaxClass::Namespace},
            {"namespace", SyntaxClass::Namespace},
        };
        return table;
    }

    // Maps a tree-sitter query capture name (e.g. "string.special.key",
    // without the leading '@') to a SyntaxClass. A name not found in
    // CaptureTable() verbatim is resolved by repeatedly stripping the last
    // '.'-separated segment and trying again -- tree-sitter/Neovim's own
    // convention is that a capture name forms a dotted hierarchy from most
    // to least specific ("string.special.key" is a kind of "string.special"
    // is a kind of "string"), so an unrecognized specific name should
    // resolve to its nearest recognized ancestor, not straight to Default
    // the way a totally unrelated/unknown name correctly does.
    SyntaxClass SyntaxClassForCapture(std::string_view captureName) {
        const auto& table = CaptureTable();

        while (true) {
            if (const auto it = table.find(captureName); it != table.end()) {
                return it->second;
            }
            const std::size_t dot = captureName.rfind('.');
            if (dot == std::string_view::npos) {
                return SyntaxClass::Default;
            }
            captureName = captureName.substr(0, dot);
        }
    }

} // namespace

Mode FundamentalMode() {
    return Mode{.name = "fundamental-mode", .keymap = Keymap(), .highlight = HighlightFunction()};
}

Mode TreeSitterModeFromLanguage(std::string name, const treesitter::Language& language, std::string_view querySource) {
    const auto parser = std::make_shared<treesitter::Parser>(language);
    const auto query  = std::make_shared<treesitter::Query>(language, querySource);

    // parser/query are captured by shared_ptr, not by value -- Parser/Query
    // are move-only (own a real tree-sitter handle each), but Mode itself
    // needs to stay a plain, freely-copyable value type the way every
    // existing caller (tests, main.cpp) already treats it; a std::function's
    // captured state only needs to be copyable, not the captured objects
    // themselves, so this preserves that contract without Mode having to
    // change shape.
    HighlightFunction highlight = [parser, query](std::string_view bufferText) -> std::vector<HighlightSpan> {
        const treesitter::Tree tree = parser->Parse(bufferText);
        if (tree.IsNull()) {
            return {};
        }

        std::vector<HighlightSpan> spans;
        for (const treesitter::QueryCapture& capture : query->Captures(tree.RootNode())) {
            spans.push_back(HighlightSpan{
                .startByte   = capture.startByte,
                .endByte     = capture.endByte,
                .syntaxClass = SyntaxClassForCapture(capture.name),
            });
        }
        return spans;
    };

    return Mode{.name = std::move(name), .keymap = Keymap(), .highlight = std::move(highlight)};
}

Mode TreeSitterMode(std::string name, std::string_view languageName, const char* querySource) {
    const auto language = treesitter::LanguageByName(languageName);
    // Every languageName this is called with (below) names a grammar
    // Languages.cpp always bundles -- if this ever fires it's a build-time
    // bundling regression (a Mode function calling this with a typo'd or
    // no-longer-bundled name), not a runtime condition to recover from
    // gracefully.
    return TreeSitterModeFromLanguage(std::move(name), *language, querySource);
}

Mode JanetMode() {
    return TreeSitterMode("janet-mode", "janet", treesitter::queries::kJanet);
}

Mode JsonMode() {
    return TreeSitterMode("json-mode", "json", treesitter::queries::kJson);
}

Mode CMode() {
    return TreeSitterMode("c-mode", "c", treesitter::queries::kC);
}

Mode CppMode() {
    return TreeSitterMode("cpp-mode", "cpp", treesitter::queries::kCpp);
}

Mode PhpMode() {
    return TreeSitterMode("php-mode", "php", treesitter::queries::kPhp);
}

Mode JavaScriptMode() {
    return TreeSitterMode("javascript-mode", "javascript", treesitter::queries::kJavaScript);
}

Mode TypeScriptMode() {
    return TreeSitterMode("typescript-mode", "typescript", treesitter::queries::kTypeScript);
}

Mode TsxMode() {
    return TreeSitterMode("tsx-mode", "tsx", treesitter::queries::kTypeScript);
}

Mode HtmlMode() {
    return TreeSitterMode("html-mode", "html", treesitter::queries::kHtml);
}

Mode CssMode() {
    return TreeSitterMode("css-mode", "css", treesitter::queries::kCss);
}

Mode PythonMode() {
    return TreeSitterMode("python-mode", "python", treesitter::queries::kPython);
}

Mode BashMode() {
    return TreeSitterMode("bash-mode", "bash", treesitter::queries::kBash);
}

Mode MarkdownMode() {
    return TreeSitterMode("markdown-mode", "markdown", treesitter::queries::kMarkdown);
}

} // namespace ned::editor
