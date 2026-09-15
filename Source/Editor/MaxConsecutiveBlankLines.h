//
// configurable-formatter Hygiene-pass follow-up. The maximum run of
// consecutive blank lines the Hygiene pass (Editor/Format.h) leaves in
// place -- a run longer than this is collapsed down to exactly this many
// (Text/WhitespaceHygiene.h's CollapseBlankLineRuns). Process-wide,
// mutex-guarded static state, mirroring TrimOnSave.h/FinalNewline.h's exact
// pattern -- including the same default-on reasoning: this is a
// deliberately coarse, global rule (the full per-construct-anchored "kind
// 6" blank-line rules from Docs/FormattingCapabilities.md need a
// format.scm per language and are out of scope here), but a generous
// default (2) is common enough across style guides/linters to be a safe,
// low-risk default rather than something that has to be opted into.
// Configured from Janet via ned/set-max-consecutive-blank-lines.
//
// nullopt means disabled (no limit at all) -- CollapseBlankLineRuns' own
// "maxConsecutive < 0" no-op sentinel, so this setting's nullopt state maps
// onto that sentinel without a separate enabled/disabled bool the way
// TrimOnSave.h needs one (that setting has no natural "off" value for an
// int the way -1 reads unambiguously as "unlimited" here).
//

#ifndef NED_EDITOR_MAXCONSECUTIVEBLANKLINES_H
#define NED_EDITOR_MAXCONSECUTIVEBLANKLINES_H

#include <optional>

namespace ned::editor {

void                      SetMaxConsecutiveBlankLines(std::optional<int> max);
[[nodiscard]] std::optional<int> MaxConsecutiveBlankLines();

} // namespace ned::editor

#endif // NED_EDITOR_MAXCONSECUTIVEBLANKLINES_H
