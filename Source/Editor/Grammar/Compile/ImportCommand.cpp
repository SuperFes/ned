#include "ImportCommand.h"

#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <ostream>
#include <regex>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "Editor/DataDir.h"
#include "Editor/Grammar/Compile/CompileCommand.h"
#include "Editor/Grammar/Compile/GrammarFile.h"
#include "Editor/Grammar/Compile/TestCommand.h"
#include "Editor/Process/ChildProcess.h"
#include "Editor/QueryData.h"

namespace ned::editor::grammar::compile {

namespace {

    namespace fs = std::filesystem;

    std::string ReadWhole(const fs::path& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in)
            throw std::runtime_error("cannot read " + path.string());
        std::ostringstream buffer;
        buffer << in.rdbuf();
        return buffer.str();
    }

    void WriteWhole(const fs::path& path, std::string_view text) {
        fs::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out || !out.write(text.data(), static_cast<std::streamsize>(text.size())))
            throw std::runtime_error("cannot write " + path.string());
    }

    std::size_t CountLines(const std::string& text) {
        return static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n'));
    }

    bool LooksLikeUrl(const std::string& source) {
        return source.rfind("http://", 0) == 0 || source.rfind("https://", 0) == 0 || source.rfind("git@", 0) == 0 || source.rfind("ssh://", 0) == 0 ||
               source.rfind("git://", 0) == 0;
    }

    // Runs `argv` to completion; the child's output (stdout and stderr
    // merged) comes back for reporting.
    std::pair<int, std::string> Run(const std::vector<std::string>& argv) {
        process::ChildProcess child(argv, process::StderrMode::MergeWithStdout);
        std::string           output;
        while (const std::optional<std::string> chunk = child.ReadSome(std::chrono::milliseconds(200))) {
            if (chunk->empty())
                break;
            output += *chunk;
        }
        return {child.WaitForExit().value_or(-1), output};
    }

    // The port-scanner helper: beside the executable as libexec/ned/
    // port-scanner (installed), or the source tree's Tools/port-scanner.py
    // (NED_PORT_SCANNER names either explicitly).
    std::optional<fs::path> PortScannerHelper() {
        if (const char* env = std::getenv("NED_PORT_SCANNER"); env != nullptr && fs::exists(env))
            return fs::path(env);
        if (const auto candidates = DefaultDataDirCandidates(); candidates.executableRelative) {
            const fs::path prefix = candidates.executableRelative->parent_path().parent_path();
            for (const fs::path candidate : {prefix / "libexec" / "ned" / "port-scanner", prefix / ".." / "Tools" / "port-scanner.py"})
                if (fs::exists(candidate))
                    return fs::weakly_canonical(candidate);
        }
        return std::nullopt;
    }

    struct FileTypes {
        std::vector<std::string> extensions;
        std::vector<std::string> filenames;
    };

    // The file types a grammar repository declares for itself:
    // tree-sitter.json's `grammars[].file-types`, else package.json's
    // `tree-sitter[].file-types`. A bare word is an extension; a dotfile
    // (".gitattributes") or a dotted name ("requirements.txt") is a whole
    // basename, which the definition claims through :filenames.
    FileTypes DeclaredFileTypes(const fs::path& repo, const fs::path& grammarDir) {
        FileTypes  types;
        const auto collect = [&](const nlohmann::json& list) {
            if (!list.is_array())
                return;
            for (const nlohmann::json& entry : list) {
                if (!entry.is_object())
                    continue;
                const auto declared = entry.find("file-types");
                if (declared == entry.end() || !declared->is_array())
                    continue;
                for (const nlohmann::json& type : *declared) {
                    if (!type.is_string())
                        continue;
                    const std::string text = type.get<std::string>();
                    if (text.empty())
                        continue;
                    std::vector<std::string>& into = text.find('.') != std::string::npos ? types.filenames : types.extensions;
                    const std::string         item = &into == &types.extensions ? "." + text : text;
                    if (std::find(into.begin(), into.end(), item) == into.end())
                        into.push_back(item);
                }
            }
        };
        for (const fs::path dir : {grammarDir, repo}) {
            for (const char* file : {"tree-sitter.json", "package.json"}) {
                const fs::path path = dir / file;
                if (!fs::exists(path))
                    continue;
                nlohmann::json json;
                try {
                    json = nlohmann::json::parse(ReadWhole(path));
                }
                catch (const nlohmann::json::exception&) {
                    continue;
                }
                if (std::string(file) == "tree-sitter.json")
                    collect(json.value("grammars", nlohmann::json::array()));
                else
                    collect(json.value("tree-sitter", nlohmann::json::array()));
                if (!types.extensions.empty() || !types.filenames.empty())
                    return types;
            }
        }
        return types;
    }

    bool LooksLikeCommitHash(const std::string& ref) {
        return ref.size() == 40 && std::all_of(ref.begin(), ref.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
    }

    std::string Today() {
        const std::time_t now = std::time(nullptr);
        char              buffer[16];
        std::strftime(buffer, sizeof buffer, "%Y-%m-%d", std::gmtime(&now));
        return buffer;
    }

    std::string CamelCase(const std::string& name) {
        std::string out;
        bool        upper = true;
        for (const char c : name) {
            if (c == '-' || c == '_') {
                upper = true;
                continue;
            }
            out.push_back(upper ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c);
            upper = false;
        }
        return out;
    }

} // namespace

