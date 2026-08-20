//
// Adapts a Janet-defined VCS plugin onto editor::vcs::VcsProvider. See
// ned/vcs-register-provider (EditorBindings.cpp) for how one of these
// gets constructed and registered.
//

#ifndef NED_JANET_JANETVCSPROVIDER_H
#define NED_JANET_JANETVCSPROVIDER_H

#include <janet.h>

#include <string>

#include "Editor/Vcs/VcsProvider.h"

namespace ned::janet {

// A plugin supplies five callbacks (detect, blame-argv, parse-blame,
// log-argv, parse-log), each bound into env under a generated name via
// janet_def and invoked later through janet_dostring -- the same
// "janet_def + janet_dostring, never RootedValue + janet_pcall" pattern
// NedRegisterCommand (EditorBindings.cpp) already established, for the
// same reason: this Janet build (1.32.1) corrupts state when a rooted
// value is later invoked via janet_pcall (see Value.h's RootedValue
// CAUTION comment).
//
// Every method here is a synchronous janet_dostring call and must only
// ever run on the thread that owns env (the main/UI thread) -- see
// VcsProvider.h's header comment for why VcsRunner (Editor/Vcs/VcsRunner.h)
// is careful to call these only before/after, never during, a background
// subprocess wait.
class JanetVcsProvider : public editor::vcs::VcsProvider {
  public:
    JanetVcsProvider(JanetTable* env, std::string name, Janet detectFn, Janet blameArgvFn, Janet parseBlameFn,
                      Janet logArgvFn, Janet parseLogFn);

    [[nodiscard]] bool Detect(const std::filesystem::path& root) const override;

    [[nodiscard]] editor::vcs::VcsCommandSpec BlameArgv(const std::filesystem::path& path) const override;
    [[nodiscard]] std::vector<editor::vcs::VcsBlameLine> ParseBlame(const std::string& stdout_) const override;

    [[nodiscard]] editor::vcs::VcsCommandSpec LogArgv(const std::filesystem::path& path) const override;
    [[nodiscard]] std::vector<editor::vcs::VcsLogEntry> ParseLog(const std::string& stdout_) const override;

  private:
    // Binds arg as a global Janet string under a generated temp name and
    // invokes "(fnInternalName tempName)" via janet_dostring, returning the
    // raw result. Binding the *value* (rather than splicing arg as escaped
    // source text into the call form) sidesteps needing any string-literal
    // escaping at all -- arg can contain quotes/backslashes/anything else
    // a real file path might, with no special-casing required.
    [[nodiscard]] Janet CallWithString(const std::string& fnInternalName, const std::string& arg) const;

    JanetTable* env_;
    std::string name_;
    std::string detectName_;
    std::string blameArgvName_;
    std::string parseBlameName_;
    std::string logArgvName_;
    std::string parseLogName_;
};

} // namespace ned::janet

#endif // NED_JANET_JANETVCSPROVIDER_H
