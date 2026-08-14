#include "ScriptingSession.h"

#include <stdexcept>

namespace ned::editor {

namespace {
ScriptingSession* g_current = nullptr;
}

ScriptingSessionScope::ScriptingSessionScope(ScriptingSession session) : session_(session) {
    previous_ = g_current;
    g_current = &session_;
}

ScriptingSessionScope::~ScriptingSessionScope() {
    g_current = previous_;
}

ScriptingSession& ScriptingSessionScope::Current() {
    if (!g_current) {
        throw std::runtime_error("ned: no active scripting session");
    }
    return *g_current;
}

CommandContextScope::CommandContextScope(CommandContext& context) {
    ScriptingSession& session = ScriptingSessionScope::Current();
    previous_                 = session.context;
    session.context           = &context;
}

CommandContextScope::~CommandContextScope() {
    ScriptingSessionScope::Current().context = previous_;
}

} // namespace ned::editor
