//
// ned-format argv[0] dispatch follow-up: the pure half of "was ned invoked
// through its ned-format symlink" -- pulled out of main.cpp (which isn't
// linked into ned_tests at all) so this one predicate is unit-testable in
// isolation from CLI11's own wiring there.
//

#ifndef NED_EDITOR_CLIFORMATDISPATCH_H
#define NED_EDITOR_CLIFORMATDISPATCH_H

#include <string_view>

namespace ned::editor {

// True when argv0's own basename is exactly "ned-format" -- a
// std::filesystem::path comparison, so both a bare PATH lookup ("ned-format")
// and a full path to the installed symlink ("/usr/bin/ned-format") resolve
// identically; a path merely containing "ned-format" as a directory
// component (e.g. ".../ned-format/ned") does not match.
[[nodiscard]] bool InvokedAsNedFormat(std::string_view argv0);

// The same test for the `ned-langc` symlink (`ned --compile-language`).
[[nodiscard]] bool InvokedAsNedLangc(std::string_view argv0);

} // namespace ned::editor

#endif // NED_EDITOR_CLIFORMATDISPATCH_H
