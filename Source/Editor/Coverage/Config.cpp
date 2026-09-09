#include "Config.h"

#include <fstream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "OutputParser.h"

namespace ned::editor::coverage {

namespace {

    std::mutex                 g_mutex;
    std::optional<std::string> g_file;
    Report             g_report;
    std::size_t                g_generation = 0;

} // namespace

void SetFile(std::string path) {
    const std::lock_guard<std::mutex> lock(g_mutex);
    if (path.empty()) {
        g_file.reset();
    }
    else {
        g_file = std::move(path);
    }
}

std::optional<std::string> File() {
    const std::lock_guard<std::mutex> lock(g_mutex);
    return g_file;
}

void LoadCoverageReport() {
    std::string path;
    {
        const std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_file) {
            throw std::runtime_error("no coverage file configured (see ned/set-coverage-file)");
        }
        path = *g_file;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("could not open coverage file: " + path);
    }
    std::ostringstream contents;
    contents << in.rdbuf();
    Report report = ParseLcovInfo(contents.str());

    const std::lock_guard<std::mutex> lock(g_mutex);
    g_report = std::move(report);
    ++g_generation;
}

void ClearCoverageReport() {
    const std::lock_guard<std::mutex> lock(g_mutex);
    g_report.clear();
    ++g_generation;
}

Report CurrentCoverageReport() {
    const std::lock_guard<std::mutex> lock(g_mutex);
    return g_report;
}

std::size_t ReportGeneration() {
    const std::lock_guard<std::mutex> lock(g_mutex);
    return g_generation;
}

} // namespace ned::editor::coverage
