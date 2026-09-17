//
// file-naming-conventions follow-up (Docs/FormattingCapabilities.md's B1
// "File naming conventions" -- explicitly NOT one of the nine rule kinds:
// this is about a NEW file's own basename, never about reformatting an
// existing file's content, so none of it goes through the
// (tree, text, range, config) -> edits pass-table shape the rule kinds
// share. Two independent things, both cheap pure-string work over a path,
// no parsing:
//
//  - A per-language case convention for a new file's basename (reuses
//    CaseConvention/MatchesCaseConvention/SuggestNameForConvention wholesale
//    -- FormatCase.h's own naming-convention machinery, just applied to a
//    filename instead of an identifier). A CHECKER only, never a renamer --
//    same "never automatic" stance FormatCase.h's own header comment
//    argues for identifiers, doubly true for a file that may already be
//    open elsewhere (a terminal, a VCS index, another editor).
//  - A per-language header-guard MACRO NAME template
//    ("${PROJECT_NAME}_${FILE_NAME}_${EXT}", the literal example
//    ROADMAP.md's own Configurable Formatter section gives) -- cpp/c get a
//    built-in default; BufferView's own new-header-file path (gated by
//    ned/set-auto-header-guard, default off, mirroring
//    ned/set-auto-format-on-save's own opt-in stance) wraps whatever this
//    expands to in the actual #ifndef/#define/#endif skeleton.
//

#ifndef NED_EDITOR_FILENAMING_H
#define NED_EDITOR_FILENAMING_H

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "FormatRules.h"

namespace ned::editor {

struct FileNamingRuleValue {
    std::optional<CaseConvention> caseConvention;
    // nullopt means "no guard template configured AND no built-in default
    // for this language" (most languages); an explicit empty string (via
    // SetHeaderGuardTemplate(lang, "")) means "explicitly turned off for a
    // language that DOES have a built-in default" -- FileNamingRuleFor's
    // own doc comment below covers the three-state precedence this needs.
    std::optional<std::string> headerGuardTemplate;
};

void SetFileNamingCaseConvention(const std::string& language, std::optional<CaseConvention> value);
void SetHeaderGuardTemplate(const std::string& language, std::optional<std::string> value);

// Whether creating a new header file (BufferView::CommitTextEntryPrompt's
// own isNewFile branch, gated further on the new path's extension
// classifying as a header per Editor/HeaderSource.h::IsHeaderExtension)
// auto-populates it with the expanded guard skeleton. Process-wide,
// mutex-guarded static state, mirroring AutoFormatOnSave.h's exact pattern
// and its same "default off" reasoning: auto-inserting content into a
// brand-new file is a bigger behavior change than trimming/appending
// whitespace, so it stays opt-in. Configured from Janet via
// ned/set-auto-header-guard.
void               SetAutoHeaderGuard(bool enabled);
[[nodiscard]] bool AutoHeaderGuardEnabled();

// Exact language-name lookup (e.g. "cpp") -- no cross-language fallback the
// way CaseRuleFor's own entity-kind key has one (SpaceRuleFor/BreakRuleFor/
// etc.'s "<language>/<name>" scoping doesn't apply here: language IS the
// key, not something a name is scoped BY). `headerGuardTemplate` layers
// three states: an explicit SetHeaderGuardTemplate(lang, template) value
// wins outright; with nothing ever configured, "cpp"/"c" fall back to this
// file's own built-in default (kDefaultHeaderGuardTemplate in
// FileNaming.cpp) and every other language gets nullopt; an explicit
// SetHeaderGuardTemplate(lang, "") (empty string, not nullopt) turns a
// built-in default OFF for that language without needing to know what the
// default even was.
[[nodiscard]] FileNamingRuleValue FileNamingRuleFor(std::string_view language);

// Renders `templateText`'s ${PROJECT_NAME}/${FILE_NAME}/${EXT} placeholders
// against `path` -- PROJECT_NAME is ProjectRoot()'s own basename (empty
// ProjectRoot() falls back to "PROJECT", the same "never emit a truncated
// macro name" stance SuggestNameForConvention takes for an unusable input),
// FILE_NAME is path's stem, EXT is path's extension with its leading dot
// stripped. Each substituted piece is uppercased and every run of
// characters that isn't `[A-Za-z0-9_]` collapsed to a single '_' first (a
// project or file name with a '-', a '.', or a space in it still needs to
// come out as one valid preprocessor identifier), matching the same
// "curated, not exhaustive" tolerance FormatCase.h's own tokenizer
// documents for a real-world name. A `templateText` with no recognized
// placeholder at all is returned through the same sanitizing pass but
// otherwise unchanged -- a custom caller-supplied template's own literal
// text is trusted content, not re-validated against the placeholder set.
[[nodiscard]] std::string ExpandHeaderGuardTemplate(std::string_view templateText, const std::filesystem::path& path);

} // namespace ned::editor

#endif // NED_EDITOR_FILENAMING_H
