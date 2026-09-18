#include "CompileCommand.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <ostream>
#include <sstream>

#include "Editor/Grammar/Compile/Compiler.h"
#include "Editor/Grammar/Compile/GrammarFile.h"
#include "Editor/Grammar/Compile/TableFile.h"

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

    void WriteWhole(const fs::path& path, std::string_view bytes) {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out || !out.write(bytes.data(), static_cast<std::streamsize>(bytes.size())))
            throw std::runtime_error("cannot write " + path.string());
    }

    bool CompileOne(const fs::path& source, const fs::path& output, std::ostream& out, std::ostream& err) {
        const fs::path grammarPath = fs::is_directory(source) ? source / "grammar.janet" : source;
        try {
            const std::string text    = ReadWhole(grammarPath);
            const auto        started = std::chrono::steady_clock::now();
            const GrammarFile grammar = ParseGrammarJanet(text);
            const auto        tables  = CompileGrammar(grammar);
            const std::string bytes   = SerializeLanguage(*tables);
            const auto        elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
            WriteWhole(output, bytes);
            const auto& data = *tables->Data();
            out << grammar.name << ": " << data.symbolCount << " symbols, " << data.stateCount << " parse states, " << tables->language_->main.states.size()
                << " lex states, " << bytes.size() << " bytes -> " << output.string() << " (" << elapsed << " ms)\n";
            return true;
        }
        catch (const GrammarFileError& e) {
            err << grammarPath.string() << ":" << e.Line() << ": " << e.what() << "\n";
        }
        catch (const CompileError& e) {
            err << grammarPath.string() << ": " << e.what() << "\n";
        }
        catch (const std::exception& e) {
            err << grammarPath.string() << ": " << e.what() << "\n";
        }
        return false;
    }

} // namespace

int RunCompileLanguage(const std::vector<std::string>& sources, const std::string& output, std::ostream& out, std::ostream& err) {
    if (sources.empty()) {
        err << "ned --compile-language: a language directory (or its grammar.janet) is required\n";
        return 2;
    }
    if (!output.empty() && sources.size() > 1) {
        err << "ned --compile-language: --output names one file, but " << sources.size() << " languages were given\n";
        return 2;
    }
    bool ok = true;
    for (const std::string& source : sources) {
        const fs::path path(source);
        if (!fs::exists(path)) {
            err << source << ": no such file or directory\n";
            ok = false;
            continue;
        }
        const fs::path target = !output.empty() ? fs::path(output) : (fs::is_directory(path) ? path : path.parent_path()) / "tables";
        ok                    = CompileOne(path, target, out, err) && ok;
    }
    return ok ? 0 : 1;
}

} // namespace ned::editor::grammar::compile
