#include <catch2/catch_test_macros.hpp>

#include <CLI/CLI.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "Editor/CliOptions.h"

// Docs/Invocation.md and Docs/man/ned.1.md, both generated from the live
// CLI::App Editor/CliOptions.cpp builds -- the same one `ned --help` prints
// from and main() parses with.
//
// The third of the generated-reference set (Commands.md and Scripting.md are
// in CommandReferenceTest.cpp, and the same bless variable regenerates all
// three):
//
//     NED_BLESS_COMMAND_DOCS=1 ./build/ned_tests "[CommandDocs]"
//
// then read the diff. An option with no description fails the build, the same
// way an undocumented command does: `ned --help` would show the blank too.
//
// Two renderings of one walk rather than one file converted twice, because
// the shapes genuinely differ -- the book page wants a heading per flag (it
// becomes the page's own sidebar), a man page wants a definition list (pandoc
// turns that into the `.TP` blocks every other man page uses).

namespace {

namespace fs = std::filesystem;

fs::path ReferencePath() {
    return fs::path(NED_REPO_ROOT) / "Docs" / "Invocation.md";
}
fs::path ManPath() {
    return fs::path(NED_REPO_ROOT) / "Docs" / "man" / "ned.1.md";
}

// "-o, --output" -- every spelling, short names first, as the user types them.
std::string Names(const CLI::Option& option) {
    if (option.get_positional() && !option.nonpositional())
        return option.get_name(true);

    std::string joined;
    for (const std::string& sname : option.get_snames()) {
        if (!joined.empty())
            joined += ", ";
        joined += "-" + sname;
    }
    for (const std::string& lname : option.get_lnames()) {
        if (!joined.empty())
            joined += ", ";
        joined += "--" + lname;
    }
    return joined;
}

// "--output TEXT" -- the placeholder CLI11 itself shows for a value-taking
// option, and nothing for a flag.
std::string NameWithValue(const CLI::Option& option) {
    const std::string type = option.get_type_name();
    return type.empty() ? Names(option) : Names(option) + " " + type;
}

// The `->needs()` relations, rendered. `->excludes()` is deliberately not:
// every startup mode excludes every other, which is one sentence of prose in
// the group's own heading rather than the same line repeated on 7 flags.
std::string Requires(const CLI::Option& option) {
    std::vector<std::string> needed;
    for (const CLI::Option* need : option.get_needs())
        needed.push_back("`--" + need->get_lnames().front() + "`");
    std::ranges::sort(needed);

    std::string joined;
    for (const std::string& name : needed) {
        if (!joined.empty())
            joined += ", ";
        joined += name;
    }
    return joined;
}

struct Section {
    std::string                     title;
    std::vector<const CLI::Option*> options;
};

// Startup modes first (they decide what the process even is), then ordinary
// options, then the positional -- `ned --help`'s own order, which is
// registration order within each group.
std::vector<Section> Sections(const CLI::App& app) {
    Section startup{ned::editor::kStartupModesGroup, {}};
    Section options{"Options", {}};
    Section positional{"Positional arguments", {}};

    for (const CLI::Option* option : app.get_options()) {
        if (option->get_positional() && !option->nonpositional())
            positional.options.push_back(option);
        else if (option->get_group() == ned::editor::kStartupModesGroup)
            startup.options.push_back(option);
        else
            options.options.push_back(option);
    }
    return {startup, options, positional};
}

constexpr const char* kStartupModesProse =
    "Each of these makes `ned` do one job and exit instead of starting the editor. They are\n"
    "mutually exclusive -- passing two is a usage error, not a silent win for whichever came\n"
    "first.\n";

constexpr const char* kSymlinkProse =
    "Four of the startup modes are also installed as their own executables, so a build tool can\n"
    "call one by name instead of remembering a flag. `ned-format` is `ned --format`, `ned-langc`\n"
    "is `ned --compile-language`, `ned-import-language` is `ned --import-language`, and\n"
    "`ned-test-language` is `ned --test-language`. Dispatch is on `argv[0]`'s own basename, and\n"
    "an explicit startup-mode flag on the command line still wins.\n";

std::string RenderBookPage(const CLI::App& app) {
    std::ostringstream out;
    out << "# Invocation\n\n"
        << "Every flag `ned` accepts. Generated from the same option table `ned --help` prints\n"
           "from, so it cannot drift from the binary you have installed.\n\n"
        << "```sh\n"
           "ned [options] [paths...]\n"
           "```\n\n"
        << kSymlinkProse << "\n"
        << "For what ned returns to a caller -- `EDITOR=ned` and a version control system reading\n"
           "the result -- see [Exit codes](getting-started.md#exit-codes).\n\n";

    for (const Section& section : Sections(app)) {
        if (section.options.empty())
            continue;
        out << "## " << section.title << "\n\n";
        if (section.title == ned::editor::kStartupModesGroup)
            out << kStartupModesProse << "\n";

        for (const CLI::Option* option : section.options) {
            out << "### `" << NameWithValue(*option) << "`\n\n"
                << option->get_description() << "\n\n";
            const std::string requires_ = Requires(*option);
            if (!requires_.empty())
                out << "Requires " << requires_ << ".\n\n";
        }
    }
    return out.str();
}

std::string RenderManPage(const CLI::App& app) {
    std::ostringstream out;
    // Title block only -- the version and the footer are passed by
    // CMake/ManPages.cmake at build time, so bumping project(Ned VERSION ...)
    // doesn't dirty a committed file.
    out << "% NED(1)\n\n"
        << "# NAME\n\n"
           "ned - a terminal-based, Janet-scriptable text editor\n\n"
        << "# SYNOPSIS\n\n"
           "**ned** \\[*options*] \\[*paths*...]\n\n"
           "**ned-format** \\[*paths*...]\n\n"
           "**ned-langc** \\[*language directories*...]\n\n"
        << "# DESCRIPTION\n\n"
        << app.get_description() << "\n\n"
        << "A path may be a file or a directory; a directory opens as a project. Paths beyond the\n"
           "first open as background buffers.\n\n"
        << kSymlinkProse << "\n";

    for (const Section& section : Sections(app)) {
        if (section.options.empty())
            continue;

        std::string heading = section.title;
        std::ranges::transform(heading, heading.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        out << "# " << heading << "\n\n";
        if (section.title == ned::editor::kStartupModesGroup)
            out << kStartupModesProse << "\n";

        for (const CLI::Option* option : section.options) {
            // A pandoc definition list: `.TP` in the man output.
            out << "`" << NameWithValue(*option) << "`\n\n"
                << ":   " << option->get_description() << "\n";
            const std::string requires_ = Requires(*option);
            if (!requires_.empty())
                out << "\n    Requires " << requires_ << ".\n";
            out << "\n";
        }
    }

    out << "# EXIT STATUS\n\n"
           "`0`\n\n"
           ":   Success. The editor ran and exited normally, and every file the user asked to save\n"
           "    is on disk.\n\n"
           "`1`\n\n"
           ":   A requested operation failed -- the general case, and what every failing startup\n"
           "    mode returns.\n\n"
           "`2`\n\n"
           ":   The command line itself was wrong. Nothing was attempted.\n\n"
           "`3`\n\n"
           ":   At least one file the user asked to save is still unwritten, because the write\n"
           "    failed. Quitting *without* saving is not this, and exits `0`.\n\n"
        << "# ENVIRONMENT\n\n"
           "`NED_DATA_DIR`\n\n"
           ":   Where ned reads its bundled data tree (languages, Janet plugins) from. Falls back\n"
           "    to `<exe>/../share/ned`, then the configured install datadir.\n\n"
           "`XDG_CONFIG_HOME`, `XDG_DATA_HOME`, `XDG_STATE_HOME`, `XDG_CACHE_HOME`\n\n"
           ":   Every file ned reads or writes outside a project lives under a `ned/`\n"
           "    subdirectory of one of these, per the XDG Base Directory specification.\n\n"
        << "# FILES\n\n"
           "`$XDG_CONFIG_HOME/ned/init.janet`\n\n"
           ":   Configuration, as Janet code. Not read by any startup mode except the editor\n"
           "    itself.\n\n"
           "`$XDG_CONFIG_HOME/ned/format.janet`, `<project>/.ned/format.janet`\n\n"
           ":   Formatter settings, as plain Janet data. Read by both the editor and `--format`.\n\n"
           "`$XDG_CONFIG_HOME/ned/languages/`\n\n"
           ":   Language packages installed by `--import-language`.\n\n"
        << "# SEE ALSO\n\n"
           "**ned-commands**(7), **ned-janet**(7)\n\n"
           "The full documentation is at <https://superfes.github.io/ned/>.\n";
    return out.str();
}

std::string ReadFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in);
    std::ostringstream content;
    content << in.rdbuf();
    return content.str();
}

