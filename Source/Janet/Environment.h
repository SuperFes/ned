//
// RAII around the Janet VM lifecycle (janet_init/janet_deinit) plus a single
// environment table, and template-based native-function registration that
// replaces hand-written JanetCFunction shims.
//
// Real applications construct exactly one Environment for the process
// lifetime -- never more than one at a time, even sequentially. Repeated
// janet_init()/janet_deinit() cycles corrupt state in this Janet build (see
// Tests/JanetTestSupport.cpp), so treat the VM as a true once-per-process
// resource, including in tests.
//

#ifndef NED_JANET_ENVIRONMENT_H
#define NED_JANET_ENVIRONMENT_H

#include <janet.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "Value.h"

namespace ned::janet {

namespace detail {

    template <typename T>
    struct FunctionTraits;

    template <typename R, typename... Args>
    struct FunctionTraits<R (*)(Args...)> {
        using ReturnType                   = R;
        using ArgsTuple                    = std::tuple<Args...>;
        static constexpr std::size_t Arity = sizeof...(Args);
    };

    template <auto Fn, typename Traits, std::size_t... I>
    Janet InvokeAndConvert(Janet* argv, std::index_sequence<I...>) {
        // FromJanet may throw (a type mismatch) -- that has to happen in here,
        // inside the caller's try block, so normal C++ unwinding runs before any
        // janet_panicf conversion. See NativeShim below for the full reasoning.
        if constexpr (std::is_void_v<typename Traits::ReturnType>) {
            Fn(FromJanet<std::tuple_element_t<I, typename Traits::ArgsTuple>>(argv[I])...);
            return janet_wrap_nil();
        }
        else {
            return ToJanet(Fn(FromJanet<std::tuple_element_t<I, typename Traits::ArgsTuple>>(argv[I])...));
        }
    }

    // Adapts a plain C++ free function into a JanetCFunction. Fn must be a
    // non-capturing free function pointer (or equivalent), with each parameter
    // type having a FromJanet<T> specialization and the return type (or void)
    // having a ToJanet overload.
    template <auto Fn>
    Janet NativeShim(int32_t argc, Janet* argv) {
        using Traits = FunctionTraits<decltype(Fn)>;

        // Safe to call directly (may janet_panic/longjmp) precisely because
        // nothing with a non-trivial destructor exists on this stack yet.
        janet_fixarity(argc, static_cast<int32_t>(Traits::Arity));

        try {
            return InvokeAndConvert<Fn, Traits>(argv, std::make_index_sequence<Traits::Arity>{});
        }
        catch (const std::exception& e) {
            // Safe here too: the try block's locals have already unwound
            // normally by the time we reach this catch, so nothing but this
            // stack frame itself (no non-trivial destructors) sits between here
            // and Janet's own protection boundary when this longjmps.
            janet_panicf("%s", e.what());
        }
        return janet_wrap_nil(); // unreachable
    }

} // namespace detail

class Environment {
  public:
    Environment();
    ~Environment();

    Environment(const Environment&)            = delete;
    Environment& operator=(const Environment&) = delete;

    // Registers Fn as <prefix>/<name>, e.g. Register<&Foo>("ned", "foo", "...").
    template <auto Fn>
    void Register(const char* prefix, const char* name, const char* docstring) {
        RegisterRaw(prefix, name, docstring, &detail::NativeShim<Fn>);
    }

    // Evaluates code and returns the value of its last top-level form.
    // Throws std::runtime_error on a Janet-level evaluation error --
    // diagnostics-log follow-up: the thrown message, and the
    // ned/*-diagnostics-log entry logged alongside it, now carry Janet's own
    // real captured stacktrace text (see DoStringCapturingStacktrace below)
    // rather than a generic "see stderr for details" placeholder.
    Janet DoString(const std::string& code, const std::string& sourcePath = "eval");
    Janet DoFile(const std::filesystem::path& path);

    [[nodiscard]] JanetTable* Env() const;

    // Self-hosting-completion follow-up. Every symbol name directly def'd in
    // this environment's own table (not walking a proto chain -- every
    // Register<Fn> call lands here via janet_cfuns_prefix, and so does an
    // ordinary top-level (def ...) from loaded Janet source) whose name
    // begins with prefix, sorted. Introspects the live JanetTable via
    // janet_dictionary_next rather than keeping a shadow list of every
    // Register<Fn> call, so it can never drift from what's actually bound --
    // and, called fresh per completion request rather than snapshotted once,
    // stays correct across a hot-reloaded init.janet or a project's own
    // .ned/init.janet trusted after startup.
    [[nodiscard]] std::vector<std::string> BindingNamesWithPrefix(std::string_view prefix) const;

  private:
    void RegisterRaw(const char* prefix, const char* name, const char* docstring, JanetCFunction fn);

    JanetTable* env_;
};

// diagnostics-log follow-up: runs janet_dostring and, on a nonzero return
// signal, stringifies the real error value janet_dostring's own contract
// already leaves in *out (via janet_to_string) into *capturedError, rather
// than the generic "see stderr for details" placeholder every caller used
// to throw instead. Free function, not an Environment method, because
// EditorBindings.cpp's command-invocation path (NedRegisterCommand's
// invocation lambda) only ever holds a raw JanetTable*, not an Environment&
// -- both callers share this rather than each hand-rolling the same
// *out-stringification.
//
// No location (path/line) is ever extractable from the captured text --
// tmux/unit-verified against both a runtime panic and a compile error: Janet
// really does print a real stacktrace/"path:line:col:"-prefixed message
// straight to the process's raw stderr on failure (confirming the original
// "see stderr for details" comment this replaced was accurate), but that
// text is neither reachable through Janet's ":err" dynamic binding (tried
// first; came back empty) nor present in *out, which only ever holds the
// bare message with any location prefix already stripped. A real fix would
// need the raw stderr fd redirected around this call -- out of scope here,
// noted on this feature's own ROADMAP.md entry rather than attempted with
// string-parsing that provably can't work.
int DoStringCapturingStacktrace(JanetTable* env, const std::string& code, const std::string& sourcePath, Janet* out,
                                 std::string* capturedError);

} // namespace ned::janet

#endif // NED_JANET_ENVIRONMENT_H
