#include "TestRunConfig.h"

#include <mutex>
#include <unordered_map>
#include <utility>

namespace ned::editor::testrun {

namespace {

    std::mutex                                    g_mutex;
    std::optional<TestCommandConfig>              g_command;
    std::optional<std::vector<std::string>>       g_filterTemplate;
    std::optional<std::string>                    g_resultsFile;
    std::unordered_map<std::string, TestParserFn> g_parsers;

    void ReplaceAll(std::string& text, std::string_view placeholder, const std::string& value) {
        std::size_t pos = 0;
        while ((pos = text.find(placeholder, pos)) != std::string::npos) {
            text.replace(pos, placeholder.size(), value);
            pos += value.size();
        }
    }

} // namespace

void SetTestCommand(std::vector<std::string> argv, std::string format) {
    const std::lock_guard<std::mutex> lock(g_mutex);
    if (argv.empty()) {
        g_command.reset();
    }
    else {
        g_command = TestCommandConfig{.argv = std::move(argv), .format = std::move(format)};
    }
}

std::optional<TestCommandConfig> TestCommand() {
    const std::lock_guard<std::mutex> lock(g_mutex);
    return g_command;
}

void SetTestFilterCommand(std::vector<std::string> argvTemplate) {
    const std::lock_guard<std::mutex> lock(g_mutex);
    if (argvTemplate.empty()) {
        g_filterTemplate.reset();
    }
    else {
        g_filterTemplate = std::move(argvTemplate);
    }
}

std::optional<std::vector<std::string>> TestFilterCommand() {
    const std::lock_guard<std::mutex> lock(g_mutex);
    return g_filterTemplate;
}

std::vector<std::string> SubstituteFilterTemplate(const std::vector<std::string>& argvTemplate,
                                                  const std::string& testName, const std::string& file) {
    std::vector<std::string> argv;
    argv.reserve(argvTemplate.size());
    for (const std::string& element : argvTemplate) {
        std::string substituted = element;
        ReplaceAll(substituted, "{test}", testName);
        ReplaceAll(substituted, "{file}", file);
        argv.push_back(std::move(substituted));
    }
    return argv;
}

void SetTestResultsFile(std::string path) {
    const std::lock_guard<std::mutex> lock(g_mutex);
    if (path.empty()) {
        g_resultsFile.reset();
    }
    else {
        g_resultsFile = std::move(path);
    }
}

std::optional<std::string> TestResultsFile() {
    const std::lock_guard<std::mutex> lock(g_mutex);
    return g_resultsFile;
}

void RegisterTestParser(const std::string& name, TestParserFn fn) {
    const std::lock_guard<std::mutex> lock(g_mutex);
    if (!fn) {
        g_parsers.erase(name);
    }
    else {
        g_parsers[name] = std::move(fn);
    }
}

std::optional<TestParserFn> RegisteredTestParser(const std::string& name) {
    const std::lock_guard<std::mutex> lock(g_mutex);
    const auto                        it = g_parsers.find(name);
    if (it == g_parsers.end()) {
        return std::nullopt;
    }
    return it->second;
}

} // namespace ned::editor::testrun
