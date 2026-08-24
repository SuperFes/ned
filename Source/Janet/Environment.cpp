#include "Environment.h"

#include "Editor/DiagnosticsLog.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <stdexcept>

#include <unistd.h>

namespace ned::janet {

namespace {

    // Redirects this process's own stderr (fd 2) into a pipe for the
    // duration of a janet_dostring call, so Janet's unbuffered, otherwise-
    // uncapturable stacktrace/compile-error print (see
    // DoStringCapturingStacktrace's own doc comment in Environment.h) lands
    // in a buffer this process controls instead of hitting the live
    // Notcurses-rendered terminal directly -- the same class of corruption
    // ProjectSearch.cpp's RE2 log_errors=false suppression and
    // ChildProcess's own stdout/stderr piping both exist to avoid, just for
    // this process's own fd rather than a child's. Output is drained only
    // after Stop() restores the real fd -- fine for Janet's own stacktrace
    // prints, which are always well under a pipe's default 64KB buffer; if
    // something written during the call itself ever exceeded that, the
    // write would block on the full pipe with nothing draining it, a
    // limitation Janet's own printer never hits in practice. If the
    // pipe/dup setup itself fails, this degrades to a no-op (stderr stays
    // wired to the real fd, matching pre-fix behavior) rather than risking a
    // broken fd 2 for the rest of the process.
    class StderrCapture {
      public:
        StderrCapture() {
            if (pipe(pipeFds_) != 0) {
                return;
            }
            savedStderr_ = dup(STDERR_FILENO);
            if (savedStderr_ < 0) {
                close(pipeFds_[0]);
                close(pipeFds_[1]);
                pipeFds_[0] = pipeFds_[1] = -1;
                return;
            }
            fflush(stderr);
            dup2(pipeFds_[1], STDERR_FILENO);
            active_ = true;
        }

        ~StderrCapture() {
            Stop();
        }

        StderrCapture(const StderrCapture&)            = delete;
        StderrCapture& operator=(const StderrCapture&) = delete;

        // Restores the real stderr fd and drains everything written to the
        // pipe meanwhile into captured_. Safe to call more than once (the
        // destructor calls it too) -- only the first call does anything.
        void Stop() {
            if (!active_) {
                return;
            }
            active_ = false;
            fflush(stderr);
            dup2(savedStderr_, STDERR_FILENO);
            close(savedStderr_);
            close(pipeFds_[1]);

            char    buffer[4096];
            ssize_t bytesRead;
            while ((bytesRead = read(pipeFds_[0], buffer, sizeof(buffer))) > 0) {
                captured_.append(buffer, static_cast<std::size_t>(bytesRead));
            }
            close(pipeFds_[0]);
        }

        [[nodiscard]] const std::string& Captured() const {
            return captured_;
        }

      private:
        int         pipeFds_[2]    = {-1, -1};
        int         savedStderr_   = -1;
        bool        active_        = false;
        std::string captured_;
    };

} // namespace

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
    // janet_dostring independently prints a real stacktrace -- including the
    // "path:line:col:" location *out itself never carries, for either a
    // runtime panic or a compile error -- straight to the process's raw
    // stderr on failure. StderrCapture redirects that print into a pipe for
    // the duration of the call instead of letting it hit the live terminal
    // (raw-stderr-fd-redirect follow-up); *capturedError below prefers that
    // captured text, since it's strictly more informative than *out's bare,
    // location-stripped message.
    StderrCapture capture;
    const int     signal = janet_dostring(env, code.c_str(), sourcePath.c_str(), out);
    capture.Stop();

    if (signal != 0 && capturedError) {
        std::string stderrText = capture.Captured();
        while (!stderrText.empty() && (stderrText.back() == '\n' || stderrText.back() == '\r')) {
            stderrText.pop_back();
        }

        if (!stderrText.empty()) {
            *capturedError = std::move(stderrText);
        }
        else {
            const JanetString description = janet_to_string(*out);
            *capturedError                = std::string(reinterpret_cast<const char*>(description),
                                                          static_cast<std::size_t>(janet_string_length(description)));
            if (capturedError->empty()) {
                *capturedError = "ned: janet error evaluating " + sourcePath + " (no error value captured)";
            }
        }
    }
    return signal;
}

Janet Environment::DoString(const std::string& code, const std::string& sourcePath) {
    Janet       out;
    std::string capturedError;
    const int   signal = DoStringCapturingStacktrace(env_, code, sourcePath, &out, &capturedError);
    if (signal != 0) {
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
