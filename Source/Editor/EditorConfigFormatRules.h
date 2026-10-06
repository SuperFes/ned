//
// .editorconfig keys that map onto ned's formatter rules (FormatRules.h),
// kept apart from EditorConfig.h so reading .editorconfig doesn't pull in the
// language machinery.
//

#ifndef NED_EDITOR_EDITORCONFIGFORMATRULES_H
#define NED_EDITOR_EDITORCONFIGFORMATRULES_H

#include <filesystem>

namespace ned::editor {

// C#'s formatting keys (Roslyn's csharp_new_line_before_open_brace,
// csharp_new_line_before_else/catch/finally,
// csharp_space_after_keywords_in_control_flow_statements and
// csharp_space_between_parentheses), as the project root's `*.cs` files see
// them, written as csharp-scoped rules into the File layer over C#'s bundled
// style. Nothing while .editorconfig is disabled.
void ApplyEditorConfigFormatRules(const std::filesystem::path& projectRoot);

} // namespace ned::editor

#endif // NED_EDITOR_EDITORCONFIGFORMATRULES_H
