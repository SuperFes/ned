//
// configurable-formatter follow-up. `<root>/.ned/format.janet` (project) and
// `$XDG_CONFIG_HOME/ned/format.janet` (personal) -- ned's own plain
// Janet-data format-preferences file, read with the same no-VM reader
// (JanetData.h) `language.janet` already uses. Deliberately NOT trust-gated
// (Project/Trust.h): its schema is bool/int/keyword-enum leaves only, with no
// field naming a shell command, executable path, or anything else
// interpretable as code -- see Docs/FormattingRules.md's tripwire note. If
// this schema ever grows such a field, that field (or the whole file) must
// move onto Trust.h's allowlist FIRST -- nothing here enforces that
// automatically.
//
// Layering is a per-field cascade, not a per-file wholesale replace: CLI
// flags (applied by the --format entry point, separately) win over the
// project file, which wins over the personal file, which wins over
// IndentDefaults.h's built-in per-language table. Resolved by applying files
// in reverse-precedence order (personal, then project) and having each
// application read the CURRENT EffectiveIndentStyle for a touched language,
// override only the fields it actually sets, and write the merged result
// back via SetIndentStyleForMode -- so a field a file is silent on always
// falls through to whatever was already in effect, never resets to a
// hardcoded C++ default. trim-trailing-whitespace/ensure-final-newline are
// plain global bools with no sub-fields, so the same "only call the setter
// when the file actually sets the key" rule gives them the identical
// fall-through behavior for free.
//
// Schema (every field optional; an unrecognized key or wrong-typed value is
// a loud path:line error, never silently ignored):
//
//   {:indent {:<language-key> {:tabs true/false :width N} ...}
//    :space  {"<capture>" {:before true/false :after true/false :within true/false} ...}
//    :break  {"<capture>" {:before true/false :after true/false
//                           :placement :same-line/:next-line/:next-line-indented
//                           :collapse-empty true/false :collapse-simple true/false} ...}
//    :blank  {"<capture>" {:min-before N :max-before N} ...}
//    :wrap   {"<capture>" {:policy :never/:always :force-trailing-comma true/false} ...}
//    :align  {"<capture>" {:enabled true/false} ...}
//    :arrange {"<capture>" {:enabled true/false :case-insensitive true/false} ...}
//    :rewrite {"<capture>" {:quote-style :single/:double} ...}
//    :case   {"<entity-kind>" :none/:lowercase/:uppercase/:camel-case/:pascal-case/
//                              :snake-case/:leading-snake-case/:upper-snake-case/
//                              :screaming-snake-case/:lisp-case ...}
//    :trim-trailing-whitespace true/false
//    :ensure-final-newline true/false
//    :max-consecutive-blank-lines N}
//
// The last three map straight onto the Hygiene pass' own settings
// (TrimOnSave.h/FinalNewline.h/MaxConsecutiveBlankLines.h) -- same "only call
// the setter when the file actually sets the key" fall-through rule as
// :indent's fields, no sub-struct needed since none of the three take a
// per-language override.
//
// <language-key> is the same key IndentDefaults.cpp's built-in table and
// ned/set-lsp-command use ("python", "cpp", ...) -- LanguageDefinition::name,
// not the "<name>-mode" Mode name.
//
// :space/:break/:blank follow a different keying convention from :indent --
// configurable-formatter-rules follow-up (Editor/FormatRules.h). Their keys
// are STRINGS, not keywords (a capture name like "control.parens" isn't a
// valid Janet keyword symbol), and each key is either a bare capture name
// (the shared rule every language gets) or "<language>/<capture>" (that
// language's own override) -- FormatRules.h's own flat, language-prefixed
// key convention, mirroring SyntaxTheme.h's SyntaxClassOverrideForCapture
// resolution. There is no nested per-language sub-table: the prefix IS the
// scoping, so applying a :space/:break/:blank entry is one FormatRules
// setter call per field the entry sets, keyed by the string exactly as
// written -- no language/capture split happens in this parser at all.
//

#ifndef NED_EDITOR_FORMATCONFIGPARSE_H
#define NED_EDITOR_FORMATCONFIGPARSE_H

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "FormatRules.h"

