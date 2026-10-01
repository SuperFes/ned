//
// Marshalling between Janet values and C++ types. FromJanet throws
// std::runtime_error (never janet_panic directly) on a type mismatch -- see
// Environment.h for why that distinction matters.
//

#ifndef NED_JANET_VALUE_H
#define NED_JANET_VALUE_H

#include <janet.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ned::janet {

template <typename T>
T FromJanet(Janet value);

// LSP client follow-up (ned/set-lsp-command's argv parameter): accepts
// either a Janet array (@[...]) or tuple ([...]) of strings, via
// janet_indexed_view's own polymorphic-over-both-types accessor -- the
// first FromJanet specialization in this codebase to unwrap a container
// rather than a scalar.
template <>
std::vector<std::string> FromJanet<std::vector<std::string>>(Janet value);

Janet ToJanet(bool value);
Janet ToJanet(std::int64_t value);
Janet ToJanet(std::size_t value);
Janet ToJanet(double value);
Janet ToJanet(const std::string& value);

// nil when unset -- syntax-theme-overrides follow-up: the "safely nil,
// not an error, for a class/field that simply has nothing configured yet"
// contract the ned/syntax-* Janet getters need (Editor/SyntaxTheme.h's own
// header comment has the full nil-vs-throw rationale).
Janet ToJanet(const std::optional<std::string>& value);
Janet ToJanet(const std::optional<bool>& value);
Janet ToJanet(const std::optional<int>& value);

// A real Janet array of strings -- ned/syntax-classes' own return type
// (syntax-theme-overrides follow-up), the first ToJanet overload in this
// codebase to return a container rather than a scalar.
Janet ToJanet(const std::vector<std::string>& value);

// Identity, so a binding can build and return an arbitrary Janet value.
inline Janet ToJanet(Janet value) {
    return value;
}

// Shared ownership of a Janet value kept alive against Janet's GC
// (janet_gcroot/janet_gcunroot) for as long as any copy of this survives.
// Needed whenever C++ code holds onto a Janet value (e.g. a callback
// function) across native-function calls, since Janet's collector can't see
// C++-side storage on its own.
//
// To call a held function with janet_pcall, start its JanetFiber* out-
// parameter null: pcall reads it as well as writing it, resetting and
// reusing a fiber it finds there.
class RootedValue {
  public:
    explicit RootedValue(Janet value);

    [[nodiscard]] Janet Get() const;

  private:
    struct Root {
        Janet value;
        explicit Root(Janet v);
        ~Root();
        Root(const Root&)            = delete;
        Root& operator=(const Root&) = delete;
    };

    std::shared_ptr<Root> root_;
};

template <>
bool FromJanet<bool>(Janet value);
template <>
std::int64_t FromJanet<std::int64_t>(Janet value);
template <>
std::size_t FromJanet<std::size_t>(Janet value);
template <>
double FromJanet<double>(Janet value);
template <>
std::string FromJanet<std::string>(Janet value);
// Accepts a JANET_FUNCTION or JANET_CFUNCTION; throws otherwise.
template <>
RootedValue FromJanet<RootedValue>(Janet value);
// Identity passthrough (no type check): for native functions that need the
// raw Janet value itself, e.g. to hand to janet_def (see EditorBindings.cpp).
template <>
Janet FromJanet<Janet>(Janet value);

} // namespace ned::janet

#endif // NED_JANET_VALUE_H
