//
// prose-checking follow-up. Spell/grammar checking wired in as a second,
// independent LSP diagnostics channel attached alongside a buffer's primary
// language server (see LspManager.h's kProseLanguageKey) -- not a hunspell
// integration, harper-ls speaks LSP natively and already knows how to
// extract comments/strings per real language, so it's just another server
// LspManager talks to.
//
// Deliberately its own file rather than folded into LspServerConfig.h:
// that file's header comment states "nothing is bundled and nothing is
// ever installed/updated by ned itself... deliberately not
// auto-detected/auto-fetched either" as a considered design principle for
// the general per-language server table. harper-ls is a documented,
// narrow exception to that principle -- auto-wired when it's on $PATH so
// prose checking works with zero configuration -- and keeping the
// exception in its own file keeps LspServerConfig.h's own claim about
// itself true.
//
// Falls back through: an explicit user override, else auto-detection, else
// "no prose checking" -- never a hard failure. A user who wants a different
// LSP-speaking checker (ltex-ls, say) points SetProseCheckerCommand at it
// directly; ned never bundles or requires a JVM for that case.
//

#ifndef NED_EDITOR_LSP_PROSECHECKER_H
#define NED_EDITOR_LSP_PROSECHECKER_H

#include <optional>
#include <string>
#include <vector>

namespace ned::editor::lsp {

// Same argv shape/empty-clears-the-override convention as
// SetLspServerCommand: argv[0] the executable, remaining elements its
// arguments. An empty argv clears any explicit override, reverting to
// auto-detection rather than to "disabled" -- ProseCheckingEnabled is the
// actual on/off switch, mirroring SetLspAutoCompleteEnabled's own separate
// enabled-flag shape elsewhere in this subsystem.
void SetProseCheckerCommand(std::vector<std::string> argv);

// Resolution order: ProseCheckingEnabled() == false always yields
// std::nullopt outright (the hard kill switch, checked first). Otherwise:
// an explicit override from SetProseCheckerCommand if one is set, else
// auto-detection of "harper-ls" on $PATH (memoized after the first call --
// ResolveExecutable does a real filesystem scan over $PATH and shouldn't
// repeat every frame the way LspManager::SyncBuffer's caller runs), else
// std::nullopt.
[[nodiscard]] std::optional<std::vector<std::string>> ProseCheckerCommand();

// Default true. The real user-facing on/off switch for prose checking as a
// whole -- also what keeps unit tests hermetic against whatever happens to
// be installed on the machine running them (see
// Tests/ProseCheckerTestGuard.cpp, which forces this false for the whole
// ned_tests binary).
void               SetProseCheckingEnabled(bool enabled);
[[nodiscard]] bool ProseCheckingEnabled();

} // namespace ned::editor::lsp

#endif // NED_EDITOR_LSP_PROSECHECKER_H