void CheckOrBless(const fs::path& path, const std::string& rendered) {
    if (std::getenv("NED_BLESS_COMMAND_DOCS") != nullptr) {
        fs::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        REQUIRE(out);
        out << rendered;
        SUCCEED("regenerated " + path.string() + " -- read the diff");
        return;
    }

    INFO("regenerate: NED_BLESS_COMMAND_DOCS=1 ./build/ned_tests \"[CommandDocs]\"");
    REQUIRE(fs::exists(path));
    CHECK(ReadFile(path) == rendered);
}

} // namespace

TEST_CASE("Every command-line option carries a description", "[CommandDocs]") {
    CLI::App             app{"", "ned"};
    ned::editor::CliArgs args;
    ned::editor::BuildCli(app, args);

    std::string undocumented;
    for (const CLI::Option* option : app.get_options()) {
        if (option->get_description().empty())
            undocumented += " " + Names(*option);
    }
    INFO("options with no description:" << undocumented);
    CHECK(undocumented.empty());
}

TEST_CASE("Docs/Invocation.md matches the live option table", "[CommandDocs]") {
    CLI::App             app{"", "ned"};
    ned::editor::CliArgs args;
    ned::editor::BuildCli(app, args);
    CheckOrBless(ReferencePath(), RenderBookPage(app));
}

TEST_CASE("Docs/man/ned.1.md matches the live option table", "[CommandDocs]") {
    CLI::App             app{"", "ned"};
    ned::editor::CliArgs args;
    ned::editor::BuildCli(app, args);
    CheckOrBless(ManPath(), RenderManPage(app));
}