namespace ned::editor {

struct FormatConfigIndentEntry {
    std::optional<bool> useTabs;
    std::optional<int>  width;
};

struct FormatConfig {
    std::unordered_map<std::string, FormatConfigIndentEntry> indent; // keyed by language key
    // Keyed by capture name, bare ("control.parens") or language-scoped
    // ("cpp/control.parens") -- FormatRules.h's own key convention, applied
    // verbatim with no parsing of the "/" here. SpaceRuleValue/BreakRuleValue
    // reused directly rather than a parser-local entry struct: both are
    // already "every field optional" structs shaped exactly like what the
    // schema accepts.
    std::unordered_map<std::string, SpaceRuleValue>   space;
    std::unordered_map<std::string, BreakRuleValue>   breakRules; // "break" is a C++ keyword
    std::unordered_map<std::string, BlankRuleValue>   blank;
    std::unordered_map<std::string, WrapRuleValue>    wrap;
    std::unordered_map<std::string, AlignRuleValue>   align;
    std::unordered_map<std::string, ArrangeRuleValue> arrange;
    std::unordered_map<std::string, RewriteRuleValue> rewrite;
    // Kind 7 (Case) -- keyed by a bare entity-kind string ("function",
    // "parameter", ...) or its language-scoped form ("cpp/function"),
    // FormatRules.h's own CaseRuleFor key convention (see its header
    // comment for why this is NOT a real query-capture name the way
    // :space/:break/:blank/:wrap's keys are).
    std::unordered_map<std::string, CaseRuleValue> caseRules;
    std::optional<bool>                            trimTrailingWhitespaceOnSave;
    std::optional<bool>                            ensureFinalNewline;
    // A negative value means "no limit", the same sentinel
    // ned/set-max-consecutive-blank-lines uses (MaxConsecutiveBlankLines.h).
    std::optional<int> maxConsecutiveBlankLines;
};

// Parses `source` (format.janet's own content). `path` is used only to
// prefix a thrown error ("path:line: message") -- mirrors LanguageParse.h's
// ParseLanguageDefinition exactly. Throws std::runtime_error on any parse
// or schema error.
[[nodiscard]] FormatConfig ParseFormatConfig(std::string_view source, const std::string& path);

// Applies every field `config` sets, per the cascade rule above -- calls
// only existing setters (IndentStyle.h's SetIndentStyleForMode,
// TrimOnSave.h's SetTrimTrailingWhitespaceOnSave, FinalNewline.h's
// SetEnsureFinalNewline). A field left at nullopt is untouched.
void ApplyFormatConfig(const FormatConfig& config);

// Reads `path`, parses it, and applies it. A missing file is a silent no-op
// -- there's simply no config there. A real parse/schema error propagates as
// std::runtime_error for the caller to report, the same convention as
// ned::janet::LoadInitFile.
void LoadFormatConfigFile(const std::filesystem::path& path);

// $XDG_CONFIG_HOME/ned/format.janet, falling back to $HOME/.config/ned/
// format.janet -- the personal-tier file's own path, resolved independently
// of ned::janet::InitFilePath() (Janet/InitFile.h) so this stays reachable
// from Editor/ code (Commands.cpp's reload-format-config command included)
// without inverting the Editor-depends-on-Text-only / Janet-depends-on-
// Editor layering this codebase otherwise holds throughout. Throws
// std::runtime_error if neither XDG_CONFIG_HOME nor HOME is set, matching
// InitFilePath()'s own behavior.
[[nodiscard]] std::filesystem::path PersonalFormatConfigPath();

// projectRoot / ".ned" / "format.janet" -- the project-tier file's own path,
// named here purely so every caller spells it the same way.
[[nodiscard]] std::filesystem::path ProjectFormatConfigPath(const std::filesystem::path& projectRoot);

// Every top-level format.janet key, sorted -- the docs-parity counterpart of
// UI/ThemeFile.h's ThemeKeys(): Docs/FormattingRules.md's fenced key
// reference is held against this list in both directions
// (Tests/FormatKeyDocsTest.cpp), so an undocumented key and a stale doc
// entry both fail the build.
[[nodiscard]] std::vector<std::string> FormatConfigKeys();

// Every field name inside one :indent language entry (":python {...}"),
// sorted -- the same parity role, one level down.
[[nodiscard]] std::vector<std::string> FormatConfigIndentEntryKeys();

// Same, one level down inside a :space / :break / :blank entry.
[[nodiscard]] std::vector<std::string> FormatConfigSpaceEntryKeys();
[[nodiscard]] std::vector<std::string> FormatConfigBreakEntryKeys();
[[nodiscard]] std::vector<std::string> FormatConfigBlankEntryKeys();
[[nodiscard]] std::vector<std::string> FormatConfigWrapEntryKeys();
[[nodiscard]] std::vector<std::string> FormatConfigAlignEntryKeys();
[[nodiscard]] std::vector<std::string> FormatConfigArrangeEntryKeys();
[[nodiscard]] std::vector<std::string> FormatConfigRewriteEntryKeys();

} // namespace ned::editor

#endif // NED_EDITOR_FORMATCONFIGPARSE_H
