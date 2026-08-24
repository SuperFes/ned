//
// RAII environment-variable override for tests. GitIgnoreMatcher resolves
// the developer machine's real global git configuration (HOME/.gitconfig,
// $XDG_CONFIG_HOME/git/*, $GIT_CONFIG_GLOBAL) whenever a test root carries
// a .git entry -- any test doing that must pin these to a disposable
// location or the suite's results depend on whoever runs it. Restores the
// previous value (or unset-ness) on destruction. Not thread-safe, like the
// environment itself; Catch2 runs test cases serially in one process.
//

#ifndef NED_TESTS_ENVOVERRIDE_H
#define NED_TESTS_ENVOVERRIDE_H

#include <cstdlib>
#include <optional>
#include <string>

namespace ned::tests {

class ScopedEnvOverride {
  public:
    // value == nullptr unsets the variable for the scope.
    ScopedEnvOverride(const char* name, const char* value) : name_(name) {
        if (const char* previous = std::getenv(name)) {
            previous_ = previous;
        }
        if (value != nullptr) {
            ::setenv(name, value, 1);
        }
        else {
            ::unsetenv(name);
        }
    }

    ~ScopedEnvOverride() {
        if (previous_) {
            ::setenv(name_.c_str(), previous_->c_str(), 1);
        }
        else {
            ::unsetenv(name_.c_str());
        }
    }

    ScopedEnvOverride(const ScopedEnvOverride&)            = delete;
    ScopedEnvOverride& operator=(const ScopedEnvOverride&) = delete;

  private:
    std::string                name_;
    std::optional<std::string> previous_;
};

} // namespace ned::tests

#endif // NED_TESTS_ENVOVERRIDE_H
