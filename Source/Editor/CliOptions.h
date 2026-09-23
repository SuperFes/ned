//
// Every top-level command-line option ned recognizes, registered in one
// place -- pulled out of main.cpp (which isn't linked into ned_tests at all)
// so the generated invocation reference reads the same CLI::App the editor
// itself parses with, rather than a hand-maintained copy of it that would
// drift the moment a flag is added.
//

#ifndef NED_EDITOR_CLIOPTIONS_H
#define NED_EDITOR_CLIOPTIONS_H

#include <string>
#include <vector>

namespace CLI {
class App;
}

namespace ned::editor {

// Where BuildCli binds every option's parsed value. Plain aggregate: main()
// reads it directly after CLI::App::parse, and the argv[0]-symlink dispatch
// writes back into it (`ned-format` sets `format`).
struct CliArgs {
    bool                     lspBroker     = false;
    bool                     lspBrokerStop = false;
    bool                     foreground    = false;
    std::string              mcpStdioRelaySocketPath;
    bool                     format          = false;
    bool                     forceHuge       = false;
    bool                     compileLanguage = false;
    std::string              compileOutput;
    bool                     importLanguage = false;
    std::string              importName;
    std::string              importSubdir;
    std::string              importRef;
    std::string              importInto;
    bool                     testLanguage = false;
    bool                     bless        = false;
    bool                     forceBinary  = false;
    bool                     noRestore    = false;
    bool                     transient    = false;
    bool                     noTransient  = false;
    std::string              keymapStyle; // "emacs"/"vim"/"modern"; empty means "leave it to init.janet/default"
    std::vector<std::string> paths;
};

// Sets `app`'s own description/version flag and registers every option
// against `args`. `args` must outlive `app`'s parse.
void BuildCli(CLI::App& app, CliArgs& args);

// The group name BuildCli puts the mutually-exclusive startup-mode flags in.
// Named here because the reference generator sections its output by group.
inline constexpr const char* kStartupModesGroup = "Startup modes";

} // namespace ned::editor

#endif // NED_EDITOR_CLIOPTIONS_H
