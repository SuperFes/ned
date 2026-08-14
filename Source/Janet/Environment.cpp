#include "Environment.h"

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

Janet Environment::DoString(const std::string& code, const std::string& sourcePath) {
    Janet     out;
    const int signal = janet_dostring(env_, code.c_str(), sourcePath.c_str(), &out);
    if (signal != 0) {
        throw std::runtime_error("ned: janet error evaluating " + sourcePath + " (see stderr for details)");
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

} // namespace ned::janet
