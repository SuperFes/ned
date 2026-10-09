#include "KnownHosts.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>

#include <arpa/inet.h>
#include <glob.h>

#include "Editor/FuzzyMatch.h"
#include "Text/RemoteFile.h"

namespace ned::editor::remote {

namespace {

    // ssh gives up past this depth too.
    constexpr int kMaxIncludeDepth = 16;

    std::string_view Trim(std::string_view text) {
        const std::size_t first = text.find_first_not_of(" \t\r");
        if (first == std::string_view::npos) {
            return {};
        }
        return text.substr(first, text.find_last_not_of(" \t\r") - first + 1);
    }

    std::vector<std::string_view> Lines(std::string_view text) {
        std::vector<std::string_view> lines;
        while (!text.empty()) {
            const std::size_t end = text.find('\n');
            lines.push_back(text.substr(0, end));
            text = end == std::string_view::npos ? std::string_view() : text.substr(end + 1);
        }
        return lines;
    }

    std::vector<std::string> Words(std::string_view text) {
        std::vector<std::string> words;
        std::size_t              i = 0;
        while (i < text.size()) {
            if (std::isspace(static_cast<unsigned char>(text[i])) != 0) {
                ++i;
                continue;
            }
            if (text[i] == '#') {
                break;
            }
            std::string word;
            if (text[i] == '"') {
                const std::size_t close = text.find('"', i + 1);
                word                    = std::string(text.substr(i + 1, close == std::string_view::npos ? std::string_view::npos : close - i - 1));
                i                       = close == std::string_view::npos ? text.size() : close + 1;
            }
            else {
                while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])) == 0) {
                    word += text[i++];
                }
            }
            words.push_back(std::move(word));
        }
        return words;
    }

    std::string Lower(std::string_view text) {
        std::string lower(text);
        std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return lower;
    }

    bool IsPattern(std::string_view name) {
        return name.find_first_of("*?!") != std::string_view::npos;
    }

    bool IsAddress(const std::string& name) {
        unsigned char buffer[16];
        return ::inet_pton(AF_INET, name.c_str(), buffer) == 1 || ::inet_pton(AF_INET6, name.c_str(), buffer) == 1;
    }

    // ~ and the %d/%% tokens; nullopt for a path that needs any other token.
    std::optional<std::filesystem::path> ExpandPath(std::string_view word, const std::filesystem::path& home) {
        std::string expanded;
        if (word == "~" || word.starts_with("~/")) {
            expanded = home.string();
            word.remove_prefix(1);
        }
        for (std::size_t i = 0; i < word.size(); ++i) {
            if (word[i] != '%') {
                expanded += word[i];
                continue;
            }
            if (i + 1 == word.size()) {
                return std::nullopt;
            }
            switch (word[++i]) {
                case '%':
                    expanded += '%';
                    break;
                case 'd':
                    expanded += home.string();
                    break;
                default:
                    return std::nullopt;
            }
        }
        return std::filesystem::path(expanded);
    }

    std::optional<std::string> ReadFile(const std::filesystem::path& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            return std::nullopt;
        }
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    std::vector<std::filesystem::path> Glob(const std::string& pattern) {
        std::vector<std::filesystem::path> paths;
        glob_t                             matches{};
        if (::glob(pattern.c_str(), 0, nullptr, &matches) == 0) {
            for (std::size_t i = 0; i < matches.gl_pathc; ++i) {
                paths.emplace_back(matches.gl_pathv[i]);
            }
        }
        ::globfree(&matches);
        return paths;
    }

    struct Collected {
        std::vector<std::string>           hosts;
        std::vector<std::filesystem::path> knownHostsFiles;
    };

    // A config line's keyword, lower case, and its arguments; nullopt for a
    // blank line or a comment.
    std::optional<std::pair<std::string, std::vector<std::string>>> SplitLine(std::string_view line) {
        line = Trim(line);
        if (line.empty() || line.front() == '#') {
            return std::nullopt;
        }
        const std::size_t keywordEnd = line.find_first_of(" \t=");
        std::string_view  rest       = keywordEnd == std::string_view::npos ? std::string_view() : Trim(line.substr(keywordEnd));
        if (rest.starts_with('=')) {
            rest = Trim(rest.substr(1));
        }
        return std::pair{Lower(line.substr(0, keywordEnd)), Words(rest)};
    }

    // ssh's glob: * and ?, ignoring case.
    bool GlobMatches(std::string_view pattern, std::string_view text) {
        std::size_t p      = 0;
        std::size_t t      = 0;
        std::size_t star   = std::string_view::npos;
        std::size_t resume = 0;
        while (t < text.size()) {
            if (p < pattern.size() && (pattern[p] == '?' || std::tolower(static_cast<unsigned char>(pattern[p])) ==
                                                                std::tolower(static_cast<unsigned char>(text[t])))) {
                ++p;
                ++t;
            }
            else if (p < pattern.size() && pattern[p] == '*') {
                star   = p++;
                resume = t;
            }
            else if (star != std::string_view::npos) {
                p = star + 1;
                t = ++resume;
            }
            else {
                return false;
            }
        }
        while (p < pattern.size() && pattern[p] == '*') {
            ++p;
        }
        return p == pattern.size();
    }

    // Some pattern matches and no negated one does.
    template <typename Patterns>
    bool PatternsMatch(const Patterns& patterns, std::string_view host) {
        bool matched = false;
        for (std::string_view pattern : patterns) {
            const bool negated = pattern.starts_with('!');
            if (GlobMatches(negated ? pattern.substr(1) : pattern, host)) {
                if (negated) {
                    return false;
                }
                matched = true;
            }
        }
        return matched;
    }

    std::vector<std::string_view> CommaList(std::string_view list) {
        std::vector<std::string_view> items;
        while (!list.empty()) {
            const std::size_t comma = list.find(',');
            items.push_back(list.substr(0, comma));
            list = comma == std::string_view::npos ? std::string_view() : list.substr(comma + 1);
        }
        return items;
    }

    // [user@]host or an ssh URI, apart; an address loses its brackets.
    struct Destination {
        std::optional<std::string> user;
        std::string                host;
        std::optional<std::string> port;
    };

    Destination SplitDestination(std::string_view destination) {
        const bool uri = destination.starts_with(text::kSshScheme);
        if (uri) {
            destination.remove_prefix(text::kSshScheme.size());
            while (destination.ends_with('/')) {
                destination.remove_suffix(1);
            }
        }
        Destination split;
        if (const std::size_t at = destination.rfind('@'); at != std::string_view::npos) {
            split.user = std::string(destination.substr(0, at));
            destination.remove_prefix(at + 1);
        }
        if (destination.starts_with('[')) {
            const std::size_t close = destination.find(']');
            split.host              = std::string(destination.substr(1, close == std::string_view::npos ? std::string_view::npos : close - 1));
            if (uri && close != std::string_view::npos && destination.substr(close + 1).starts_with(':')) {
                split.port = std::string(destination.substr(close + 2));
            }
            return split;
        }
        const std::size_t colon = uri ? destination.find(':') : std::string_view::npos;
        split.host              = std::string(destination.substr(0, colon));
        if (colon != std::string_view::npos) {
            split.port = std::string(destination.substr(colon + 1));
        }
        return split;
    }

    // A Match line's criteria, all of which must hold. Only all, host and
    // originalhost can be told here; anything else doesn't match.
    bool MatchLineMatches(const std::vector<std::string>& arguments, std::string_view host) {
        if (arguments.empty()) {
            return false;
        }
        for (std::size_t i = 0; i < arguments.size(); ++i) {
            std::string criterion = Lower(arguments[i]);
            const bool  negated   = criterion.starts_with('!');
            if (negated) {
                criterion.erase(0, 1);
            }
            bool holds = false;
            if (criterion == "all") {
                holds = true;
            }
            else if ((criterion == "host" || criterion == "originalhost") && i + 1 < arguments.size()) {
                holds = PatternsMatch(CommaList(arguments[++i]), host);
            }
            else {
                return false;
            }
            if (holds == negated) {
                return false;
            }
        }
        return true;
    }

    void ReadConfig(const std::filesystem::path& file, const std::filesystem::path& includeBase, const std::filesystem::path& home, int depth,
                    Collected& collected) {
        if (depth > kMaxIncludeDepth) {
            return;
        }
        const std::optional<std::string> text = ReadFile(file);
        if (!text) {
            return;
        }
        SshConfigNames names = ParseSshConfig(*text, home);
        collected.hosts.insert(collected.hosts.end(), names.hosts.begin(), names.hosts.end());
        collected.knownHostsFiles.insert(collected.knownHostsFiles.end(), names.knownHostsFiles.begin(), names.knownHostsFiles.end());
        for (const std::string& include : names.includes) {
            const std::filesystem::path pattern = std::filesystem::path(include).is_absolute() ? std::filesystem::path(include) : includeBase / include;
            for (const std::filesystem::path& included : Glob(pattern.string())) {
                ReadConfig(included, includeBase, home, depth + 1, collected);
            }
        }
    }

} // namespace

