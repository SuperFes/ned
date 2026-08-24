#include "Environment.h"

#include "Editor/DiagnosticsLog.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace ned::janet {

Environment::Environment() {
    janet_init();
    env_ = janet_core_env(nullptr);
}

Environment::~Environment() {
    janet_deinit();
}

void Environment::RegisterRaw(const char* prefix, const char* name, const char* docstring, JanetCFunction fn) {
    const JanetReg regs[] = {
        {name, fn, docstring},
        {nullptr, nullptr, nullptr},
    };
    janet_cfuns_prefix(env_, prefix, regs);
}

int DoStringCapturingStacktrace(JanetTable* env, const std::string& code, const std::string& sourcePath, Janet* out,
                                 std::string* capturedError) {
    // janet_dostring does independently print a real stacktrace straight to
    // the process's raw stderr on failure (tmux/unit-verified,
    // diagnostics-log follow-up -- confirming the original "see stderr for
    // details" comment this replaced was accurate) but that text is neither
    // capturable through Janet's ":err" dynamic binding (tried first; came
    // back empty every time -- apparently not the mechanism janet_dostring's
    // own default error reporting uses) nor present in *out. *out only ever
    // holds the bare panic/compile-error message with any "path:line:col:"
    // prefix already stripped -- verified for both a runtime panic and a
    // compile error -- so there is no location to extract from it; a real
    // fix would need the raw stderr fd redirected around this call, out of
    // scope here (see this feature's own ROADMAP.md entry). janet_to_string
    // is what turns *out into displayable text.
    const int signal = janet_dostring(env, code.c_str(), sourcePath.c_str(), out);

    if (signal != 0 && capturedError) {
        const JanetString description = janet_to_string(*out);
        *capturedError                = std::string(reinterpret_cast<const char*>(description),
                                                      static_cast<std::size_t>(janet_string_length(description)));
        if (capturedError->empty()) {
            *capturedError = "ned: janet error evaluating " + sourcePath + " (no error value captured)";
        }
    }
    return signal;
}

Janet Environment::DoString(const std::string& code, const std::string& sourcePath) {
    Janet       out;
    std::string capturedError;
    const int   signal = DoStringCapturingStacktrace(env_, code, sourcePath, &out, &capturedError);
    if (signal != 0) {
        // No path/line: see DoStringCapturingStacktrace's own doc comment
        // for why that's never available here, even though sourcePath is
        // known -- the captured text itself never carries a location.
        editor::LogMessage(editor::LogCategory::Janet, editor::LogSeverity::Error, capturedError);
        throw std::runtime_error(capturedError);
    }
    return out;
}

Janet Environment::DoFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("ned: cannot open janet file: " + path.string());
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return DoString(content, path.string());
}

JanetTable* Environment::Env() const {
    return env_;
}

std::vector<std::string> Environment::BindingNamesWithPrefix(std::string_view prefix) const {
    std::vector<std::string> names;
    const JanetKV*           kv = nullptr;
    while ((kv = janet_dictionary_next(env_->data, env_->capacity, kv)) != nullptr) {
        if (!janet_checktype(kv->key, JANET_SYMBOL)) {
            continue;
        }
        const JanetSymbol      symbol = janet_unwrap_symbol(kv->key);
        const std::string_view name(reinterpret_cast<const char*>(symbol), janet_string_length(symbol));
        if (name.substr(0, prefix.size()) == prefix) {
            names.emplace_back(name);
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

} // namespace ned::janet