int RunImportLanguage(const ImportOptions& options, std::ostream& out, std::ostream& err) {
    fs::path repo;
    fs::path scratch;
    try {
        if (LooksLikeUrl(options.source)) {
            scratch = fs::temp_directory_path() / ("ned-import-" + std::to_string(::getpid()));
            fs::remove_all(scratch);
            // A tag or branch clones by name; a full commit hash (the other
            // pin the admission policy accepts) needs --revision.
            std::vector<std::string> argv = {"git", "-c", "advice.detachedHead=false", "clone", "--depth", "1", "--quiet"};
            if (!options.ref.empty()) {
                argv.emplace_back(LooksLikeCommitHash(options.ref) ? "--revision" : "--branch");
                argv.push_back(options.ref);
            }
            argv.push_back(options.source);
            argv.push_back(scratch.string());
            out << "cloning " << options.source << (options.ref.empty() ? "" : " at " + options.ref) << "\n";
            if (const auto [code, output] = Run(argv); code != 0) {
                err << "git clone failed (exit " << code << ")\n"
                    << output;
                return 2;
            }
            repo = scratch;
        }
        else {
            repo = fs::path(options.source);
            if (!fs::is_directory(repo)) {
                err << options.source << ": not a directory or git URL\n";
                return 2;
            }
        }
        const fs::path grammarDir  = options.subdir.empty() ? repo : repo / options.subdir;
        const fs::path grammarJson = grammarDir / "src" / "grammar.json";
        if (!fs::exists(grammarJson)) {
            err << grammarJson.string() << ": not found (a grammar repository ships src/grammar.json; use --subdir for a multi-grammar repository)\n";
            return 2;
        }

        const GrammarFile grammar = ParseGrammarJson(nlohmann::ordered_json::parse(ReadWhole(grammarJson)));
        const std::string name    = options.name.empty() ? grammar.name : options.name;
        const fs::path    into    = options.into.empty() ? fs::path(std::getenv("HOME") != nullptr ? std::getenv("HOME") : ".") / ".config" / "ned" / "languages" : fs::path(options.into);
        const fs::path    package = into / name;
        if (fs::exists(package)) {
            err << package.string() << ": already exists; remove it or import under another --name\n";
            return 2;
        }
        fs::create_directories(package);
        out << "package " << package.string() << "\n";

        // The grammar.
        WriteWhole(package / "grammar.janet", ToGrammarJanet(grammar));
        out << "  grammar.janet (" << grammar.rules.size() << " rules, " << grammar.externals.size() << " external tokens)\n";

        // Upstream queries, in ned's spelling: queries/<kind>.scm, or the
        // per-language layout some repositories use, queries/<name>/<kind>.scm,
        // or a queries/nvim/ set kept beside editor-specific variants -- under
        // the grammar's directory, else the repository root (a multi-grammar
        // repository keeps one queries/ tree beside its grammars).
        std::vector<std::string> queryKinds;
        for (const char* kind : {"highlights", "tags", "injections", "locals"}) {
            const std::string file = std::string(kind) + ".scm";
            for (const fs::path scm : {grammarDir / "queries" / file, grammarDir / "queries" / grammar.name / file, grammarDir / "queries" / "nvim" / file,
                                       grammarDir / "queries" / "neovim" / file, grammarDir / "queries" / "Neovim" / file, repo / "queries" / file,
                                       repo / "queries" / grammar.name / file}) {
                if (!fs::exists(scm))
                    continue;
                WriteWhole(package / "upstream" / (std::string(kind) + ".janet"), querydata::ConvertScmToJanet(ReadWhole(scm)));
                queryKinds.emplace_back(kind);
                break;
            }
        }
        if (!queryKinds.empty()) {
            out << "  upstream/:";
            for (const std::string& kind : queryKinds)
                out << " " << kind << ".janet";
            out << "\n";
        }

        // The corpus.
        // A repository holding several grammars keeps one corpus at its root,
        // routing cases with :language(...) markers.
        std::size_t corpusFiles = 0;
        fs::path    corpus      = grammarDir / "test" / "corpus";
        for (const fs::path candidate : {repo / "test" / "corpus", grammarDir / "corpus", repo / "corpus"})
            if (!fs::is_directory(corpus))
                corpus = candidate; // some repositories (nix, astro) keep the corpus at the root
        if (fs::is_directory(corpus)) {
            fs::copy(corpus, package / "corpus", fs::copy_options::recursive);
            for (const auto& entry : fs::recursive_directory_iterator(package / "corpus"))
                corpusFiles += entry.is_regular_file() ? 1 : 0;
        }
        out << "  corpus/ (" << corpusFiles << " files)\n";

        // The scanner, staged; ported when the helper is at hand.
        std::size_t scannerLines = 0;
        std::string scannerNote;
        fs::path    scannerSource;
        for (const char* file : {"scanner.c", "scanner.cc", "scanner.cpp"})
            if (fs::exists(grammarDir / "src" / file))
                scannerSource = grammarDir / "src" / file;
        if (!scannerSource.empty()) {
            // Everything under src/ but the generated files and tree-sitter's
            // own headers: a scanner may include siblings from a
            // subdirectory (rst's tree_sitter_rst/, asciidoc's include/),
            // which the port helper inlines from the staged copy.
            const fs::path staging = package / "scanner";
            fs::create_directories(staging);
            for (const auto& entry : fs::directory_iterator(grammarDir / "src")) {
                const std::string base = entry.path().filename().string();
                if (entry.is_directory() && base != "tree_sitter")
                    fs::copy(entry.path(), staging / base, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
                else if (entry.is_regular_file() && base != "parser.c" && base != "grammar.json" && base != "node-types.json")
                    fs::copy_file(entry.path(), staging / base, fs::copy_options::overwrite_existing);
            }
            // A multi-grammar repository keeps shared scanner code in a
            // common/ directory above the grammar (typescript, ocaml,
            // fsharp), reached by `#include "../../common/scanner.h"`. It is
            // staged beside the scanner and the includes retargeted, so the
            // port helper inlines it from the staged copy.
            for (fs::path dir = grammarDir;; dir = dir.parent_path()) {
                if (const fs::path common = dir / "common"; fs::is_directory(common) && dir != grammarDir) {
                    fs::copy(common, staging / "common", fs::copy_options::recursive | fs::copy_options::overwrite_existing);
                    for (const auto& entry : fs::recursive_directory_iterator(staging)) {
                        if (!entry.is_regular_file())
                            continue;
                        const std::string text       = ReadWhole(entry.path());
                        const std::string retargeted = std::regex_replace(text, std::regex("#include \"(?:\\.\\./)+common/"), "#include \"common/");
                        if (retargeted != text)
                            WriteWhole(entry.path(), retargeted);
                    }
                    break;
                }
                if (dir == repo || dir.empty() || dir == dir.parent_path())
                    break;
            }
            scannerLines               = CountLines(ReadWhole(scannerSource));
            const std::string ported   = CamelCase(name) + "Scanner.cpp";
            const fs::path    portedTo = staging / ported;
            if (const std::optional<fs::path> helper = PortScannerHelper()) {
                const auto [code, output] = Run({"python3", helper->string(), (staging / scannerSource.filename()).string(), name, portedTo.string(), "--library", "--repo", options.source});
                if (code != 0)
                    err << output;
                scannerNote = code == 0 ? "ported to scanner/" + ported + " -- build it as a shared library (see ned's language authoring guide) and set :scanner-library"
                                        : "staged as scanner/" + scannerSource.filename().string() + " -- the port helper failed; port it by hand against Editor/Parse/Scanner.h";
            }
            else {
                scannerNote = "staged as scanner/" + scannerSource.filename().string() + " -- port it with Tools/port-scanner.py (not found beside this ned)";
            }
            out << "  scanner: " << scannerLines << " lines, " << scannerNote << "\n";
        }

        // Admission facts, for the skeleton's header.
        std::string abiVersion = "unknown";
        if (const fs::path parserC = grammarDir / "src" / "parser.c"; fs::exists(parserC)) {
            std::smatch       match;
            const std::string text = ReadWhole(parserC).substr(0, 4096);
            if (std::regex_search(text, match, std::regex("#define LANGUAGE_VERSION (\\d+)")))
                abiVersion = match[1];
        }
        const FileTypes fileTypes = DeclaredFileTypes(repo, grammarDir);

        // The definition skeleton.
        std::ostringstream definition;
        definition << "# " << name << ": imported " << Today() << " from " << options.source << (options.ref.empty() ? "" : " (" + options.ref + ")")
                   << (options.subdir.empty() ? "" : ", subdir " + options.subdir) << "\n"
                   << "# Admission facts (Docs/LanguageCoverage.md): generated ABI " << abiVersion << ", scanner " << scannerLines << " lines, corpus " << corpusFiles
                   << " files.\n"
                   << "# Fill in what the import cannot know: :line-comment, :lsp-root-markers, and a\n"
                   << "# tags.janet/indents.janet beside this file.\n\n"
                   << "{:name \"" << name << "\"\n";
        const auto writeList = [&](const char* key, const std::vector<std::string>& items) {
            definition << " " << key << " [";
            for (std::size_t i = 0; i < items.size(); ++i)
                definition << (i > 0 ? " " : "") << "\"" << items[i] << "\"";
            definition << "]\n";
        };
        if (!fileTypes.extensions.empty() || fileTypes.filenames.empty())
            writeList(":extensions", fileTypes.extensions);
        if (!fileTypes.filenames.empty())
            writeList(":filenames", fileTypes.filenames);
        if (!scannerSource.empty())
            definition << " # The grammar's external scanner: uncomment once scanner/ is built as a shared library.\n"
                       << " # :scanner-library \"" << (package / "scanner" / ("libned-" + name + "-scanner.so")).string() << "\"\n";
        definition << "}\n";
        WriteWhole(package / "language.janet", definition.str());
        out << "  language.janet (file types:";
        for (const std::string& extension : fileTypes.extensions)
            out << " " << extension;
        for (const std::string& filename : fileTypes.filenames)
            out << " " << filename;
        out << (fileTypes.extensions.empty() && fileTypes.filenames.empty() ? " none declared" : "") << ")\n";

        // Compile, then run the corpus.
        if (const int code = RunCompileLanguage({package.string()}, "", out, err); code != 0)
            return 2;
        if (!scannerSource.empty() && !grammar.externals.empty()) {
            out << "the grammar has external tokens: build the scanner and set :scanner-library, then `ned --test-language " << package.string() << "`\n";
            return 1;
        }
        if (corpusFiles == 0)
            return 0;
        return RunTestLanguage({package.string()}, false, out, err);
    }
    catch (const std::exception& e) {
        err << "ned --import-language: " << e.what() << "\n";
        return 2;
    }
}

} // namespace ned::editor::grammar::compile