SshConfigNames ParseSshConfig(std::string_view text, const std::filesystem::path& home) {
    SshConfigNames names;
    for (const std::string_view line : Lines(text)) {
        const std::optional<std::pair<std::string, std::vector<std::string>>> split = SplitLine(line);
        if (!split) {
            continue;
        }
        const auto& [keyword, arguments] = *split;
        if (keyword == "host") {
            for (const std::string& name : arguments) {
                if (!IsPattern(name)) {
                    names.hosts.push_back(name);
                }
            }
        }
        else if (keyword == "userknownhostsfile" || keyword == "globalknownhostsfile") {
            for (const std::string& path : arguments) {
                if (Lower(path) == "none") {
                    continue;
                }
                if (std::optional<std::filesystem::path> expanded = ExpandPath(path, home)) {
                    names.knownHostsFiles.push_back(std::move(*expanded));
                }
            }
        }
        else if (keyword == "include") {
            for (const std::string& path : arguments) {
                if (std::optional<std::filesystem::path> expanded = ExpandPath(path, home)) {
                    names.includes.push_back(expanded->string());
                }
            }
        }
    }
    return names;
}

std::vector<std::string> ParseKnownHosts(std::string_view text) {
    std::vector<std::string> hosts;
    for (std::string_view line : Lines(text)) {
        std::vector<std::string> fields = Words(Trim(line));
        if (!fields.empty() && fields.front().starts_with('@')) {
            if (fields.front() == "@revoked") {
                continue;
            }
            fields.erase(fields.begin());
        }
        if (fields.empty()) {
            continue;
        }
        std::vector<std::string> names;
        std::vector<std::string> addresses;
        std::string_view         list = fields.front();
        while (!list.empty()) {
            const std::size_t comma = list.find(',');
            std::string_view  entry = list.substr(0, comma);
            list                    = comma == std::string_view::npos ? std::string_view() : list.substr(comma + 1);
            if (entry.empty() || entry.starts_with('|') || IsPattern(entry)) {
                continue;
            }
            std::string name(entry);
            if (entry.starts_with('[')) {
                const std::size_t close = entry.find("]:");
                if (close == std::string_view::npos) {
                    continue;
                }
                const std::string_view host = entry.substr(1, close - 1);
                const bool             v6   = host.find(':') != std::string_view::npos;
                name                        = text::NormalizeDestination(std::string(text::kSshScheme) + (v6 ? "[" + std::string(host) + "]" : std::string(host)) +
                                                                         ":" + std::string(entry.substr(close + 2)));
                (IsAddress(std::string(host)) ? addresses : names).push_back(std::move(name));
                continue;
            }
            (IsAddress(name) ? addresses : names).push_back(std::move(name));
        }
        const std::vector<std::string>& chosen = names.empty() ? addresses : names;
        hosts.insert(hosts.end(), chosen.begin(), chosen.end());
    }
    return hosts;
}

