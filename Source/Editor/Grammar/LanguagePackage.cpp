#include "LanguagePackage.h"

#include <dlfcn.h>

#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>

#include "Editor/Grammar/Compile/Compiler.h"
#include "Editor/Grammar/Compile/GrammarFile.h"
#include "Editor/Grammar/Compile/TableFile.h"
#include "Editor/Languages/Scanners/Scanners.h"

namespace ned::editor::grammar {

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

    std::unique_ptr<compile::CompiledLanguage> LoadTables(const fs::path& directory) {
        const fs::path tables  = directory / "tables";
        const fs::path grammar = directory / "grammar.janet";
        if (fs::exists(tables)) {
            try {
                return compile::LoadLanguage(ReadWhole(tables));
            }
            catch (const compile::CompileError& e) {
                throw std::runtime_error(tables.string() + ": " + e.what());
            }
        }
        if (fs::exists(grammar)) {
            try {
                return compile::CompileGrammar(compile::ParseGrammarJanet(ReadWhole(grammar)));
            }
            catch (const compile::GrammarFileError& e) {
                throw std::runtime_error(grammar.string() + ":" + std::to_string(e.Line()) + ": " + e.what());
            }
            catch (const compile::CompileError& e) {
                throw std::runtime_error(grammar.string() + ": " + e.what());
            }
        }
        throw std::runtime_error(directory.string() + ": no tables or grammar.janet");
    }

    // The handle is never closed: the table stays in use for the process.
    const parse::ScannerVTable* LoadScannerLibrary(const fs::path& library, const std::string& name) {
        void* handle = dlopen(library.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (handle == nullptr) {
            const char* reason = dlerror();
            throw std::runtime_error(library.string() + ": " + (reason != nullptr ? reason : "dlopen failed"));
        }
        const std::string symbol = "ned_scanner_" + name;
        void*             entry  = dlsym(handle, symbol.c_str());
        if (entry == nullptr)
            throw std::runtime_error(library.string() + ": no " + symbol + " export");
        using Entry = const parse::ScannerVTable* (*)();
        return reinterpret_cast<Entry>(entry)();
    }

    std::mutex                                                        g_mutex;
    std::map<std::string, std::unique_ptr<compile::CompiledLanguage>> g_packages;

} // namespace

Language LoadLanguagePackage(const fs::path& directory, const PackageScanner& scanner) {
    std::error_code   ec;
    const fs::path    canonical = fs::weakly_canonical(directory, ec);
    const std::string key       = (ec ? directory : canonical).string();

    const std::lock_guard<std::mutex> lock(g_mutex);
    if (const auto it = g_packages.find(key); it != g_packages.end())
        return Language(it->second->Data());

    std::unique_ptr<compile::CompiledLanguage> package = LoadTables(directory);
    const parse::ScannerVTable*                vtable  = scanner.library.empty() ? languages::scanners::FindBundledScanner(scanner.name)
                                                                                 : LoadScannerLibrary(scanner.library, scanner.name);
    if (package->Data()->externalTokenCount > 0) {
        if (vtable == nullptr)
            throw std::runtime_error(directory.string() + ": the grammar declares external tokens but no scanner named '" + scanner.name +
                                     "' is bundled and no :scanner-library was given");
        package->AdoptExternalScanner(*vtable);
    }
    const Language language(package->Data());
    g_packages.emplace(key, std::move(package));
    return language;
}

} // namespace ned::editor::grammar
