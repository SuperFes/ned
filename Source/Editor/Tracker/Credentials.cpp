#include "Credentials.h"

namespace ned::editor::tracker {

namespace {

    std::string ConnectionSubject(const std::string& name) {
        return "connection \"" + name + "\"";
    }

    bool IsSpace(char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
    }

} // namespace

std::optional<std::string> MissingCredentials(const Connection& connection) {
    if (connection.email.empty() || connection.tokenCommand.empty()) {
        return ConnectionSubject(connection.name) + " needs an :email and a :token-command";
    }
    return std::nullopt;
}

std::expected<std::string, std::string> TokenFromOutput(const std::string& connectionName, const std::string& output,
                                                        std::optional<int> exitCode) {
    const std::string subject = ConnectionSubject(connectionName) + ": :token-command";
    if (!exitCode) {
        return std::unexpected(subject + " couldn't start or was terminated");
    }
    if (*exitCode != 0) {
        return std::unexpected(subject + " failed (exit " + std::to_string(*exitCode) + ")");
    }
    std::size_t begin = 0;
    std::size_t end   = output.size();
    while (begin < end && IsSpace(output[begin])) {
        ++begin;
    }
    while (end > begin && IsSpace(output[end - 1])) {
        --end;
    }
    if (begin == end) {
        return std::unexpected(subject + " printed nothing");
    }
    return output.substr(begin, end - begin);
}

std::expected<std::string, std::string> CurlUserConfig(std::string_view email, std::string_view token) {
    std::string line = "user = \"";
    for (const std::string_view part : {email, std::string_view(":"), token}) {
        for (const char c : part) {
            if (c == '\n' || c == '\r' || c == '\0') {
                return std::unexpected(std::string("the email or token contains a line break"));
            }
            if (c == '\\' || c == '"') {
                line += '\\';
            }
            line += c;
        }
    }
    line += "\"\n";
    return line;
}

std::expected<CredentialedCommand, std::string> CredentialCommand(const Connection& connection, std::vector<std::string> argv,
                                                                  const std::string& tokenOutput, std::optional<int> exitCode) {
    const auto token = TokenFromOutput(connection.name, tokenOutput, exitCode);
    if (!token) {
        return std::unexpected(token.error());
    }
    const auto config = CurlUserConfig(connection.email, *token);
    if (!config) {
        return std::unexpected(ConnectionSubject(connection.name) + ": " + config.error());
    }
    auto file = PrivateFile::Write(*config);
    if (!file) {
        return std::unexpected(ConnectionSubject(connection.name) + ": " + file.error());
    }
    argv.push_back("-K");
    argv.push_back((*file)->Path().string());
    return CredentialedCommand{.argv = std::move(argv), .file = std::move(*file)};
}

} // namespace ned::editor::tracker