std::vector<std::string> ParseSearchDomains(std::string_view text) {
    std::vector<std::string> domains;
    for (std::string_view line : Lines(text)) {
        line = Trim(line);
        if (line.empty() || line.front() == '#' || line.front() == ';') {
            continue;
        }
        std::vector<std::string> words = Words(line.substr(0, line.find(';')));
        if (words.empty() || (words.front() != "search" && words.front() != "domain")) {
            continue;
        }
        if (words.front() == "domain") {
            words.resize(std::min<std::size_t>(words.size(), 2));
        }
        domains.clear();
        for (auto word = words.begin() + 1; word != words.end(); ++word) {
            while (word->ends_with('.')) {
                word->pop_back();
            }
            if (!word->empty()) {
                domains.push_back(std::move(*word));
            }
        }
    }
    return domains;
}

SshConfig SshConfig::Load(const HostSources& sources) {
    SshConfig config;
    for (const auto& [file, base] : {std::pair{sources.userConfig, sources.home / ".ssh"}, std::pair{sources.systemConfig, sources.systemConfig.parent_path()}}) {
        if (file.empty()) {
            continue;
        }
        if (const std::optional<std::string> text = ReadFile(file)) {
            config.Append(*text, sources.home, base, 0);
        }
    }
    return config;
}

SshConfig SshConfig::Parse(std::string_view text, const std::filesystem::path& home, const std::filesystem::path& includeBase) {
    SshConfig config;
    config.Append(text, home, includeBase, 0);
    return config;
}

void SshConfig::Append(std::string_view text, const std::filesystem::path& home, const std::filesystem::path& includeBase, int depth) {
    for (const std::string_view line : Lines(text)) {
        std::optional<std::pair<std::string, std::vector<std::string>>> split = SplitLine(line);
        if (!split) {
            continue;
        }
        auto& [keyword, arguments] = *split;
        if (keyword == "include") {
            if (depth >= kMaxIncludeDepth) {
                continue;
            }
            for (const std::string& argument : arguments) {
                const std::optional<std::filesystem::path> expanded = ExpandPath(argument, home);
                if (!expanded) {
                    continue;
                }
                const std::filesystem::path pattern = expanded->is_absolute() ? *expanded : includeBase / *expanded;
                for (const std::filesystem::path& included : Glob(pattern.string())) {
                    if (const std::optional<std::string> body = ReadFile(included)) {
                        entries_.push_back(Entry{.kind = Entry::Kind::Enter, .keyword = {}, .arguments = {}});
                        Append(*body, home, includeBase, depth + 1);
                        entries_.push_back(Entry{.kind = Entry::Kind::Leave, .keyword = {}, .arguments = {}});
                    }
                }
            }
            continue;
        }
        const Entry::Kind kind = keyword == "host" ? Entry::Kind::Host : keyword == "match" ? Entry::Kind::Match
                                                                                            : Entry::Kind::Setting;
        entries_.push_back(Entry{.kind = kind, .keyword = std::move(keyword), .arguments = std::move(arguments)});
    }
}

SshTarget SshConfig::Resolve(std::string_view destination) const {
    const Destination given = SplitDestination(destination);
    // An Include inside a block that doesn't apply never applies either,
    // whatever its own blocks say.
    struct Scope {
        bool active = true;
        bool never  = false;
    };
    std::vector<Scope>                 scopes{Scope{}};
    std::optional<std::string>         hostName;
    std::optional<std::string>         user;
    std::optional<std::string>         port;
    std::optional<std::string>         proxyJump;
    std::vector<std::filesystem::path> identityFiles;
    const auto                         first = [](std::optional<std::string>& slot, const std::vector<std::string>& arguments) {
        if (!slot && !arguments.empty()) {
            slot = arguments.front();
        }
    };
    for (const Entry& entry : entries_) {
        switch (entry.kind) {
            case Entry::Kind::Enter:
                scopes.push_back(Scope{.active = scopes.back().active, .never = !scopes.back().active});
                break;
            case Entry::Kind::Leave:
                scopes.pop_back();
                break;
            case Entry::Kind::Host:
                scopes.back().active = !scopes.back().never && PatternsMatch(entry.arguments, given.host);
                break;
            case Entry::Kind::Match:
                scopes.back().active = !scopes.back().never && MatchLineMatches(entry.arguments, given.host);
                break;
            case Entry::Kind::Setting:
                if (!scopes.back().active) {
                    break;
                }
                if (entry.keyword == "hostname") {
                    first(hostName, entry.arguments);
                }
                else if (entry.keyword == "user") {
                    first(user, entry.arguments);
                }
                else if (entry.keyword == "port") {
                    first(port, entry.arguments);
                }
                else if (entry.keyword == "proxyjump") {
                    first(proxyJump, entry.arguments);
                }
                else if (entry.keyword == "identityfile" && !entry.arguments.empty()) {
                    identityFiles.emplace_back(entry.arguments.front());
                }
                break;
        }
    }
    SshTarget target;
    // HostName's %h is the host as given.
    const std::string named = hostName.value_or(given.host);
    for (std::size_t i = 0; i < named.size(); ++i) {
        if (named[i] == '%' && i + 1 < named.size() && (named[i + 1] == 'h' || named[i + 1] == '%')) {
            target.hostName += named[++i] == 'h' ? given.host : "%";
        }
        else {
            target.hostName += named[i];
        }
    }
    target.user          = given.user ? given.user : user;
    target.port          = given.port.value_or(port.value_or("22"));
    target.identityFiles = std::move(identityFiles);
    if (proxyJump && Lower(*proxyJump) != "none") {
        target.proxyJump = proxyJump;
    }
    return target;
}

std::string SshConfig::Describe(std::string_view destination) const {
    const Destination given  = SplitDestination(destination);
    const SshTarget   target = Resolve(destination);
    const bool        v6     = target.hostName.find(':') != std::string::npos;
    std::string       where  = (target.user ? *target.user + "@" : std::string()) + (v6 && target.port != "22" ? "[" + target.hostName + "]" : target.hostName) +
                               (target.port != "22" ? ":" + target.port : std::string());
    if (target.hostName == given.host && target.user == given.user && target.port == given.port.value_or("22") && !target.proxyJump) {
        return {};
    }
    if (target.proxyJump) {
        where += " via " + *target.proxyJump;
    }
    return where;
}

HostSources DefaultHostSources() {
    const char*                 home = std::getenv("HOME");
    const std::filesystem::path homeDir(home != nullptr ? home : "");
    HostSources                 sources{.home = homeDir, .userConfig = {}, .systemConfig = "/etc/ssh/ssh_config", .knownHosts = {}};
    if (!homeDir.empty()) {
        sources.userConfig = homeDir / ".ssh" / "config";
        sources.knownHosts = {homeDir / ".ssh" / "known_hosts", homeDir / ".ssh" / "known_hosts2"};
    }
    sources.knownHosts.emplace_back("/etc/ssh/ssh_known_hosts");
    sources.knownHosts.emplace_back("/etc/ssh/ssh_known_hosts2");
    return sources;
}

std::vector<std::string> KnownHosts(const HostSources& sources) {
    Collected collected;
    if (!sources.userConfig.empty()) {
        ReadConfig(sources.userConfig, sources.home / ".ssh", sources.home, 0, collected);
    }
    Collected system;
    if (!sources.systemConfig.empty()) {
        ReadConfig(sources.systemConfig, sources.systemConfig.parent_path(), sources.home, 0, system);
    }

    std::vector<std::filesystem::path> files = sources.knownHosts;
    std::vector<std::filesystem::path> named = collected.knownHostsFiles;
    named.insert(named.end(), system.knownHostsFiles.begin(), system.knownHostsFiles.end());
    for (const std::filesystem::path& file : named) {
        if (std::ranges::find(files, file) == files.end()) {
            files.push_back(file);
        }
    }
    // Where the config's own names lead on other ports, so known_hosts
    // doesn't offer an ssh URI for a machine a config name already reaches.
    const SshConfig                               config = SshConfig::Load(sources);
    std::set<std::pair<std::string, std::string>> covered;
    for (const std::vector<std::string>* names : {&collected.hosts, &system.hosts}) {
        for (const std::string& name : *names) {
            if (const SshTarget target = config.Resolve(name); target.port != "22") {
                covered.emplace(Lower(target.hostName), target.port);
            }
        }
    }
    for (const std::filesystem::path& file : files) {
        if (const std::optional<std::string> text = ReadFile(file)) {
            for (std::string& host : ParseKnownHosts(*text)) {
                const Destination split = SplitDestination(host);
                if (!split.port || !covered.contains({Lower(split.host), *split.port})) {
                    collected.hosts.push_back(std::move(host));
                }
            }
        }
    }
    // Last: a system config's hosts are mostly the distribution's own.
    collected.hosts.insert(collected.hosts.end(), system.hosts.begin(), system.hosts.end());

    std::vector<std::string> unique;
    std::set<std::string>    seen;
    for (std::string& host : collected.hosts) {
        if (seen.insert(Lower(host)).second) {
            unique.push_back(std::move(host));
        }
    }
    return unique;
}

std::vector<std::string> MatchHosts(const std::vector<std::string>& hosts, std::string_view typed) {
    std::vector<std::pair<int, const std::string*>> scored;
    for (const std::string& host : hosts) {
        if (const std::optional<int> score = FuzzyScore(host, typed)) {
            scored.emplace_back(*score, &host);
        }
    }
    std::ranges::stable_sort(scored, std::ranges::greater(), &std::pair<int, const std::string*>::first);
    std::vector<std::string> matched;
    matched.reserve(scored.size());
    for (const auto& [score, host] : scored) {
        matched.push_back(*host);
    }
    return matched;
}

} // namespace ned::editor::remote
