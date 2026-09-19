#include "Manager.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>

#include <fnmatch.h>
#include <unistd.h>

#include "Editor/BackgroundActivity.h"
#include "Editor/Project/Root.h"
#include "Editor/Project/Settings.h"
#include "Editor/TabWidth.h"
#include "BrokerConnect.h"
#include "Position.h"
#include "RootResolver.h"
#include "ServerConfig.h"
#include "ProseChecker.h"
#include "Text/BinaryDetect.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/Rope.h"
#include "Text/RopeStorage.h"
#include "Text/Utf8.h"

namespace ned::editor::lsp {

namespace {

    // crash-loop-respawn-guard follow-up: see Manager.h's disconnectBurst_
    // doc comment for why this exists at all. Thresholds, not Janet-
    // configurable -- proportionate to closing a real resource-exhaustion
    // bug, not a tunable feature.
    constexpr std::chrono::milliseconds kCrashLoopWindow{3000};
    constexpr int                       kCrashLoopThreshold = 3;

    // respawn-debounce follow-up: see ClientForLanguage's own doc comment on
    // lastDisconnectAt_ for why this exists alongside (not instead of) the
    // crash-loop guard above -- breathing room for each individual retry,
    // not just a cap on the total. 3 * kRespawnCooldown comfortably fits
    // inside kCrashLoopWindow, so a real crash-looping server still reaches
    // the giveup threshold, just no longer within the same video frame.
    constexpr std::chrono::milliseconds kRespawnCooldown{1000};

    // v1: no percent-encoding of special characters in the path -- every
    // path this touches (an open Buffer's own Path(), editor::ProjectRoot())
    // is already a real filesystem path this process itself resolved, not
    // untrusted input, so the common case (no space/unicode-heavy path)
    // round-trips correctly; a path containing characters that need real
    // percent-encoding is a known, documented gap, not silently assumed away.
    std::string PathToUri(const std::filesystem::path& path) {
        // Absolutized here rather than assumed: a buffer opened via a
        // relative CLI argument (`ned demo.cpp`) keeps that relative Path(),
        // and "file://demo.cpp" is unresolvable to a server -- clangd
        // rejected every request for such a buffer ("failed to decode ...
        // unresolvable URI"), found live while verifying the
        // codeActionLiteralSupport fix, not in review. The error_code
        // overload, not the throwing one: this runs under Paint() with no
        // catch anywhere above it, and absolute() throws for an empty path
        // (and when the cwd is gone) -- a degraded URI beats aborting the
        // whole editor (a real SIGABRT from a core dump, not hypothetical).
        std::error_code             ec;
        const std::filesystem::path absolute = std::filesystem::absolute(path, ec);
        return "file://" + (ec ? path : absolute).lexically_normal().string();
    }

    std::optional<std::filesystem::path> UriToPath(const std::string& uri) {
        constexpr std::string_view kPrefix = "file://";
        if (uri.rfind(kPrefix, 0) != 0) {
            return std::nullopt;
        }
        // documentLink follow-up: percent-decode. A file: URI's path is
        // percent-encoded per RFC 3986, and real servers do encode it --
        // found live, not assumed: clangd reports every system include's
        // target under this machine's own libstdc++ directory as
        // ".../g%2B%2B-v16/algorithm", which as a literal path exists
        // nowhere. Undecoded, every URI-carrying response (definition,
        // references, rename, documentLink) silently missed any path
        // containing a character outside the unreserved set. A stray '%'
        // that isn't followed by two hex digits is kept verbatim rather
        // than treated as a parse failure -- a filename may legitimately
        // contain one.
        const std::string encoded = uri.substr(kPrefix.size());
        std::string       decoded;
        decoded.reserve(encoded.size());
        for (std::size_t i = 0; i < encoded.size(); ++i) {
            const auto isHex = [](char c) {
                return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
            };
            if (encoded[i] == '%' && i + 2 < encoded.size() && isHex(encoded[i + 1]) && isHex(encoded[i + 2])) {
                decoded.push_back(static_cast<char>(std::stoi(encoded.substr(i + 1, 2), nullptr, 16)));
                i += 2;
                continue;
            }
            decoded.push_back(encoded[i]);
        }
        return std::filesystem::path(decoded);
    }

    // project-settings-lsp-init-options follow-up. Resolves a dotted "section"
    // path (e.g. "phpactor", "intelephense.environment" -- exactly the shape a
    // real workspace/configuration request item's own "section" field takes) into
    // tree, walking one object level per '.'-separated segment. Returns JSON null
    // ("no client-side override, use your own defaults") the instant a segment is
    // missing or the tree isn't an object at that point -- the same fallback
    // WireNotificationHandlers' workspace/configuration handler already used
    // before any lspWorkspaceConfiguration existed to resolve against.
    Json ResolveConfigurationSection(const Json& tree, const std::string& section) {
        const Json* current = &tree;
        std::size_t start   = 0;
        while (true) {
            const std::size_t dot     = section.find('.', start);
            const std::string segment = section.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
            if (!current->is_object() || !current->contains(segment)) {
                return Json(nullptr);
            }
            current = &current->at(segment);
            if (dot == std::string::npos) {
                return *current;
            }
            start = dot + 1;
        }
    }

    text::Buffer::Diagnostic::Severity SeverityFromLsp(int severity) {
        switch (severity) {
            case 1:
                return text::Buffer::Diagnostic::Severity::Error;
            case 2:
                return text::Buffer::Diagnostic::Severity::Warning;
            case 3:
                return text::Buffer::Diagnostic::Severity::Information;
            case 4:
                return text::Buffer::Diagnostic::Severity::Hint;
            default:
                return text::Buffer::Diagnostic::Severity::Information; // an unrecognized/missing severity -- a safe, visible-but-not-alarming default
        }
    }

    // prose-check-composer follow-up: factored out of HandlePublishDiagnostics
    // so its composer-pseudo-buffer special case (below) can parse a
    // "textDocument/publishDiagnostics" params object the exact same way the
    // real-buffer path does, against content that isn't a real, open Buffer.
    std::vector<text::Buffer::Diagnostic> ParsePublishedDiagnostics(const Json& params, const text::ITextStorage& content,
                                                                    text::Buffer::Diagnostic::Origin origin) {
        std::vector<text::Buffer::Diagnostic> diagnostics;
        if (!params.contains("diagnostics")) {
            return diagnostics;
        }
        for (const Json& item : params["diagnostics"]) {
            const Json& range = item.value("range", Json::object());
            const Json& start = range.value("start", Json::object());
            const Json& end   = range.value("end", Json::object());

            const std::size_t startByte =
                PositionToByte(content, Position{.line      = start.value("line", static_cast<std::size_t>(0)),
                                                       .character = start.value("character", static_cast<std::size_t>(0))});
            const std::size_t endByte =
                PositionToByte(content, Position{.line      = end.value("line", static_cast<std::size_t>(0)),
                                                       .character = end.value("character", static_cast<std::size_t>(0))});

            diagnostics.push_back(text::Buffer::Diagnostic{
                .startByte = startByte,
                .endByte   = endByte,
                .severity  = SeverityFromLsp(item.value("severity", 3)),
                .origin    = origin,
                .message   = item.value("message", std::string()),
            });
        }
        return diagnostics;
    }

    // prose-check-composer follow-up: a fixed, private path identifying the
    // composer's pseudo-document to the prose-checker connection -- never
    // read or written on disk (SyncTextToServer sends `text` as didOpen/
    // didChange content directly), just a stable identity PathToUri/UriToPath
    // can round-trip and HandlePublishDiagnostics can recognize. Deliberately
    // not under $XDG_STATE_HOME/ned (every other path this codebase resolves
    // there is real, persisted state) -- temp_directory_path with a "/tmp"
    // fallback keeps this obviously scratch, and avoids the XDG resolution's
    // own "throws if neither XDG_STATE_HOME nor HOME is set" failure mode for
    // something that was never going to touch disk anyway.
    const std::filesystem::path& ComposerProseScratchPath() {
        static const std::filesystem::path path = [] {
            std::error_code       ec;
            std::filesystem::path dir = std::filesystem::temp_directory_path(ec);
            if (ec || dir.empty()) {
                dir = "/tmp";
            }
            return dir / "ned-acp-composer-prose-scratch.md";
        }();
        return path;
    }

    // code-actions follow-up: the reverse of SeverityFromLsp, for building a
    // textDocument/codeAction request's own "context.diagnostics" -- the
    // server expects real LSP Diagnostic shapes back, not this codebase's
    // internal Buffer::Diagnostic::Severity enum.
    int SeverityToLsp(text::Buffer::Diagnostic::Severity severity) {
        switch (severity) {
            case text::Buffer::Diagnostic::Severity::Error:
                return 1;
            case text::Buffer::Diagnostic::Severity::Warning:
                return 2;
            case text::Buffer::Diagnostic::Severity::Information:
                return 3;
            case text::Buffer::Diagnostic::Severity::Hint:
                return 4;
        }
        return 3; // unreachable for a real enum value -- Information is the same safe default SeverityFromLsp uses
    }

    // semanticTokens follow-up. Maps one of the LSP spec's standard
    // SemanticTokenTypes strings onto an existing editor::SyntaxClass --
    // reusing the same curated set tree-sitter highlighting already
    // populates rather than adding a new axis to HighlightSpan (see
    // ROADMAP.md/the plan this follows for why). nullopt for a token type
    // with no sensible existing class (dropped by the caller, not
    // force-fit) -- "event" and "unknown" (a real type clangd itself
    // emits) are the two standard-or-observed-in-practice types left
    // unmapped. Deliberately ignores tokenModifiers -- a v1 scope cut, not
    // a bug: a "readonly"/"static"/etc. refinement is a real future
    // improvement, not required for the base feature to be useful.
    std::optional<editor::SyntaxClass> SyntaxClassForSemanticTokenType(const std::string& tokenType) {
        static const std::unordered_map<std::string, editor::SyntaxClass> kMapping = {
            {"namespace", editor::SyntaxClass::Namespace},
            {"class", editor::SyntaxClass::Type},
            {"enum", editor::SyntaxClass::Type},
            {"interface", editor::SyntaxClass::Type},
            {"struct", editor::SyntaxClass::Type},
            {"type", editor::SyntaxClass::Type},
            {"typeParameter", editor::SyntaxClass::Type},
            {"parameter", editor::SyntaxClass::Parameter},
            {"variable", editor::SyntaxClass::Variable},
            {"property", editor::SyntaxClass::Property},
            {"enumMember", editor::SyntaxClass::Constant},
            {"function", editor::SyntaxClass::Function},
            {"method", editor::SyntaxClass::Method},
            {"macro", editor::SyntaxClass::FunctionBuiltin},
            {"keyword", editor::SyntaxClass::Keyword},
            {"modifier", editor::SyntaxClass::KeywordModifier},
            {"comment", editor::SyntaxClass::Comment},
            {"string", editor::SyntaxClass::String},
            {"number", editor::SyntaxClass::Number},
            {"regexp", editor::SyntaxClass::String},
            {"operator", editor::SyntaxClass::Operator},
            {"decorator", editor::SyntaxClass::Attribute},
            {"label", editor::SyntaxClass::Label},
        };
        const auto it = kMapping.find(tokenType);
        return it != kMapping.end() ? std::optional(it->second) : std::nullopt;
    }

    // error-visibility follow-up. No existing timestamp-formatting
    // convention exists anywhere else in this codebase (confirmed via
    // search) -- this is a small, self-contained, file-local helper, not
    // something sharing a home with anything else.
    std::string FormatLogLine(std::string_view language, std::string_view message) {
        const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm           localNow{};
        localtime_r(&now, &localNow);
        char timestamp[16];
        std::strftime(timestamp, sizeof(timestamp), "%H:%M:%S", &localNow);
        return "[" + std::string(timestamp) + "] " + std::string(language) + ": " + std::string(message) + "\n";
    }

    // error-visibility follow-up. Extracts a human-readable string from a
    // JSON-RPC error object -- "message" per the spec if present, else the
    // raw JSON so nothing is ever silently dropped even for a
    // spec-noncompliant server.
    std::string ExtractErrorMessage(const Json& error) {
        return error.value("message", error.dump());
    }

    // rename-file-notifications follow-up. Matches path against every glob
    // in globs via POSIX fnmatch with no flags -- without FNM_PATHNAME, '*'
    // matches '/' too, which is what makes a "**/*.ts"-shaped LSP filter
    // (rust-analyzer/typescript-language-server's own convention) actually
    // match a nested path the way a real "**" component would: this isn't a
    // literal globstar implementation, but the redundant leading "**"
    // collapses to an ordinary "*" under fnmatch's rules and produces the
    // same match here regardless. A reasonable v1 approximation of LSP's
    // glob grammar, not a complete implementation of it (no brace
    // expansion, no character-class edge cases beyond what fnmatch itself
    // supports).
    bool MatchesFileOperationGlob(const std::filesystem::path& path, const std::vector<std::string>& globs) {
        const std::string pathStr = path.string();
        return std::any_of(globs.begin(), globs.end(),
                           [&pathStr](const std::string& glob) { return ::fnmatch(glob.c_str(), pathStr.c_str(), 0) == 0; });
    }

    // background-activity-spinner follow-up: same std::string materialization
    // of the shared constant Client.cpp's own copy makes.
    const std::string kLspActivity{kLspActivityName};

    Json DiagnosticToLsp(const text::Buffer::Diagnostic& diagnostic, const text::ITextStorage& content) {
        const Position start = BytePositionToLsp(content, diagnostic.startByte);
        const Position end   = BytePositionToLsp(content, diagnostic.endByte);
        return Json{
            {"range", {{"start", {{"line", start.line}, {"character", start.character}}}, {"end", {{"line", end.line}, {"character", end.character}}}}},
            {"severity", SeverityToLsp(diagnostic.severity)},
            {"message", diagnostic.message},
        };
    }

    // lsp-workspace-folders follow-up. One LSP WorkspaceFolder object.
    // `name` is purely a human-readable label per spec (servers key on the
    // uri); the directory's own basename is what every other client sends,
    // falling back to the full path string for a root with no filename
    // component of its own (e.g. "/").
    Json WorkspaceFolderEntry(const std::filesystem::path& root) {
        const std::string name = root.filename().empty() ? root.string() : root.filename().string();
        return Json{{"uri", PathToUri(root)}, {"name", name}};
    }

    // Is [start, end) entirely inside a sorted, disjoint, merged range list?
    bool RangeIsCovered(const std::vector<std::pair<std::size_t, std::size_t>>& ranges, std::size_t start, std::size_t end) {
        if (start >= end) {
            return true; // an empty range asks nothing
        }
        for (const auto& range : ranges) {
            if (range.first > start) {
                return false; // a gap before the next covered range
            }
            if (range.second >= end) {
                return true;
            }
            if (range.second > start) {
                start = range.second; // partial cover; keep walking from here
            }
        }
        return false;
    }

    // Adds [start, end), keeping the list sorted, disjoint and merged.
    void AddCoveredRange(std::vector<std::pair<std::size_t, std::size_t>>& ranges, std::size_t start, std::size_t end) {
        if (start >= end) {
            return;
        }
        std::vector<std::pair<std::size_t, std::size_t>> merged;
        merged.reserve(ranges.size() + 1);
        for (const auto& range : ranges) {
            if (range.second < start || range.first > end) {
                merged.push_back(range); // disjoint, and not even touching
                continue;
            }
            start = std::min(start, range.first); // overlaps or abuts -- absorb it
            end   = std::max(end, range.second);
        }
        merged.emplace_back(start, end);
        std::sort(merged.begin(), merged.end());
        ranges = std::move(merged);
    }

} // namespace

Json BuildInitializeParams(const std::filesystem::path& projectRoot, const Json& initializationOptions) {
    // codeActionLiteralSupport is load-bearing, not boilerplate: per the LSP
    // spec a server may only return edit-carrying CodeAction literals to a
    // client that advertises it, and must fall back to bare Command objects
    // otherwise (executeCommand follow-up: now runnable via
    // Manager::ExecuteCommand/workspace/executeCommand, but a client
    // that doesn't advertise this still gets the plain-Command fallback
    // form regardless). clangd honors codeActionLiteralSupport exactly --
    // without it, its "fix available" quickfixes (e.g. "remove #include
    // directive") arrived as Commands with no "edit", and applying one
    // reported "has no edit to apply". Confirmed against a real clangd 22
    // session both ways, not inferred from the spec alone.
    //
    // dataSupport/resolveSupport (code-actions-resolve follow-up) advertise
    // that this client will call codeAction/resolve for a CodeAction sent
    // back without an "edit" -- see ResolveCodeAction.
    //
    // window.workDoneProgress (workDoneProgress-support follow-up) invites
    // "$/progress" reporting -- server-side busy state (clangd's background
    // indexing) for the mode-line spinner; see HandleProgress.
    // An empty root becomes rootUri: null (explicitly allowed by the LSP
    // spec -- "rootUri: DocumentUri | null") rather than a nonsense
    // "file://" URI: ProjectRoot() should never be empty anymore
    // (DetectProjectRoot absolutizes now), but this handshake runs under
    // Paint() with no catch above it, so it must stay total regardless.
    // lsp-workspace-folders follow-up: workspaceFolders is sent alongside
    // (not instead of) the deprecated rootUri -- the spec says a client
    // supporting both should send both, and a server that only understands
    // rootUri keeps working byte-for-byte as before. null, not an empty
    // array, for an empty root: an empty array means "no folders open,"
    // which is a different claim than "this client doesn't have one."
    Json params = Json{
        {"processId", static_cast<std::int64_t>(::getpid())},
        {"rootUri", projectRoot.empty() ? Json(nullptr) : Json(PathToUri(projectRoot))},
        {"workspaceFolders", projectRoot.empty() ? Json(nullptr) : Json::array({WorkspaceFolderEntry(projectRoot)})},
        {"capabilities",
         {{"textDocument",
           // completionItem.snippetSupport (snippet-expansion follow-up) is
           // load-bearing the same way codeActionLiteralSupport below is:
           // per the spec a server may only send insertTextFormat: 2
           // (snippet-syntax) items to a client advertising this, so
           // without it a conforming server (clangd's function-argument
           // completions, rust-analyzer, tsserver) never sends the snippet
           // form at all -- the accept path expands them via
           // Editor/Snippet.h into a real tabstop session.
           //
           // capabilities-hygiene follow-up: hover/definition/declaration/
           // typeDefinition/implementation/references/rename/signatureHelp/
           // publishDiagnostics are all requests or notifications this
           // client already sends/handles (see Manager's own Request*
           // methods and HandlePublishDiagnostics) but never previously
           // declared -- bare {} advertises plain support with no optional
           // refinement (no tagSupport/relatedInformation on
           // publishDiagnostics since neither is parsed). Harmless against a
           // permissive server (confirmed live against clangd either way),
           // but a capability-strict one is entitled to assume a client that
           // never declares e.g. "rename" doesn't want rename requests at
           // all.
           //
           // prepareRename/linkedEditingRange follow-up: rename now declares
           // prepareSupport (RequestPrepareRename is sent, see that method's
           // own doc comment), and linkedEditingRange is declared bare, the
           // same "plain support, no optional refinement" shape as
           // documentHighlight just below it.
           // completion-resolve/completion-trigger-characters follow-up:
           // resolveSupport names exactly the three fields
           // CompletionSession::ApplyResolution will merge back in, and is
           // what makes rust-analyzer/jdtls defer their additionalTextEdits
           // (the "#include"/"import" an accepted symbol needs) to
           // completionItem/resolve rather than dropping them.
           // commitCharactersSupport/preselectSupport are the sibling
           // "this client actually reads that field" declarations for the
           // other two the parser now keeps -- a server is entitled to omit
           // both from a client that never claims them.
           {{"completion",
             {{"completionItem",
               {{"snippetSupport", true},
                {"preselectSupport", true},
                {"commitCharactersSupport", true},
                {"resolveSupport", {{"properties", Json::array({"documentation", "detail", "additionalTextEdits"})}}}}},
              {"contextSupport", true}}},
            {"hover", Json::object()},
            {"signatureHelp", Json::object()},
            {"declaration", Json::object()},
            {"definition", Json::object()},
            {"typeDefinition", Json::object()},
            {"implementation", Json::object()},
            {"references", Json::object()},
            {"documentHighlight", Json::object()},
            {"linkedEditingRange", Json::object()},
            {"rename", {{"prepareSupport", true}}},
            {"formatting", Json::object()},
            {"rangeFormatting", Json::object()},
            {"onTypeFormatting", Json::object()},
            // semanticTokens follow-up, extended by the range/delta
            // follow-up: tokenTypes/tokenModifiers here are spec-required
            // but purely informational (the client's own decode is
            // index-based against the server's own legend, not filtered
            // against this list) -- declares every standard type
            // SyntaxClassForSemanticTokenType actually maps, so a
            // capability-strict server has no reason to omit any of them
            // from its own legend. requests.full advertises delta support
            // (the object form, not a bare true -- RequestSemanticTokens
            // only ever sends a full/delta request once
            // SemanticTokensLegend::fullDeltaSupported says the server
            // reciprocated in its own semanticTokensProvider.full);
            // requests.range is declared too. formats always ["relative"],
            // the only value the spec defines.
            {"semanticTokens",
             {{"requests", {{"full", {{"delta", true}}}, {"range", true}}},
              {"tokenTypes", Json::array({"namespace", "class", "enum", "interface", "struct", "type", "typeParameter",
                                          "parameter", "variable", "property", "enumMember", "function", "method", "macro",
                                          "keyword", "modifier", "comment", "string", "number", "regexp", "operator",
                                          "decorator", "label"})},
              {"tokenModifiers", Json::array()},
              {"formats", Json::array({"relative"})}}},
            {"inlayHint", Json::object()},
            {"codeLens", Json::object()},
            {"publishDiagnostics", Json::object()},
            {"codeAction",
             {{"codeActionLiteralSupport",
               {{"codeActionKind",
                 {{"valueSet", Json::array({"", "quickfix", "refactor", "refactor.extract", "refactor.inline", "refactor.rewrite",
                                            "source", "source.organizeImports", "source.fixAll"})}}}}},
              {"dataSupport", true},
              {"resolveSupport", {{"properties", Json::array({"edit"})}}}}},
            // call/type-hierarchy follow-up: both are bare {} like every
            // other capabilities-hygiene entry above -- no optional
            // refinement (no "dynamicRegistration") this client needs.
            {"callHierarchy", Json::object()},
            {"typeHierarchy", Json::object()}}},
          // capabilities-hygiene follow-up: workspace/configuration and
          // workspace/executeCommand are both handled/sent (see
          // WireNotificationHandlers/ExecuteCommand) but this object
          // previously had no "workspace" key at all -- "configuration" is a
          // bare boolean per spec, unlike every textDocument.* entry above.
          // edit-application-gaps follow-up: "applyEdit" advertises this
          // client now handles a server-pushed workspace/applyEdit request
          // (see WireNotificationHandlers' own handler for it);
          // "workspaceEdit.documentChanges" tells a server it's safe to send
          // the richer "documentChanges" WorkspaceEdit form (file
          // create/rename/delete, not just edits to existing files) rather
          // than staying on the plain "changes" map some servers default to
          // for an undeclared client.
          // rename-file-notifications follow-up: unlike every other
          // capabilities-hygiene entry above, FileOperationClientCapabilities'
          // fields are plain booleans per spec (not objects) -- confirmed the
          // hard way: harper-ls's serde-based parser rejects an object here
          // with "invalid type: map, expected a boolean" and fails
          // initialize outright, where clangd silently tolerated the
          // malformed {} shape. `true` just means "no dynamicRegistration
          // needed," the same thing bare {} means for every boolean-typed
          // sibling above.
          {"workspace",
           {{"applyEdit", true},
            {"workspaceEdit", {{"documentChanges", true}}},
            {"configuration", true},
            {"didChangeConfiguration", Json::object()},
            {"executeCommand", Json::object()},
            // lsp-workspace-folders follow-up: a plain boolean per spec (like
            // applyEdit/configuration above, unlike the object-shaped
            // textDocument.* entries) -- declares this client can both be
            // handed several folders at initialize time and send
            // workspace/didChangeWorkspaceFolders afterwards. Without it a
            // conforming server has no reason to advertise its own
            // workspaceFolders support back, so Manager would never find a
            // connection worth joining.
            {"workspaceFolders", true},
            {"fileOperations", {{"willRename", true}, {"didRename", true}}}}},
          {"window", {{"workDoneProgress", true}}}}},
    };
    if (!initializationOptions.empty()) {
        params["initializationOptions"] = initializationOptions;
    }
    return params;
}

Manager::Manager(text::BufferList& bufferList, ned::ui::EventLoop& eventLoop) : bufferList_(bufferList), eventLoop_(eventLoop) {
}

Client* Manager::ExistingClientForLanguage(const std::string& language) const {
    const auto it = clients_.find(language);
    return it != clients_.end() ? it->second.get() : nullptr;
}

void Manager::WireNotificationHandlers(Client& client, const std::string& serverKey, const std::string& connectionKey,
                                          const Json& workspaceConfiguration) {
    client.SetNotificationHandler("textDocument/publishDiagnostics",
                                  [this, serverKey](const Json& params) { HandlePublishDiagnostics(params, serverKey); });
    // workDoneProgress-support follow-up: the create request just
    // establishes a token the following "$/progress" notifications carry --
    // there's nothing to decide, its result is null by spec; HandleProgress
    // tracks the token itself from the begin/end notifications rather than
    // from here, so an unsolicited-progress server (the spec explicitly
    // allows initiating progress without create) works identically.
    client.SetRequestHandler("window/workDoneProgress/create", [](const Json&) { return Json(nullptr); });
    // prose-checking follow-up, found live against a real harper-ls: a
    // config-pull server sends this right after "initialized" and, per spec,
    // expects one result array entry per requested "items" scope -- unlike
    // window/workDoneProgress/create (whose result is genuinely unused),
    // harper-ls does not proceed to check any document at all until this
    // gets a real (non-error) response. Before this handler existed, every
    // such request fell through to DispatchFrame's generic "method not
    // found" error response, and harper-ls silently never published a
    // single diagnostic for the rest of the connection's lifetime -- no
    // crash, no log entry, just permanent silence.
    //
    // project-settings-lsp-init-options follow-up: each requested item's
    // "section" is now resolved against workspaceConfiguration
    // (Editor/ProjectSettings.h's lspWorkspaceConfiguration) -- still null
    // ("no client-side override, use your own defaults", the standard
    // response VS Code's own client sends too) for any section nothing was
    // configured for, or when a request item carries no section at all.
    client.SetRequestHandler("workspace/configuration", [workspaceConfiguration](const Json& params) {
        if (!params.contains("items") || !params["items"].is_array()) {
            return Json(std::vector<Json>(1, Json(nullptr)));
        }
        std::vector<Json> results;
        results.reserve(params["items"].size());
        for (const Json& item : params["items"]) {
            const bool hasSection = item.contains("section") && item["section"].is_string();
            results.push_back(hasSection ? ResolveConfigurationSection(workspaceConfiguration, item["section"].get<std::string>())
                                         : Json(nullptr));
        }
        return Json(results);
    });
    // edit-application-gaps follow-up: see SetApplyEditHandler's own doc
    // comment. Per spec, "params.edit" is required and the response is
    // always {applied: bool, failureReason?: string} -- never an error
    // response, even on failure.
    client.SetRequestHandler("workspace/applyEdit", [this](const Json& params) -> Json {
        const auto editIt = params.find("edit");
        if (editIt == params.end()) {
            return Json{{"applied", false}, {"failureReason", "missing \"edit\""}};
        }
        const RenameResult parsed = ExtractRenameEdits(*editIt);
        if (parsed.touchesUnsupportedForm) {
            return Json{{"applied", false}, {"failureReason", "unsupported edit form"}};
        }
        ResolvedRename resolved;
        for (const RenameEdit& edit : parsed.edits) {
            const std::optional<std::filesystem::path> path = UriToPath(edit.uri);
            if (!path) {
                return Json{{"applied", false}, {"failureReason", "unresolvable uri"}};
            }
            resolved.edits.push_back(ResolvedRenameEdit{.path = *path, .edits = edit.edits});
        }
        if (!parsed.documentChangeOps.empty()) {
            const std::optional<std::vector<ResolvedDocumentChangeOp>> resolvedOps =
                ResolveDocumentChangeOps(parsed.documentChangeOps);
            if (!resolvedOps) {
                return Json{{"applied", false}, {"failureReason", "unresolvable uri"}};
            }
            resolved.documentChangeOps = std::move(*resolvedOps);
        }
        resolved.hasEdit = !resolved.edits.empty() || !resolved.documentChangeOps.empty();
        if (!resolved.hasEdit) {
            return Json{{"applied", false}, {"failureReason", "empty edit"}};
        }
        if (!applyEditHandler_) {
            return Json{{"applied", false}, {"failureReason", "not supported"}};
        }
        const std::string label = params.value("label", std::string("workspace edit"));
        return Json{{"applied", applyEditHandler_(resolved, label)}};
    });
    client.SetNotificationHandler("$/progress",
                                  [this, connectionKey](const Json& params) { HandleProgress(connectionKey, params); });
    client.SetOnDisconnected([this, serverKey, connectionKey](std::string reason) {
        LogError(serverKey, "server disconnected: " + reason);
        disconnectDetail_[connectionKey] = reason;
        ClientDisconnected(serverKey, connectionKey);
    });
}

std::string Manager::ConnectionKey(const std::filesystem::path& root, const std::string& serverKey) const {
    if (root == editor::ProjectRoot()) {
        return serverKey;
    }
    return root.string() + '\x1f' + serverKey;
}

std::string Manager::ConnectionKeyForBuffer(const text::Buffer& buffer, const std::string& serverKey) const {
    const auto it = bufferResolvedRoot_.find(const_cast<text::Buffer*>(&buffer));
    return ResolvedConnectionKey(it != bufferResolvedRoot_.end() ? it->second : editor::ProjectRoot(), serverKey);
}

std::string Manager::ResolvedConnectionKey(const std::filesystem::path& root, const std::string& serverKey) const {
    const std::string key = ConnectionKey(root, serverKey);
    const auto        it  = joinedConnection_.find(key);
    return it != joinedConnection_.end() ? it->second : key;
}

std::vector<std::string> Manager::ConnectionKeysForServer(const std::string& serverKey) const {
    const std::string        suffix = '\x1f' + serverKey;
    std::vector<std::string> keys;
    for (const auto& [key, client] : clients_) {
        if (key == serverKey || (key.size() > suffix.size() && key.compare(key.size() - suffix.size(), suffix.size(), suffix) == 0)) {
            keys.push_back(key);
        }
    }
    return keys;
}

std::optional<std::string> Manager::TryJoinWorkspaceFolder(const std::string& serverKey, const std::filesystem::path& root) {
    std::string joinable;
    for (const std::string& candidate : ConnectionKeysForServer(serverKey)) {
        if (handshakePending_.contains(candidate)) {
            return std::nullopt; // see this method's own doc comment -- wait, don't race
        }
        const auto supportIt = workspaceFoldersSupport_.find(candidate);
        if (supportIt != workspaceFoldersSupport_.end() && supportIt->second.supported && supportIt->second.changeNotifications &&
            joinable.empty()) {
            joinable = candidate;
        }
    }
    if (joinable.empty()) {
        return std::nullopt;
    }

    Client* client = ExistingClientForLanguage(joinable);
    if (!client) {
        return std::nullopt; // died between the scan above and here
    }
    client->SendNotification(
        "workspace/didChangeWorkspaceFolders",
        {{"event", {{"added", Json::array({WorkspaceFolderEntry(root)})}, {"removed", Json::array()}}}});
    connectionFolders_[joinable].push_back(root);
    joinedConnection_[ConnectionKey(root, serverKey)] = joinable;
    return joinable;
}

std::filesystem::path Manager::ResolveCachedRoot(const std::filesystem::path& bufferPath, const std::string& language) {
    const std::string cacheKey = bufferPath.parent_path().string() + '\x1f' + language;
    if (const auto it = resolvedRootCache_.find(cacheKey); it != resolvedRootCache_.end()) {
        return it->second;
    }
    return resolvedRootCache_.emplace(cacheKey, ResolveLspRoot(bufferPath, language)).first->second;
}

Client* Manager::ClientForLanguage(const std::string& serverKey, const std::filesystem::path& root) {
    // lsp-workspace-folders follow-up: resolved, not raw -- a root that
    // previously joined another process's folder set must keep landing on
    // that same client rather than spawning its own on the next sync.
    std::string connectionKey = ResolvedConnectionKey(root, serverKey);
    if (Client* existing = ExistingClientForLanguage(connectionKey)) {
        return existing;
    }
    if (connectionKey != ConnectionKey(root, serverKey)) {
        // The connection this root had joined is gone. Drop the stale
        // redirect and fall through to resolve fresh below -- it may join a
        // different survivor, or spawn its own.
        joinedConnection_.erase(ConnectionKey(root, serverKey));
        connectionKey = ConnectionKey(root, serverKey);
    }

    // lsp-workspace-folders follow-up: before resolving a command or spawning
    // anything, see whether an already-running server for this same language
    // can simply be handed this root as an additional workspace folder -- one
    // process serving a monorepo's subpackages instead of one per subpackage.
    // Deliberately ahead of the ServerCommand lookup below: joining an
    // existing process needs no argv of its own, and a language whose command
    // was cleared after its server came up should still be able to serve a
    // newly-opened sibling root from it. Only ever reachable in a genuine
    // multi-root session -- in the single-root case the exact-key lookup at
    // the top of this function already returned.
    if (WorkspaceFoldersEnabled() && !ConnectionKeysForServer(serverKey).empty()) {
        if (const std::optional<std::string> joined = TryJoinWorkspaceFolder(serverKey, root)) {
            return ExistingClientForLanguage(*joined);
        }
        // A sibling exists but couldn't take this root: either it's still
        // handshaking, or it doesn't support workspaceFolders.
        // TryJoinWorkspaceFolder doesn't distinguish those for us, so re-check
        // the one case that must not fall through to a spawn -- a pending
        // handshake, whose answer is what decides between joining and
        // spawning. Retried on the next SyncBuffer, at most a frame away.
        for (const std::string& candidate : ConnectionKeysForServer(serverKey)) {
            if (handshakePending_.contains(candidate)) {
                return nullptr;
            }
        }
    }

    // prose-checking follow-up: the one place kProseLanguageKey is treated
    // differently from a real language -- its command comes from
    // ProseCheckerCommand()'s auto-detect/override/enabled-toggle
    // resolution instead of the plain per-language table.
    const std::optional<std::vector<std::string>> command =
        (serverKey == kProseLanguageKey) ? ProseCheckerCommand() : ServerCommand(serverKey);
    if (!command) {
        return nullptr;
    }

    // error-visibility follow-up. don't retry (or re-log) a command that
    // already failed to spawn on a previous frame -- SyncBuffer calls this
    // every Paint() for the active buffer, and a real subprocess-spawn
    // failure is not transient. Erasing on a *different* command lets a
    // user's own SetLspServerCommand reconfiguration get one fresh attempt.
    // Keyed by connectionKey: the same command failing for one root says
    // nothing about a sibling root's own connection, which may well have a
    // different resolved environment.
    if (const auto failed = failedCommands_.find(connectionKey); failed != failedCommands_.end()) {
        if (failed->second == *command) {
            return nullptr;
        }
        failedCommands_.erase(failed);
        spawnFailureDetail_.erase(connectionKey);
    }

    // respawn-debounce follow-up (requested alongside the crash-loop guard
    // above): even below kCrashLoopThreshold, a disconnect used to be
    // followed by a fresh spawn attempt on the very next SyncBuffer call --
    // the next Paint() frame, tens of milliseconds later, no breathing room
    // at all. A real server that stumbles once (a slow-starting language
    // server racing its own config file, a transient resource hiccup) gets
    // hammered immediately rather than given a moment to actually recover.
    // Silent no-op while cooling down -- not worth a log line every frame
    // for what's an ordinary, expected wait.
    if (const auto lastDisconnect = lastDisconnectAt_.find(connectionKey); lastDisconnect != lastDisconnectAt_.end()) {
        if (std::chrono::steady_clock::now() - lastDisconnect->second < kRespawnCooldown) {
            return nullptr;
        }
    }

    // lsp-broker follow-up. Try attaching to an already-running LSP broker
    // daemon before spawning our own subprocess -- see BrokerConnect.h's
    // own header comment for exactly why this is always safe to attempt
    // (nullptr on any failure, never throws) and why nothing downstream of
    // this needs to know the difference: the returned Client still
    // genuinely performs the real initialize/initialized handshake below,
    // just over a socket to the broker instead of a pipe to a directly-
    // spawned process. LSP multi-root follow-up: root (the buffer's own
    // resolved root, not unconditionally editor::ProjectRoot() anymore) is
    // what actually exercises the broker's own pre-existing (root,
    // language) keying -- see BrokerConnect.h.
    std::unique_ptr<Client> client =
        TryConnectToBroker(root, serverKey, *command, eventLoop_, brokerSocketPathOverrideForTesting_);
    // graceful-lsp-shutdown follow-up: stamped here, the one place this
    // distinction is actually made -- see brokerBackedLanguages_'s own doc
    // comment in Manager.h.
    const bool brokerBacked = (client != nullptr);
    if (!client) {
        try {
            client = std::make_unique<Client>(*command, eventLoop_);
        }
        catch (const std::exception& e) {
            // Previously uncaught -- Transport's constructor throws
            // std::runtime_error for a missing executable, a pipe() failure, or
            // a posix_spawn failure, and this call chain (SyncBuffer <-
            // BufferView::Paint()) had no catch anywhere above it, crashing the
            // whole running editor the instant a buffer of a misconfigured-LSP
            // language was displayed. Report instead of crashing.
            failedCommands_[connectionKey]     = *command;
            spawnFailureDetail_[connectionKey] = e.what();
            LogError(serverKey, e.what());
            return nullptr;
        }
    }
    // LSP multi-root follow-up: root, not unconditionally editor::ProjectRoot()
    // -- a subpackage with its own <root>/.ned/settings.json gets its own
    // settings, same as it would opening that subdirectory as its own
    // top-level project.
    const editor::ProjectSettings projectSettings = editor::LoadProjectSettings(root);
    WireNotificationHandlers(*client, serverKey, connectionKey, projectSettings.lspWorkspaceConfiguration);

    Client* rawClient = client.get();
    // project-settings-lsp-init-options follow-up: initializationOptions
    // covers servers that only read config at handshake time;
    // workspace/didChangeConfiguration (sent right after "initialized",
    // only when lspWorkspaceConfiguration is non-empty) covers the "push"
    // model some servers expect instead -- see ProjectSettings.h's own doc
    // comment on lspWorkspaceConfiguration for why both exist side by side.
    rawClient->SendRequest(
        "initialize", BuildInitializeParams(root, editor::InitializationOptionsForLanguage(projectSettings, serverKey)),
        [this, rawClient, serverKey, connectionKey, workspaceConfiguration = projectSettings.lspWorkspaceConfiguration](
            std::optional<Json> result, std::optional<Json> error) {
            // hang-on-timed-out-initialize follow-up: ExpireStaleRequests
            // invokes this with (nullopt, a synthesized timeout error) if
            // the server never responds -- previously this branch was
            // unreachable in practice because both parameters were ignored,
            // so a timed-out handshake still opened the queued-notification
            // gate and flushed everything queued behind it (including a
            // full-document textDocument/didChange) into a server that had
            // already proven unresponsive, wedging the write.
            handshakePending_.erase(connectionKey); // lsp-workspace-folders follow-up -- decided, either way
            if (error) {
                LogError(serverKey, "initialize failed: " + ExtractErrorMessage(*error));
                disconnectDetail_[connectionKey] = ExtractErrorMessage(*error);
                ClientDisconnected(serverKey, connectionKey);
                return;
            }
            // semantic-tokens/on-type-formatting follow-up: the only two
            // pieces of this response this class keeps -- see
            // SemanticTokensLegendFor/OnTypeFormattingTriggersFor's own doc
            // comment in Manager.h for why. Absent means whatever result
            // is present, the provider just isn't advertised.
            if (result) {
                if (const auto legend = ExtractSemanticTokensLegend(*result)) {
                    semanticTokensLegend_[connectionKey] = *legend;
                }
                if (const auto triggers = ExtractOnTypeFormattingTriggers(*result)) {
                    onTypeFormattingTriggers_[connectionKey] = *triggers;
                }
                if (const auto completionProvider = ExtractCompletionProvider(*result)) {
                    completionProvider_[connectionKey] = *completionProvider;
                }
                if (const auto syncKind = ExtractTextDocumentSyncKind(*result)) {
                    textDocumentSyncKind_[connectionKey] = *syncKind;
                }
                if (const auto fileOpFilters = ExtractFileOperationFilters(*result)) {
                    fileOperationFilters_[connectionKey] = *fileOpFilters;
                }
                // lsp-workspace-folders follow-up: what decides whether a
                // later buffer under a different root joins this process or
                // gets its own -- see TryJoinWorkspaceFolder.
                if (const auto folders = ExtractWorkspaceFoldersSupport(*result)) {
                    workspaceFoldersSupport_[connectionKey] = *folders;
                }
            }
            rawClient->SendNotification("initialized", Json::object());
            if (!workspaceConfiguration.empty()) {
                rawClient->SendNotification("workspace/didChangeConfiguration", Json{{"settings", workspaceConfiguration}});
            }
        });

    clients_.emplace(connectionKey, std::move(client));
    // lsp-workspace-folders follow-up: this connection's own initialize-time
    // folder, and the gate that keeps a second root from racing ahead of the
    // handshake that decides whether it can join here.
    connectionFolders_[connectionKey] = {root};
    handshakePending_.insert(connectionKey);
    if (brokerBacked) {
        brokerBackedLanguages_.insert(connectionKey);
    }
    else {
        brokerBackedLanguages_.erase(connectionKey); // a stale mark from a prior broker-backed client must not outlive a direct respawn
    }
    // mode-line-lsp-status-round-2 follow-up: a successful (re)spawn
    // resolves any prior disconnect -- StatusForLanguage should report
    // Running now, not a stale Disconnected from before this attempt.
    disconnectedLanguages_.erase(connectionKey);
    disconnectDetail_.erase(connectionKey);
    lastDisconnectAt_.erase(connectionKey); // respawn-debounce follow-up -- a stale cooldown must not outlive a real respawn
    return rawClient;
}

void Manager::SyncBuffer(text::Buffer& buffer, const std::string& language) {
    if (!buffer.Path()) {
        return; // a scratch buffer has no URI to tell a server about
    }

    primaryServerKey_[&buffer] = language; // see PrimarySyncState's own doc comment

    // huge-file-lsp-gate follow-up: see this method's own header comment --
    // a huge buffer gets neither sync, ever, rather than paying a
    // buffer.Text() materialization just to discover no server can sanely
    // use the result.
    if (buffer.Content().IsHuge()) {
        if (hugeSyncSkipNotified_.insert(&buffer).second) {
            LogError(language, "\"" + buffer.Name() +
                                   "\" is too large for LSP/prose-checker sync -- diagnostics, completion, "
                                   "and spell-checking are unavailable on this buffer");
        }
        return;
    }

    // LSP multi-root follow-up: resolved once per buffer, reused for both
    // syncs below and (if SyncEmbeddedDocuments follows) every embedded key
    // too -- see this method's own doc comment.
    const std::filesystem::path root = ResolveCachedRoot(*buffer.Path(), language);
    bufferResolvedRoot_[&buffer]     = root;

    SyncToServer(buffer, language, language, root);                       // primary language server
    SyncToServer(buffer, std::string(kProseLanguageKey), language, root); // prose checker, independent of the above
}

void Manager::CheckComposerProseText(const std::string& text, ComposerProseCallback callback) {
    composerProseCallback_ = std::move(callback);
    // Debounces the send itself, not just applying the eventual publish
    // (diagnosticsDebounceTimers_'s own job) -- see this method's own doc
    // comment in the header for why. `text` is captured by value into the
    // timer's callback since prompt_'s own text may have changed again by
    // the time this fires.
    composerProseDebounceTimer_.Arm(eventLoop_, std::chrono::milliseconds(DiagnosticsDebounceMs()), [this, text] {
        if (!composerProseBuffer_) {
            text::Buffer created = text::Buffer::NewFile(ComposerProseScratchPath());
            composerProseBuffer_ = std::make_unique<text::Buffer>(std::move(created));
        }
        composerProseBuffer_->ReplaceContentForLoad(text::Rope(text));
        SyncToServer(*composerProseBuffer_, std::string(kProseLanguageKey), "plaintext", editor::ProjectRoot());
    });
}

void Manager::SyncEmbeddedDocuments(text::Buffer& buffer, const std::vector<EmbeddedDocumentSync>& documents) {
    if (!buffer.Path()) {
        return; // a scratch buffer has no URI to tell a server about
    }

    // LSP multi-root follow-up: an embedded document shares its host
    // buffer's own resolved root, never a root resolved from its own
    // embedded language -- one buffer has exactly one LSP root. Stamped by
    // SyncBuffer, which always precedes this call for the same buffer
    // within a frame (BufferView::Paint()); the editor::ProjectRoot()
    // fallback below should never actually trigger in practice, but keeps
    // this method total regardless of call order.
    const std::filesystem::path root =
        bufferResolvedRoot_.contains(&buffer) ? bufferResolvedRoot_[&buffer] : editor::ProjectRoot();

    std::unordered_set<std::string> desiredKeys;
    for (const EmbeddedDocumentSync& document : documents) {
        desiredKeys.insert(document.language);
        embeddedOwnedRanges_[&buffer][document.language] = document.ownedRanges;
        SyncTextToServer(buffer, document.language, document.language, document.documentText, root);
    }

    // Tear down any server key this buffer was previously embedded-synced
    // to but that documents no longer contains -- its only region was
    // deleted (or the buffer switched to a mode/state with none at all).
    // Left running, it would keep reporting stale diagnostics for content
    // that no longer exists in the buffer.
    bool diagnosticsChanged = false;
    for (const std::string& key : embeddedServerKeys_[&buffer]) {
        if (desiredKeys.contains(key)) {
            continue; // still wanted -- SyncTextToServer above already handled it
        }
        if (const auto bufferIt = bufferState_.find(&buffer); bufferIt != bufferState_.end()) {
            if (const auto stateIt = bufferIt->second.find(key); stateIt != bufferIt->second.end()) {
                if (stateIt->second.opened) {
                    if (Client* client = ExistingClientForLanguage(stateIt->second.connectionKey)) {
                        client->SendNotification("textDocument/didClose", {{"textDocument", {{"uri", stateIt->second.uri}}}});
                    }
                }
                bufferIt->second.erase(stateIt);
                if (bufferIt->second.empty()) {
                    bufferState_.erase(bufferIt);
                }
            }
        }
        if (const auto ownedIt = embeddedOwnedRanges_.find(&buffer); ownedIt != embeddedOwnedRanges_.end()) {
            ownedIt->second.erase(key);
        }
        if (const auto diagIt = diagnosticsBySource_.find(&buffer); diagIt != diagnosticsBySource_.end()) {
            if (diagIt->second.erase(key) > 0) {
                diagnosticsChanged = true;
            }
        }
    }
    embeddedServerKeys_[&buffer] = std::move(desiredKeys);

    if (diagnosticsChanged) {
        PushMergedDiagnostics(buffer);
    }
}

std::vector<std::string> Manager::ActiveServerKeysForBuffer(const text::Buffer& buffer) const {
    std::vector<std::string> keys;
    const auto               it = bufferState_.find(const_cast<text::Buffer*>(&buffer));
    if (it == bufferState_.end()) {
        return keys;
    }
    keys.reserve(it->second.size());
    for (const auto& [serverKey, state] : it->second) {
        keys.push_back(serverKey);
    }
    return keys;
}

void Manager::SyncToServer(text::Buffer& buffer, const std::string& serverKey, const std::string& languageId,
                              const std::filesystem::path& root) {
    // progressive-huge-file-load follow-up: checked here, ahead of the
    // buffer.Text() argument below, rather than relying solely on
    // SyncTextToServer's own (still-kept, still-needed-by-
    // SyncEmbeddedDocuments) client check -- buffer.Text() unconditionally
    // materializes the whole document (Storage_->ToString()), which for a
    // huge buffer with no server configured for its language was a real,
    // reproduced live hang: every SyncBackgroundBuffers tick paid two full
    // multi-GB copies (primary + prose-checker) just to discover there was
    // nothing to send them to, and once that cost exceeds the tick
    // interval the EventLoop::Post queue backs up forever. A no-op buffer
    // argument is never worth evaluating eagerly.
    // NED_DEBUG_LSP_SYNC also records why a sync did NOT happen: a didChange
    // that stops being sent and a didChange that is sent wrongly look
    // identical from the server's answers.
    const auto traceSkip = [&](const char* reason, std::size_t detail) {
        if (const char* path = std::getenv("NED_DEBUG_LSP_SYNC"); path != nullptr && *path != '\0') {
            if (std::ofstream trace{path, std::ios::app}) {
                trace << "syncSkip " << reason << " serverKey=" << serverKey << " generation=" << buffer.ContentGeneration()
                      << " detail=" << detail << '\n';
            }
        }
    };
    if (!ClientForLanguage(serverKey, root)) {
        traceSkip("no-client", 0);
        return;
    }

    // per-frame-sync-materialize follow-up: SyncTextToServer's own
    // "nothing changed since the last sync" check happens too late to help
    // here -- buffer.Text() below is a function argument, evaluated
    // unconditionally before SyncTextToServer's body ever runs. Called
    // every Paint() for the focused buffer, that meant a buffer already
    // synced and unchanged still paid a full ITextStorage::ToString() on
    // every repaint, forever -- for a several-GB buffer, a reproduced live
    // hang (see this file's own history). Duplicating the same check here,
    // ahead of buffer.Text(), is what actually makes the "second call is a
    // no-op" claim true.
    BufferSyncState* existingState = nullptr;
    if (const auto bufferIt = bufferState_.find(&buffer); bufferIt != bufferState_.end()) {
        if (const auto stateIt = bufferIt->second.find(serverKey); stateIt != bufferIt->second.end()) {
            existingState = &stateIt->second;
            if (existingState->opened && existingState->lastSyncedGeneration == buffer.ContentGeneration()) {
                return; // nothing changed since the last sync (not traced: the common per-frame no-op)
            }
        }
    }

    if (!existingState || !existingState->opened) {
        // Not yet opened -- didOpen must stay immediate, never debounced (a
        // newly visible buffer needs diagnostics/highlighting right away,
        // not after an arbitrary delay); SyncTextToServer's own
        // !state.opened branch is what actually sends it.
        SyncTextToServer(buffer, serverKey, languageId, buffer.Text(), root);
        return;
    }

    // sync-debounce follow-up: already open, content changed -- debounce
    // the actual textDocument/didChange send instead of materializing
    // buffer.Text() and sending synchronously right here. A real,
    // gdb-confirmed live freeze traced to exactly this send happening on
    // every single keystroke (see ServerConfig.h's SyncDebounceMs
    // doc comment): the main thread blocked inside ChildProcess::WriteAll,
    // stuck writing a full-document sync to a server whose stdin pipe
    // couldn't drain fast enough. See BufferSyncState::pendingSyncGeneration's
    // own doc comment for why this guards against re-arming on every
    // Paint(), not just on a genuine new edit.
    if (existingState->pendingSyncGeneration && *existingState->pendingSyncGeneration == buffer.ContentGeneration()) {
        traceSkip("already-pending", *existingState->pendingSyncGeneration);
        return; // already debounced for this exact generation -- let it run its course
    }
    existingState->pendingSyncGeneration = buffer.ContentGeneration();
    traceSkip("arming", static_cast<std::size_t>(SyncDebounceMs()));

    text::Buffer* const bufferPtr = &buffer;
    syncDebounceTimers_[&buffer][serverKey].Arm(
        eventLoop_, std::chrono::milliseconds(SyncDebounceMs()), [this, bufferPtr, serverKey, languageId, root] {
            // Re-reads buffer.Text() fresh here, not at arm time -- more
            // edits may have landed during the debounce window, and this
            // must send the *latest* content, not a stale snapshot.
            if (const char* path = std::getenv("NED_DEBUG_LSP_SYNC"); path != nullptr && *path != '\0') {
                if (std::ofstream trace{path, std::ios::app}) {
                    trace << "syncTimerFired serverKey=" << serverKey << " generation=" << bufferPtr->ContentGeneration() << '\n';
                }
            }
            SyncTextToServer(*bufferPtr, serverKey, languageId, bufferPtr->Text(), root);
        });
}

void Manager::SyncTextToServer(text::Buffer& buffer, const std::string& serverKey, const std::string& languageId,
                                  const std::string& documentText, const std::filesystem::path& root) {
    Client* client = ClientForLanguage(serverKey, root);
    if (!client) {
        return; // nothing configured/running for this server
    }

    BufferSyncState& state = bufferState_[&buffer][serverKey];

    if (!state.opened) {
        // prose-checking follow-up: never runs prose checking against a
        // binary buffer -- keyed off the same LooksBinary heuristic
        // Buffer::FromFile/ProjectSearch already use, not a new one.
        // Scoped to the prose checker specifically (not the primary
        // language server, whose own behavior here predates this feature
        // and is out of its scope) -- sending harper-ls raw binary content
        // as "text" is pure waste at best. Checked only on the not-yet-
        // opened path, not every sync, to avoid a disk read every frame.
        //
        // std::filesystem::exists is checked first: LooksBinary treats an
        // unreadable path as binary too (a sensible default for its own
        // original "read the file" callers), but buffer.Path() naming a
        // file that doesn't exist on disk yet just means an unsaved new
        // buffer -- there's no on-disk content to be binary, and that must
        // not be conflated with an actually-binary file.
        if (serverKey == kProseLanguageKey && std::filesystem::exists(*buffer.Path()) && text::LooksBinary(*buffer.Path())) {
            return;
        }
        state.connectionKey = ResolvedConnectionKey(root, serverKey); // lsp-workspace-folders follow-up: canonical, not raw
        state.uri           = PathToUri(*buffer.Path());
        state.version       = 1;
        client->SendNotification("textDocument/didOpen", {
                                                             {"textDocument",
                                                              {
                                                                  {"uri", state.uri},
                                                                  {"languageId", languageId},
                                                                  {"version", state.version},
                                                                  {"text", documentText},
                                                              }},
                                                         });
        state.opened               = true;
        state.lastSyncedGeneration = buffer.ContentGeneration();
        state.lastSyncedText       = documentText; // incremental-sync follow-up: baseline for the first didChange's diff
        // pull-diagnostics follow-up: same cadence as didOpen/didChange
        // itself, no separate debounce timer -- see RequestPullDiagnostics'
        // own doc comment in Manager.h. Opt-in (PullDiagnosticsEnabled,
        // default false): unconditionally, this would mean one extra
        // request per content sync for every server, forever, whether or
        // not it actually needs pull diagnostics at all.
        if (PullDiagnosticsEnabled()) {
            RequestPullDiagnostics(buffer, serverKey);
        }
        return;
    }

    if (buffer.ContentGeneration() == state.lastSyncedGeneration) {
        return; // nothing changed since the last sync
    }

    ++state.version;
    // NED_DEBUG_LSP_FULL_SYNC=1 sends the whole document on every change
    // regardless of what the server advertised. A server whose positions stop
    // drifting under it is one that does not apply our incremental edits.
    static const bool forceFullSync = [] {
        const char* value = std::getenv("NED_DEBUG_LSP_FULL_SYNC");
        return value != nullptr && *value != '\0' && *value != '0';
    }();
    // NED_DEBUG_LSP_SYNC=<path>: what we actually put on the wire, so a
    // server answering against a document it never received is
    // distinguishable from one we told incorrectly.
    const auto traceSync = [&](const char* kind, const std::string& detail) {
        if (const char* path = std::getenv("NED_DEBUG_LSP_SYNC"); path != nullptr && *path != '\0') {
            if (std::ofstream trace{path, std::ios::app}) {
                trace << "didChange " << kind << " uri=" << state.uri << " version=" << state.version
                      << " generation=" << buffer.ContentGeneration() << " docBytes=" << documentText.size() << ' '
                      << detail << '\n';
            }
        }
    };
    if (!forceFullSync && TextDocumentSyncKindFor(state.connectionKey) == TextDocumentSyncKind::Incremental) {
        // incremental-sync follow-up: common-prefix/common-suffix byte diff,
        // the same shape as IncrementalParseCache::Update's own diff
        // (Editor/Grammar/IncrementalParse.cpp) -- "correct, if not
        // always maximally minimal" is enough here too: a multi-cursor edit
        // or a large external revert just widens to one outer span, which
        // is spec-legal for a single contentChanges[0] entry. oldText may
        // also be a differently-padded rebuild of the same embedded region
        // rather than a minimal edit of it (SyncEmbeddedDocuments
        // periodically rebuilds virtual-document text with shifted
        // whitespace padding) -- this walk handles that the same as any
        // other "far apart" edit, no special-casing needed: it just finds a
        // smaller common prefix/suffix and sends a wider span.
        const std::string& oldText   = state.lastSyncedText;
        const std::size_t  maxCommon = std::min(oldText.size(), documentText.size());
        std::size_t        prefix    = 0;
        while (prefix < maxCommon && oldText[prefix] == documentText[prefix]) {
            ++prefix;
        }
        const std::size_t maxSuffix = maxCommon - prefix;
        std::size_t       suffix    = 0;
        while (suffix < maxSuffix && oldText[oldText.size() - 1 - suffix] == documentText[documentText.size() - 1 - suffix]) {
            ++suffix;
        }

        // A byte-identical prefix/suffix scan has no notion of UTF-8
        // codepoint boundaries -- two different multi-byte characters that
        // happen to share a leading or trailing byte (e.g. the left/right
        // "smart quote" pair E2 80 9C / E2 80 9D, sharing their first two
        // bytes) can make prefix/suffix stop mid-codepoint. Sent as-is,
        // ByteRangeToLspRange can't represent that offset at all -- its
        // walk steps whole codepoints and skips straight over a
        // non-boundary target, silently leaving `start` at its default
        // {0, 0} instead of the real position. Snapping outward (backward
        // for the start, forward for the end) only ever widens the diffed
        // span, which stays spec-legal per this function's own "correct, if
        // not always maximally minimal" contract above.
        const std::size_t oldStartByte = text::SnapDownToCodepointBoundary(oldText, prefix);
        std::size_t       oldEndByte   = text::SnapUpToCodepointBoundary(oldText, oldText.size() - suffix);
        oldEndByte                     = std::max(oldEndByte, oldStartByte);
        const std::size_t newStartByte = oldStartByte; // shared prefix bytes are identical in both texts at this offset
        std::size_t       newEndByte   = documentText.size() - (oldText.size() - oldEndByte);
        newEndByte                     = std::max(newEndByte, newStartByte);

        const Range    range       = ByteRangeToLspRange(oldText, oldStartByte, oldEndByte);
        const std::size_t rangeLength = Utf16LengthOfByteRange(oldText, oldStartByte, oldEndByte);
        const std::string changedText = documentText.substr(newStartByte, newEndByte - newStartByte);

        traceSync("incremental", "range=" + std::to_string(range.start.line) + ':' +
                                    std::to_string(range.start.character) + ".." + std::to_string(range.end.line) + ':' +
                                    std::to_string(range.end.character) + " rangeLength=" + std::to_string(rangeLength) +
                                    " textBytes=" + std::to_string(changedText.size()));
        client->SendNotification(
            "textDocument/didChange",
            {
                {"textDocument", {{"uri", state.uri}, {"version", state.version}}},
                {"contentChanges", Json::array({{
                                       {"range",
                                        {{"start", {{"line", range.start.line}, {"character", range.start.character}}},
                                         {"end", {{"line", range.end.line}, {"character", range.end.character}}}}},
                                       {"rangeLength", rangeLength},
                                       {"text", changedText},
                                   }})},
            });
    }
    else {
        traceSync("full", "advertisedKind=" + std::to_string(static_cast<int>(TextDocumentSyncKindFor(state.connectionKey))) +
                              " forced=" + std::to_string(forceFullSync));
        client->SendNotification("textDocument/didChange", {
                                                               {"textDocument", {{"uri", state.uri}, {"version", state.version}}},
                                                               {"contentChanges", Json::array({{{"text", documentText}}})},
                                                           });
    }
    state.lastSyncedGeneration = buffer.ContentGeneration();
    state.lastSyncedText       = documentText;
    if (PullDiagnosticsEnabled()) {
        RequestPullDiagnostics(buffer, serverKey);
    }
}

Manager::BufferSyncState* Manager::PrimarySyncState(text::Buffer& buffer) {
    const auto keyIt = primaryServerKey_.find(&buffer);
    if (keyIt == primaryServerKey_.end()) {
        return nullptr;
    }
    const auto bufferIt = bufferState_.find(&buffer);
    if (bufferIt == bufferState_.end()) {
        return nullptr;
    }
    const auto stateIt = bufferIt->second.find(keyIt->second);
    return stateIt != bufferIt->second.end() ? &stateIt->second : nullptr;
}

Manager::BufferSyncState* Manager::ResolveSyncState(text::Buffer& buffer, const std::string& serverKey) {
    if (serverKey.empty()) {
        return PrimarySyncState(buffer);
    }
    const auto it = bufferState_.find(&buffer);
    if (it == bufferState_.end()) {
        return nullptr;
    }
    const auto stateIt = it->second.find(serverKey);
    return stateIt != it->second.end() ? &stateIt->second : nullptr;
}

Client& Manager::SetClientForTesting(std::string language, std::unique_ptr<Client> client,
                                           const Json& workspaceConfiguration, bool brokerBacked,
                                           std::optional<std::string> connectionKeyOverride) {
    // LSP multi-root follow-up: nullopt (every pre-existing call site)
    // registers under language itself, unchanged -- see this method's own
    // doc comment in Manager.h.
    const std::string connectionKey = connectionKeyOverride.value_or(language);
    WireNotificationHandlers(*client, language, connectionKey, workspaceConfiguration); // same wiring ClientForLanguage's real spawn path applies
    disconnectedLanguages_.erase(connectionKey);                                        // an injected client is "running," same as a real successful spawn
    disconnectDetail_.erase(connectionKey);
    lastDisconnectAt_.erase(connectionKey); // respawn-debounce follow-up -- ditto
    if (brokerBacked) {
        brokerBackedLanguages_.insert(connectionKey);
    }
    else {
        brokerBackedLanguages_.erase(connectionKey);
    }
    Client& ref          = *client;
    clients_[connectionKey] = std::move(client);
    return ref;
}

void Manager::ClientDisconnected(const std::string& serverKey, const std::string& connectionKey) {
    // Both may be references into the very Client (and its
    // OnDisconnected closure) this function destroys below -- copy them
    // first so the rest of this function isn't reading freed memory.
    const std::string serverKeyCopy     = serverKey;
    const std::string connectionKeyCopy = connectionKey;
    // lsp-use-after-free follow-up: this used to retire into a retired_
    // vector instead of erasing immediately, on the theory that "wait for
    // the next periodic tick before actually freeing" gave any in-flight
    // Post()ed callback time to drain first. Confirmed live via ASan that
    // this isn't actually safe -- Client::ExpireStaleRequests's own
    // periodic tick and a client's background thread both Post() against
    // EventLoop independently, with no ordering guarantee between them, so
    // the tick can free a just-retired client while another callback for
    // that exact object is still queued. The real fix now lives in
    // Client itself (see its header comment on alive_) -- a stray
    // Post()ed callback safely no-ops instead of touching freed memory
    // regardless of when this destroys the object, so plain immediate
    // erase() is safe again and retired_ is gone.
    clients_.erase(connectionKeyCopy);
    brokerBackedLanguages_.erase(connectionKeyCopy); // graceful-lsp-shutdown follow-up -- must not outlive the client it described
    // semantic-tokens/on-type-formatting follow-up: a respawned server may
    // advertise a different legend/trigger set than the one that just
    // died -- don't let a stale entry outlive this connection.
    semanticTokensLegend_.erase(connectionKeyCopy);
    onTypeFormattingTriggers_.erase(connectionKeyCopy);
    completionProvider_.erase(connectionKeyCopy);                 // ditto -- a respawn may declare different triggers, or lose resolveProvider
    textDocumentSyncKind_.erase(connectionKeyCopy);               // ditto -- a respawned server may advertise a different sync kind
    fileOperationFilters_.erase(connectionKeyCopy);               // ditto -- a respawned server may advertise different willRename/didRename filters
    pullDiagnosticsUnsupported_.erase(connectionKeyCopy);         // a respawned server gets one fresh attempt
    inlayHintsUnsupported_.erase(connectionKeyCopy);              // ditto
    codeLensUnsupported_.erase(connectionKeyCopy);                // ditto
    documentLinkUnsupported_.erase(connectionKeyCopy);            // ditto
    semanticTokensRangeUnsupported_.erase(connectionKeyCopy);     // ditto
    semanticTokensFullDeltaUnsupported_.erase(connectionKeyCopy); // ditto
    // lsp-workspace-folders follow-up: this connection's folder set dies
    // with it, and so must every redirect pointing at it -- otherwise a
    // joined root would keep resolving to a canonical key that no longer
    // names a client, and never spawn a replacement. Each orphaned root
    // re-resolves on its next sync (ClientForLanguage's own stale-redirect
    // branch), joining a surviving sibling or spawning its own.
    workspaceFoldersSupport_.erase(connectionKeyCopy);
    connectionFolders_.erase(connectionKeyCopy);
    handshakePending_.erase(connectionKeyCopy);
    std::erase_if(joinedConnection_, [&connectionKeyCopy](const auto& entry) { return entry.second == connectionKeyCopy; });
    // mode-line-lsp-status-round-2 follow-up: latch the disconnect so
    // StatusForLanguage can report it, distinct from "never configured" --
    // cleared the moment a fresh spawn succeeds (ClientForLanguage) or the
    // reconfigured command fails outright (StatusForLanguage's SpawnFailed
    // case takes priority over this one regardless).
    disconnectedLanguages_.insert(connectionKeyCopy);

    // crash-loop-respawn-guard follow-up: see disconnectBurst_'s own doc
    // comment in Manager.h. Must run before this function returns (every
    // exit path below still respawns on the next SyncBuffer otherwise).
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    lastDisconnectAt_[connectionKeyCopy]            = now; // respawn-debounce follow-up
    auto& burst                                     = disconnectBurst_[connectionKeyCopy];
    if (now - burst.first > kCrashLoopWindow) {
        burst = {now, 1};
    }
    else {
        ++burst.second;
    }
    if (burst.second >= kCrashLoopThreshold) {
        // Latches failedCommands_ -- ClientForLanguage's own pre-existing
        // "known-bad command, stop retrying until reconfigured" guard --
        // rather than returning early here, so the ordinary cleanup below
        // (bufferState_/diagnosticsBySource_/activeProgress_) still runs
        // exactly as it would for any other disconnect.
        const std::optional<std::vector<std::string>> command =
            (serverKeyCopy == kProseLanguageKey) ? ProseCheckerCommand() : ServerCommand(serverKeyCopy);
        if (command) {
            failedCommands_[connectionKeyCopy] = *command;
        }
        spawnFailureDetail_[connectionKeyCopy] =
            "gave up after " + std::to_string(burst.second) + " immediate disconnects in a row -- reconfigure the command to retry";
        disconnectBurst_.erase(connectionKeyCopy);
        LogError(serverKeyCopy, "server crash-looped -- giving up until the command is reconfigured");
    }
    // prose-checking follow-up: erase just this server's own sub-entry, not
    // the whole buffer -- a buffer's other server (primary or prose,
    // whichever serverKeyCopy isn't) must keep its own sync state and
    // diagnostics intact. Drop the outer entry too once it's left empty,
    // and drop + re-flatten this server's now-stale diagnostics slice so
    // Buffer::Diagnostics() doesn't keep reporting from a server that's no
    // longer there.
    //
    // bufferState_/diagnosticsBySource_ are keyed by serverKey (a buffer
    // talks to at most one connection per server key), so the match on the
    // dying *connection* is BufferSyncState::connectionKey, not the key
    // itself -- a buffer whose own resolved root points this same server key
    // at a different, still-live connection must be left completely alone.
    std::vector<text::Buffer*> affected;
    for (auto it = bufferState_.begin(); it != bufferState_.end();) {
        const auto stateIt = it->second.find(serverKeyCopy);
        if (stateIt != it->second.end() && stateIt->second.connectionKey == connectionKeyCopy) {
            affected.push_back(it->first);
            it->second.erase(stateIt);
        }
        if (it->second.empty()) {
            it = bufferState_.erase(it);
        }
        else {
            ++it;
        }
    }
    for (text::Buffer* buffer : affected) {
        const auto it = diagnosticsBySource_.find(buffer);
        if (it == diagnosticsBySource_.end()) {
            continue;
        }
        if (it->second.erase(serverKeyCopy) > 0) {
            PushMergedDiagnostics(*buffer);
        }
        if (it->second.empty()) {
            diagnosticsBySource_.erase(it);
        }
    }
    // workDoneProgress-support follow-up: a dying server never sends "end"
    // for its live progress sessions -- End them here or the spinner runs
    // forever (the request-count half of the same problem is ~Client's
    // own responsibility; see its destructor comment).
    const std::string keyPrefix = connectionKeyCopy + '\x1f';
    for (auto it = activeProgress_.begin(); it != activeProgress_.end();) {
        if (it->first.rfind(keyPrefix, 0) == 0) {
            it = activeProgress_.erase(it);
            EndBackgroundActivity(kLspActivity);
        }
        else {
            ++it;
        }
    }
    if (activeProgress_.empty()) {
        SetBackgroundActivityDetail(kLspActivity, std::string()); // same stale-detail rule HandleProgress' own end branch applies
    }
}

void Manager::LogError(std::string_view language, std::string_view message) {
    text::Buffer* log = bufferList_.Find(std::string(kLspLogBufferName));
    if (!log) {
        log = &bufferList_.CreateBuffer(std::string(kLspLogBufferName));
        log->SetReadOnly(true); // must be set before the first append -- AppendWhileReadOnly's own precondition
    }
    log->AppendWhileReadOnly(FormatLogLine(language, message));
    hasUnseenLogEntry_ = true;
}

bool Manager::HasUnseenLogEntry() const {
    return hasUnseenLogEntry_;
}

void Manager::AcknowledgeLogEntry() {
    hasUnseenLogEntry_ = false;
}

Manager::Status Manager::StatusForLanguage(const std::string& connectionKey) const {
    if (ExistingClientForLanguage(connectionKey) != nullptr) {
        return Status::Running;
    }
    if (failedCommands_.contains(connectionKey)) {
        return Status::SpawnFailed;
    }
    if (disconnectedLanguages_.contains(connectionKey)) {
        return Status::Disconnected;
    }
    return Status::NotConfigured;
}

std::string Manager::SpawnFailureDetail(const std::string& connectionKey) const {
    const auto it = spawnFailureDetail_.find(connectionKey);
    return it != spawnFailureDetail_.end() ? it->second : std::string();
}

std::string Manager::DisconnectReason(const std::string& connectionKey) const {
    const auto it = disconnectDetail_.find(connectionKey);
    return it != disconnectDetail_.end() ? it->second : std::string();
}

std::optional<SemanticTokensLegend> Manager::SemanticTokensLegendFor(const std::string& connectionKey) const {
    const auto it = semanticTokensLegend_.find(connectionKey);
    return it != semanticTokensLegend_.end() ? std::optional(it->second) : std::nullopt;
}

std::optional<CompletionProviderInfo> Manager::CompletionProviderFor(const std::string& connectionKey) const {
    const auto it = completionProvider_.find(connectionKey);
    return it != completionProvider_.end() ? std::optional(it->second) : std::nullopt;
}

std::optional<OnTypeFormattingTriggers> Manager::OnTypeFormattingTriggersFor(const std::string& connectionKey) const {
    const auto it = onTypeFormattingTriggers_.find(connectionKey);
    return it != onTypeFormattingTriggers_.end() ? std::optional(it->second) : std::nullopt;
}

TextDocumentSyncKind Manager::TextDocumentSyncKindFor(const std::string& connectionKey) const {
    const auto it = textDocumentSyncKind_.find(connectionKey);
    return it != textDocumentSyncKind_.end() ? it->second : TextDocumentSyncKind::Full;
}

void Manager::NotifyBufferClosed(text::Buffer& buffer) {
    const auto it = bufferState_.find(&buffer);
    if (it != bufferState_.end()) {
        // prose-checking follow-up: buffer may have up to two sync states
        // (primary + prose) -- notify every server it was ever opened with,
        // not just one.
        for (const auto& perServer : it->second) {
            const BufferSyncState& state = perServer.second;
            if (state.opened) {
                if (Client* client = ExistingClientForLanguage(state.connectionKey)) {
                    client->SendNotification("textDocument/didClose", {{"textDocument", {{"uri", state.uri}}}});
                }
            }
        }
        bufferState_.erase(it);
    }
    diagnosticsBySource_.erase(&buffer);
    diagnosticsDebounceTimers_.erase(&buffer); // cancels a pending timer before it can fire against a dead buffer
    syncDebounceTimers_.erase(&buffer);        // sync-debounce follow-up: same rationale, for a pending didChange send
    viewportRequestTimers_.erase(&buffer);     // same rationale again, for a pending throttled viewport request
    armedViewportRequests_.erase(&buffer);
    viewportRequestPending_.erase(&buffer);
    lastViewportRequestAt_.erase(&buffer);
    primaryServerKey_.erase(&buffer);
    bufferResolvedRoot_.erase(&buffer); // LSP multi-root follow-up
    embeddedServerKeys_.erase(&buffer);
    embeddedOwnedRanges_.erase(&buffer);
    hugeSyncSkipNotified_.erase(&buffer);
    semanticTokensRequestedGeneration_.erase(&buffer);
    semanticTokensRequestCounter_.erase(&buffer);
    semanticTokenSpans_.erase(&buffer);
    semanticTokenSpansContentGeneration_.erase(&buffer);
    semanticTokensGeneration_.erase(&buffer);
    semanticTokensCoverage_.erase(&buffer);
    previousSemanticTokens_.erase(&buffer);
    inlayHintCoverage_.erase(&buffer);
    inlayHintsRequestCounter_.erase(&buffer);
    if (const auto anchorsIt = inlayHintAnchors_.find(&buffer); anchorsIt != inlayHintAnchors_.end()) {
        // Released explicitly rather than left to the buffer's own teardown:
        // an abandoned anchor is a slot this store keeps relocating forever,
        // and a buffer outlives any one server's interest in it.
        for (const AnchoredInlayHint& hint : anchorsIt->second) {
            buffer.DestroyAnchor(hint.anchor);
        }
        inlayHintAnchors_.erase(anchorsIt);
    }
    inlayHintRevision_.erase(&buffer);
    inlayHintView_.erase(&buffer);
    codeLensRequestedGeneration_.erase(&buffer);
    codeLensRequestCounter_.erase(&buffer);
    codeLensSpans_.erase(&buffer);
    codeLensSpansGeneration_.erase(&buffer);
}

void Manager::ExpireStaleRequests(std::chrono::milliseconds maxAge) {
    // reentrant-expiry-during-iteration follow-up: a stale *initialize*
    // request's synthesized-timeout callback (SpawnClient's own lambda,
    // above) calls ClientDisconnected on error, which erases the client
    // from clients_ immediately (see that function's own comment on why
    // that's correct) -- and that call can happen synchronously, from
    // inside entry.second->ExpireStaleRequests(maxAge) below, while this
    // very range-for loop is iterating clients_. Erasing the element the
    // loop is currently visiting invalidates its iterator; the loop's own
    // ++it (or a later entry sharing a since-invalidated bucket) then reads
    // freed map-node memory -- confirmed live via a real SIGSEGV, and
    // reproduced deterministically under ASan (heap-use-after-free, this
    // exact line) once the fix below was reverted. Client::
    // ExpireStaleRequests already guards its own pending_ map this same way
    // (see its own comment); this is that same fix one level up. Snapshot
    // the keys first, then re-resolve each via a fresh find() right before
    // use, so a disconnect cascaded from an earlier language in this same
    // pass is observed as "already gone" instead of dereferencing a stale
    // iterator/pointer.
    std::vector<std::string> languages;
    languages.reserve(clients_.size());
    for (const auto& entry : clients_) {
        languages.push_back(entry.first);
    }
    for (const std::string& language : languages) {
        if (const auto it = clients_.find(language); it != clients_.end()) {
            it->second->ExpireStaleRequests(maxAge);
        }
    }
}

void Manager::HandlePublishDiagnostics(const Json& params, const std::string& language) {
    if (!params.contains("uri")) {
        return;
    }
    const std::optional<std::filesystem::path> path = UriToPath(params["uri"].get<std::string>());
    if (!path) {
        return;
    }

    // prose-check-composer follow-up: the composer's pseudo-document never
    // resolves through bufferList_ (it isn't a real, registered Buffer) --
    // intercepted here, ahead of the FindByPath lookup below, and routed to
    // whichever callback CheckComposerProseText most recently stored instead
    // of buffer->SetDiagnostics.
    if (language == kProseLanguageKey && composerProseBuffer_ && path == composerProseBuffer_->Path()) {
        if (composerProseCallback_) {
            composerProseCallback_(
                ParsePublishedDiagnostics(params, composerProseBuffer_->Content(), text::Buffer::Diagnostic::Origin::Prose));
        }
        return;
    }

    text::Buffer* buffer = bufferList_.FindByPath(*path);
    if (!buffer) {
        return; // not an open buffer -- nothing to update
    }

    // prose-diagnostic-callout follow-up: language is the same per-server
    // key HandlePublishDiagnostics is registered under (see the
    // SetNotificationHandler wiring above, capturing `language` per
    // language) -- kProseLanguageKey identifies the reserved prose-checker
    // connection, everything else is a real code language server.
    const text::Buffer::Diagnostic::Origin origin =
        (language == kProseLanguageKey) ? text::Buffer::Diagnostic::Origin::Prose : text::Buffer::Diagnostic::Origin::Code;

    // stale-publish-position follow-up. A server computes its {line,
    // character} positions against the document version it was last told
    // about and answers on its own schedule, so by the time a publish lands
    // the buffer has usually moved on -- the sync itself is debounced, and
    // typing does not stop while it waits. Converting those positions against
    // the *current* content puts every diagnostic on the wrong bytes until
    // the next publish catches up, which is what "the underlines shift until
    // the LSP redraws" looked like from the outside.
    //
    // BufferSyncState::lastSyncedText is already exactly the text the server
    // was last sent (it exists as the incremental-sync baseline), and
    // lastSyncedGeneration says which buffer generation that text is -- so
    // this needs no new storage: convert against that text, then carry the
    // resulting offsets onto live content by replaying the buffer's own edits
    // since that generation.
    //
    // The remap runs before FilterToOwnedRanges, which asks about the *live*
    // buffer's embedded-language ranges and would otherwise be handed offsets
    // against a different document.
    //
    // A `version` in the params that disagrees with what we last sent means
    // the server is further behind still; lastSyncedText remains the closest
    // document we hold, so this uses it either way rather than dropping a
    // publish and leaving the line unmarked.
    const BufferSyncState* syncState = ResolveSyncState(*buffer, language);
    DiagnosticSlice        slice;
    if (syncState != nullptr && !syncState->lastSyncedText.empty() &&
        syncState->lastSyncedGeneration != buffer->ContentGeneration()) {
        const text::RopeStorage sentContent{text::Rope(syncState->lastSyncedText)};
        slice.diagnostics = ParsePublishedDiagnostics(params, sentContent, origin);
        // Stamped with the generation that text is, not the live one: these
        // offsets are against what the server was sent, and the rebase just
        // below is what has to run.
        slice.resolvedAtGeneration = syncState->lastSyncedGeneration;
        // Onto the live content right here rather than leaving it for the
        // push: FilterToOwnedRanges just below asks about the *live*
        // buffer's embedded-language ranges and would otherwise be handed
        // offsets against a different document.
        RebaseSliceOntoLiveContent(*buffer, slice);
    }
    else {
        slice.diagnostics          = ParsePublishedDiagnostics(params, buffer->Content(), origin);
        slice.resolvedAtGeneration = buffer->ContentGeneration();
    }
    FilterToOwnedRanges(buffer, language, slice.diagnostics);

    // prose-checking follow-up: this server's own full current diagnostic
    // set for buffer replaces only its own slice -- another server's slice
    // (recorded independently the same way) is untouched. PushMergedDiagnostics
    // is what actually reaches buffer.SetDiagnostics.
    // slice.resolvedAtGeneration is what PushMergedDiagnostics carries from --
    // these offsets are right for the buffer as it is at this instant, and the
    // debounce below means that is not the instant they are applied.
    diagnosticsBySource_[buffer][language] = std::move(slice);

    // diagnostics-debounce follow-up: applying this immediately would mean
    // inline diagnostics repaint on essentially every keystroke (a server
    // re-publishes after every didChange, which SyncBuffer sends on every
    // content-generation bump) -- (re)arm buffer's own debounce timer
    // instead, collapsing a rapid-typing burst of publishes into one
    // application once the buffer goes quiet for a beat.
    diagnosticsDebounceTimers_[buffer].Arm(eventLoop_, std::chrono::milliseconds(DiagnosticsDebounceMs()),
                                           [this, buffer] { PushMergedDiagnostics(*buffer); });
}

void Manager::FilterToOwnedRanges(text::Buffer* buffer, const std::string& language,
                                     std::vector<text::Buffer::Diagnostic>& diagnostics) const {
    // embedded-language-documents follow-up: an embedded server (one with an
    // owned-ranges record) only ever legitimately reports within its own
    // owned regions -- a padded/blanked region should tokenize as inert
    // whitespace, so a diagnostic starting outside every owned range is
    // either a rare parser edge case at a padding boundary or a server
    // ignoring content it wasn't asked about. Dropped defensively rather
    // than surfaced against the wrong language's chrome. No effect on the
    // primary language or kProseLanguageKey, neither of which ever has an
    // owned-ranges entry (they own the whole buffer).
    const auto ownedIt = embeddedOwnedRanges_.find(buffer);
    if (ownedIt == embeddedOwnedRanges_.end()) {
        return;
    }
    const auto rangeIt = ownedIt->second.find(language);
    if (rangeIt == ownedIt->second.end()) {
        return;
    }
    const std::vector<std::pair<std::size_t, std::size_t>>& ranges = rangeIt->second;
    std::erase_if(diagnostics, [&ranges](const text::Buffer::Diagnostic& diagnostic) {
        for (const auto& range : ranges) {
            if (diagnostic.startByte >= range.first && diagnostic.startByte < range.second) {
                return false;
            }
        }
        return true;
    });
}

void Manager::RequestPullDiagnostics(text::Buffer& buffer, const std::string& serverKey) {
    BufferSyncState* state = ResolveSyncState(buffer, serverKey);
    if (!state || !state->opened) {
        return;
    }
    // Latched per connection, not per server key -- resolved after the sync
    // state above precisely because that's what carries this buffer's own
    // connection identity. Same reordering applies to every sibling latch.
    if (pullDiagnosticsUnsupported_.contains(state->connectionKey)) {
        return; // learned once that this server doesn't support textDocument/diagnostic
    }
    Client* client = ExistingClientForLanguage(state->connectionKey);
    if (!client) {
        return;
    }

    // uri/language captured by value, not the buffer itself: the response
    // arrives after a real async round trip, during which the buffer could
    // legitimately close -- resolved fresh (by uri, via bufferList_) inside
    // the callback below rather than holding a pointer across the gap, the
    // same resolve-fresh-not-capture-stale discipline
    // HandlePublishDiagnostics itself already follows for a notification
    // arriving whenever the server feels like sending it.
    const std::string uri           = state->uri;
    const std::string connectionKey = state->connectionKey; // the "stop asking" latch's own key
    const std::string sourceKey     = serverKey;            // diagnosticsBySource_/FilterToOwnedRanges' key -- per server, not per connection
    const Json        params        = {{"textDocument", {{"uri", uri}}}};
    // buffer-anchored-lsp-results follow-up: the document at request time --
    // the one the server answers about -- plus its generation and the
    // buffer's own identity. The identity is compared, never dereferenced:
    // this handler deliberately re-resolves the buffer by uri (it could have
    // closed), and a *different* buffer reopened at the same path has its own
    // unrelated generation counter, so a snapshot from the old one must not
    // be carried onto it.
    std::shared_ptr<const text::ITextStorage> requestedContent    = buffer.Content().Clone();
    const std::size_t                         requestedGeneration = buffer.ContentGeneration();
    const text::Buffer* const                 requestedBuffer     = &buffer;
    client->SendRequest(
        "textDocument/diagnostic", params,
        [this, uri, connectionKey, sourceKey, requestedContent, requestedGeneration,
         requestedBuffer](std::optional<Json> result, std::optional<Json> error) {
            if (error) {
                // A real error response (as opposed to a legitimate "no
                // diagnostics right now" empty items array) is this
                // server's own proof it doesn't implement the method --
                // stop asking for the rest of this connection's lifetime
                // rather than re-erroring on every sync.
                pullDiagnosticsUnsupported_.insert(connectionKey);
                return;
            }
            if (!result) {
                return;
            }
            const std::optional<std::vector<PullDiagnosticItem>> items = ExtractPullDiagnosticReport(*result);
            if (!items) {
                return; // an "unchanged" report, or nothing parseable -- leave the existing slice alone
            }
            const std::optional<std::filesystem::path> path   = UriToPath(uri);
            text::Buffer* const                        buffer = path ? bufferList_.FindByPath(*path) : nullptr;
            if (!buffer) {
                return; // buffer closed since this was requested
            }
            // Same buffer as the request was made against, or a different one
            // that happens to sit at the same path now -- only the former can
            // be carried forward from the request-time snapshot.
            const bool                sameBuffer = buffer == requestedBuffer;
            const text::ITextStorage& content    = sameBuffer ? *requestedContent : buffer->Content();

            std::vector<text::Buffer::Diagnostic> diagnostics;
            diagnostics.reserve(items->size());
            for (const PullDiagnosticItem& item : *items) {
                diagnostics.push_back(text::Buffer::Diagnostic{
                    .startByte = PositionToByte(content, item.start),
                    .endByte   = PositionToByte(content, item.end),
                    .severity  = SeverityFromLsp(item.severity),
                    .origin    = (sourceKey == kProseLanguageKey) ? text::Buffer::Diagnostic::Origin::Prose
                                                                  : text::Buffer::Diagnostic::Origin::Code,
                    .message   = item.message,
                });
            }
            DiagnosticSlice slice{.diagnostics          = std::move(diagnostics),
                                  .resolvedAtGeneration = sameBuffer ? requestedGeneration : buffer->ContentGeneration()};
            // Before FilterToOwnedRanges, which asks about the *live*
            // buffer's embedded-language ranges -- the same ordering
            // HandlePublishDiagnostics' own stale-publish branch keeps.
            RebaseSliceOntoLiveContent(*buffer, slice);
            FilterToOwnedRanges(buffer, sourceKey, slice.diagnostics);
            // Same source-key slot HandlePublishDiagnostics writes into --
            // see this method's own doc comment in Manager.h for why
            // that's the deliberate choice here.
            diagnosticsBySource_[buffer][sourceKey] = std::move(slice);
            PushMergedDiagnostics(*buffer);
        });
}

void Manager::ApplyDecodedSemanticTokens(text::Buffer& buffer, const std::vector<SemanticToken>& tokens,
                                         const SemanticTokensLegend& legend, const text::ITextStorage& resolvedContent,
                                         std::size_t                                        resolvedGeneration,
                                         std::optional<std::pair<std::size_t, std::size_t>> answeredRange) {
    const text::ITextStorage&          content = resolvedContent;
    std::vector<editor::HighlightSpan> spans;
    spans.reserve(tokens.size());
    for (const SemanticToken& token : tokens) {
        if (token.tokenTypeIndex >= legend.tokenTypes.size()) {
            continue; // out-of-range index -- a malformed/mismatched-legend response, skip rather than crash
        }
        const std::optional<editor::SyntaxClass> syntaxClass = SyntaxClassForSemanticTokenType(legend.tokenTypes[token.tokenTypeIndex]);
        if (!syntaxClass) {
            continue; // no sensible existing class for this token type -- dropped, not force-fit
        }
        // A semantic token never spans multiple lines (per spec) -- its end
        // is always {start.line, start.character + length}.
        const Position end{.line = token.start.line, .character = token.start.character + token.length};
        spans.push_back(editor::HighlightSpan{
            .startByte   = PositionToByte(content, token.start),
            .endByte     = PositionToByte(content, end),
            .syntaxClass = *syntaxClass,
        });
    }
    // Resolved against the document the server answered about; carry them
    // onto whatever the buffer is now. A token whose text an edit rewrote is
    // dropped rather than left recolouring whatever now sits there -- that
    // mis-colouring is the whole symptom this family of fixes exists for.
    std::size_t resolvedAt = resolvedGeneration;
    const bool  carried    = CarryForward(spans, resolvedAt, buffer,
                                          [](editor::HighlightSpan& span, const std::vector<text::EditOp>& ops) {
                                          return RelocateRange(span.startByte, span.endByte, ops, kSemanticTokenInsideDelete);
                                          });

    if (answeredRange && carried) {
        // A range response describes its own slice and nothing else, so the
        // spans outside it are still the best answer anyone has for those
        // bytes -- replacing them is what made a scroll recolour the whole
        // screen and then have to ask for it all back. The answered range
        // travels the same path its spans did, from the requested document's
        // coordinates onto the present.
        std::size_t rangeStart = answeredRange->first;
        std::size_t rangeEnd   = answeredRange->second;
        if (const auto ops = buffer.Edits().OpsSince(resolvedGeneration)) {
            RelocateRange(rangeStart, rangeEnd, *ops, text::InsideDelete::Clamp);
        }
        std::vector<editor::HighlightSpan>& existing = semanticTokenSpans_[&buffer];
        CarryForward(existing, semanticTokenSpansContentGeneration_[&buffer], buffer,
                     [](editor::HighlightSpan& span, const std::vector<text::EditOp>& ops) {
                         return RelocateRange(span.startByte, span.endByte, ops, kSemanticTokenInsideDelete);
                     });
        for (editor::HighlightSpan& span : existing) {
            if (span.startByte < rangeStart || span.startByte >= rangeEnd) {
                spans.push_back(span); // outside what this response spoke about
            }
        }
        std::sort(spans.begin(), spans.end(), [](const editor::HighlightSpan& a, const editor::HighlightSpan& b) {
            return a.startByte < b.startByte;
        });
    }

    semanticTokenSpans_[&buffer]                  = std::move(spans);
    semanticTokenSpansContentGeneration_[&buffer] = buffer.ContentGeneration();
    ++semanticTokensGeneration_[&buffer];
}

bool Manager::SendViewportFeatures(text::Buffer& buffer, const ArmedViewportRequest& request) {
    // The same gate all three requests apply individually, hoisted so the
    // caller knows whether anything was actually sent: a pair the buffer has
    // moved off, or a generation the server hasn't been sent yet, must leave
    // nothing armed behind, or the frame that follows would compare equal to
    // it and never retry.
    const BufferSyncState* state = ResolveSyncState(buffer, request.serverKey);
    if (!state || !state->opened || request.generation != buffer.ContentGeneration() ||
        state->lastSyncedGeneration != buffer.ContentGeneration()) {
        return false;
    }
    RequestSemanticTokens(buffer, request.viewportStartByte, request.viewportEndByte, request.serverKey);
    RequestInlayHints(buffer, request.viewportStartByte, request.viewportEndByte, request.serverKey);
    RequestCodeLenses(buffer, request.serverKey);
    return true;
}

void Manager::RequestViewportFeatures(text::Buffer& buffer, std::size_t viewportStartByte, std::size_t viewportEndByte,
                                      const std::string& serverKey) {
    const ArmedViewportRequest desired{.serverKey         = serverKey,
                                       .generation        = buffer.ContentGeneration(),
                                       .viewportStartByte = viewportStartByte,
                                       .viewportEndByte   = viewportEndByte};
    if (const auto it = armedViewportRequests_.find(&buffer); it != armedViewportRequests_.end() && it->second == desired) {
        return; // this pair was already sent, or a pending fire is already carrying it
    }
    armedViewportRequests_[&buffer] = desired;

    const auto window   = std::chrono::milliseconds(RequestIdleMs());
    const auto now      = std::chrono::steady_clock::now();
    const auto sentIt   = lastViewportRequestAt_.find(&buffer);
    const bool inWindow = sentIt != lastViewportRequestAt_.end() && now - sentIt->second < window;
    if (!inWindow) {
        // Leading edge: a discrete jump (a PageDown, a click into a new
        // file) is one pair change with nothing before it, and paying the
        // window for that would make every such jump feel slow for no
        // saving at all.
        if (SendViewportFeatures(buffer, desired)) {
            lastViewportRequestAt_[&buffer] = now;
        }
        else {
            armedViewportRequests_.erase(&buffer);
        }
        return;
    }
    if (!viewportRequestPending_.insert(&buffer).second) {
        return; // a fire is already on its way, and it reads whatever pair is armed when it lands -- re-arming per frame is what would cost a thread per frame
    }
    viewportRequestTimers_[&buffer].Arm(
        eventLoop_, std::chrono::duration_cast<std::chrono::milliseconds>(window - (now - sentIt->second)),
        [this, bufferPtr = &buffer] {
            viewportRequestPending_.erase(bufferPtr);
            // Nothing dereferences bufferPtr before this lookup, deliberately:
            // a fire already Post()ed when NotifyBufferClosed ran still arrives,
            // and the erased entry is what tells it the buffer is gone.
            const auto it = armedViewportRequests_.find(bufferPtr);
            if (it == armedViewportRequests_.end()) {
                return;
            }
            if (SendViewportFeatures(*bufferPtr, it->second)) {
                lastViewportRequestAt_[bufferPtr] = std::chrono::steady_clock::now();
            }
            else {
                armedViewportRequests_.erase(it);
            }
        });
}

void Manager::RequestSemanticTokens(text::Buffer& buffer, std::size_t viewportStartByte, std::size_t viewportEndByte,
                                    const std::string& serverKey) {
    if (!SemanticHighlightingEnabled()) {
        return;
    }
    BufferSyncState* state = ResolveSyncState(buffer, serverKey);
    if (!state || !state->opened) {
        return;
    }
    // sync-debounce follow-up: the server may not have this generation's
    // content yet -- SyncToServer's own didChange send is now debounced
    // (SyncDebounceMs), so a Paint() can reach here before it's landed.
    // Retried on the next Paint() once it does (see SyncToServer's own doc
    // comment for why that's guaranteed to happen without extra plumbing).
    if (state->lastSyncedGeneration != buffer.ContentGeneration()) {
        return;
    }
    const std::optional<SemanticTokensLegend> legend = SemanticTokensLegendFor(state->connectionKey);
    if (!legend) {
        return; // server never advertised a legend -- any of the three responses would be undecodable anyway
    }
    Client* client = ExistingClientForLanguage(state->connectionKey);
    if (!client) {
        return;
    }

    const SemanticTokensLegend legendCopy = *legend;
    text::Buffer* const        bufferPtr  = &buffer;
    // Copied out of state before any SendRequest: every "learned once, stop
    // asking" latch below is per connection, and state may not outlive the
    // async round trip.
    const std::string connectionKey = state->connectionKey;

    // range/delta follow-up: range is preferred whenever the server both
    // advertised it and hasn't since proven (a real error response) that it
    // doesn't actually honor it -- see RequestSemanticTokens' own header
    // doc comment for the full three-way decision this mirrors.
    if (legendCopy.rangeSupported && !semanticTokensRangeUnsupported_.contains(state->connectionKey)) {
        ViewportCoverage&                                        coverage = semanticTokensCoverage_[&buffer];
        const std::optional<std::pair<std::size_t, std::size_t>> request =
            UncoveredRequestRange(coverage, buffer, viewportStartByte, viewportEndByte);
        if (!request) {
            return; // already answered, or already on the wire
        }
        const std::size_t requestStart = request->first;
        const std::size_t requestEnd   = request->second;

        coverage.inFlight                     = std::pair{requestStart, requestEnd};
        const std::size_t requestId           = ++semanticTokensRequestCounter_[&buffer];
        const std::size_t requestedGeneration = buffer.ContentGeneration();
        // The document the server will answer about, kept so its positions
        // convert against the right text however long the round trip takes.
        std::shared_ptr<const text::ITextStorage> requestedContent = buffer.Content().Clone();

        const text::ITextStorage& content = buffer.Content();
        const Position            start   = BytePositionToLsp(content, requestStart);
        const Position            end     = BytePositionToLsp(content, requestEnd);
        const Json                params  = {
            {"textDocument", {{"uri", state->uri}}},
            {"range", {{"start", {{"line", start.line}, {"character", start.character}}}, {"end", {{"line", end.line}, {"character", end.character}}}}},
        };
        client->SendRequest(
            "textDocument/semanticTokens/range", params,
            [this, bufferPtr, requestId, requestedGeneration, requestedContent, legendCopy, connectionKey, requestStart,
             requestEnd](std::optional<Json> result, std::optional<Json> error) {
                const auto counterIt = semanticTokensRequestCounter_.find(bufferPtr);
                if (counterIt == semanticTokensRequestCounter_.end() || counterIt->second != requestId) {
                    return; // superseded by a newer request for this buffer
                }
                const auto settle = [this, bufferPtr, requestedGeneration, requestStart, requestEnd](bool answered) {
                    if (const auto it = semanticTokensCoverage_.find(bufferPtr); it != semanticTokensCoverage_.end()) {
                        SettleCoverage(it->second, requestedGeneration, answered, requestStart, requestEnd);
                    }
                };
                if (error) {
                    // A real error response is this server's own proof it
                    // doesn't actually honor a capability it advertised --
                    // stop asking for the rest of this connection's
                    // lifetime rather than re-erroring on every viewport
                    // change. The next request for this buffer falls
                    // through to the full/delta path below instead.
                    semanticTokensRangeUnsupported_.insert(connectionKey);
                    settle(/*answered=*/false);
                    return;
                }
                if (!result) {
                    settle(/*answered=*/false);
                    return;
                }
                ApplyDecodedSemanticTokens(*bufferPtr, DecodeSemanticTokenData(ExtractSemanticTokensRawData(*result)),
                                           legendCopy, *requestedContent, requestedGeneration,
                                           std::pair{requestStart, requestEnd});
                settle(/*answered=*/true);
            });
        return;
    }

    // Full / full-delta path -- generation-only dedup (whole-document
    // scope, no viewport to also key on).
    if (const auto it = semanticTokensRequestedGeneration_.find(&buffer);
        it != semanticTokensRequestedGeneration_.end() && it->second == buffer.ContentGeneration()) {
        return; // already requested for this exact content -- a cursor-blink/scroll-only repaint, not a real change
    }
    semanticTokensRequestedGeneration_[&buffer] = buffer.ContentGeneration();
    const std::size_t requestId                 = ++semanticTokensRequestCounter_[&buffer];
    const std::size_t requestedGeneration       = buffer.ContentGeneration();
    // See the range branch above: the document the server answers about.
    std::shared_ptr<const text::ITextStorage> requestedContent = buffer.Content().Clone();

    const bool useDelta = legendCopy.fullDeltaSupported && !semanticTokensFullDeltaUnsupported_.contains(state->connectionKey) &&
                          previousSemanticTokens_.contains(&buffer);
    if (useDelta) {
        const Json params = {
            {"textDocument", {{"uri", state->uri}}},
            {"previousResultId", previousSemanticTokens_.at(&buffer).resultId},
        };
        client->SendRequest(
            "textDocument/semanticTokens/full/delta", params,
            [this, bufferPtr, requestId, requestedGeneration, requestedContent, legendCopy,
             connectionKey](std::optional<Json> result, std::optional<Json> error) {
                const auto counterIt = semanticTokensRequestCounter_.find(bufferPtr);
                if (counterIt == semanticTokensRequestCounter_.end() || counterIt->second != requestId) {
                    return; // superseded by a newer request for this buffer
                }
                if (error) {
                    // Same "learned once, stop asking" latch as the range
                    // branch above -- a server that advertised
                    // full.delta:true but errors on the actual request
                    // falls back to plain full requests for the rest of
                    // this connection's lifetime.
                    semanticTokensFullDeltaUnsupported_.insert(connectionKey);
                    return;
                }
                if (!result) {
                    return;
                }
                // stale-position-race follow-up: the response's Position
                // values were computed by the server against the document as
                // it stood AT REQUEST TIME, so converting them against
                // bufferPtr->Content() would silently land on the wrong bytes
                // once any local edit had landed in flight -- a real,
                // live-reported bug (syntax colouring visibly detached from
                // the characters it belonged to after every keystroke). This
                // used to be answered by discarding the whole response, which
                // during continuous typing meant discarding essentially all
                // of them; requestedContent/requestedGeneration below convert
                // and then carry it forward instead.
                //
                // A SemanticTokensDelta response carries "edits" (applied
                // against the cached baseline); a server that decided to
                // resend the whole document instead carries "data" like any
                // plain full response -- ExtractSemanticTokensDeltaEdits
                // returning nullopt is exactly that "not delta-shaped" case.
                std::vector<std::uint32_t> rawData;
                if (const auto edits = ExtractSemanticTokensDeltaEdits(*result)) {
                    const auto                        prevIt = previousSemanticTokens_.find(bufferPtr);
                    const std::vector<std::uint32_t>& previousData =
                        prevIt != previousSemanticTokens_.end() ? prevIt->second.rawData : std::vector<std::uint32_t>{};
                    rawData = ApplySemanticTokensDeltaEdits(previousData, *edits);
                }
                else {
                    rawData = ExtractSemanticTokensRawData(*result);
                }
                if (const auto resultId = ExtractSemanticTokensResultId(*result)) {
                    previousSemanticTokens_[bufferPtr] = PreviousSemanticTokens{.resultId = *resultId, .rawData = rawData};
                }
                else {
                    previousSemanticTokens_.erase(bufferPtr); // server stopped offering a resultId -- start fresh next time
                }
                ApplyDecodedSemanticTokens(*bufferPtr, DecodeSemanticTokenData(rawData), legendCopy, *requestedContent,
                                           requestedGeneration);
            });
        return;
    }

    const Json params = {{"textDocument", {{"uri", state->uri}}}};
    client->SendRequest(
        "textDocument/semanticTokens/full", params,
        [this, bufferPtr, requestId, requestedGeneration, requestedContent, legendCopy](std::optional<Json> result,
                                                                                        std::optional<Json> error) {
            const auto counterIt = semanticTokensRequestCounter_.find(bufferPtr);
            if (counterIt == semanticTokensRequestCounter_.end() || counterIt->second != requestId) {
                return; // superseded by a newer request for this buffer
            }
            if (error || !result) {
                return; // leave whatever spans were already applied in place
            }
            const std::vector<std::uint32_t> rawData = ExtractSemanticTokensRawData(*result);
            // range/delta follow-up: seed the delta baseline here too, not
            // just in the full/delta branch above -- this is the very
            // first request for a buffer whose server supports delta (no
            // previousResultId to send yet), and its response is the
            // baseline the *next* request deltas against.
            if (const auto resultId = ExtractSemanticTokensResultId(*result)) {
                previousSemanticTokens_[bufferPtr] = PreviousSemanticTokens{.resultId = *resultId, .rawData = rawData};
            }
            ApplyDecodedSemanticTokens(*bufferPtr, DecodeSemanticTokenData(rawData), legendCopy, *requestedContent,
                                       requestedGeneration);
        });
}

const std::vector<editor::HighlightSpan>& Manager::SemanticTokenSpans(const text::Buffer& buffer) const {
    static const std::vector<editor::HighlightSpan> kEmpty;
    const auto                                      it = semanticTokenSpans_.find(const_cast<text::Buffer*>(&buffer));
    if (it == semanticTokenSpans_.end()) {
        return kEmpty;
    }
    // These spans are byte ranges resolved against the document the server
    // answered about. Handing them out against a document that has moved does
    // not displace any text -- it recolours the *wrong characters*, so
    // colours, bolds, italics and underlines drift out of step with the code
    // they belong to while you type (live-reported 2026-09-10, right after
    // the annotation rows stopped moving and made this the visible artifact).
    //
    // That used to be answered by serving nothing at all once the buffer
    // moved, which is cheap and correct here in a way it would not be for
    // some features -- the grammar's own highlighting underneath is a
    // complete answer, and is exactly what the buffer showed before the
    // server ever replied. It also meant the server's contribution blinked
    // out on every keystroke and came back a round trip later. Carrying the
    // spans forward keeps them on the characters they describe instead, and
    // drops only the individual tokens whose own text an edit rewrote.
    const auto generationIt = semanticTokenSpansContentGeneration_.find(const_cast<text::Buffer*>(&buffer));
    if (generationIt != semanticTokenSpansContentGeneration_.end()) {
        CarryForward(it->second, generationIt->second, buffer,
                     [](editor::HighlightSpan& span, const std::vector<text::EditOp>& ops) {
                         return RelocateRange(span.startByte, span.endByte, ops, kSemanticTokenInsideDelete);
                     });
    }
    return it->second;
}

std::size_t Manager::SemanticTokensGeneration(const text::Buffer& buffer) const {
    const auto it = semanticTokensGeneration_.find(const_cast<text::Buffer*>(&buffer));
    return it != semanticTokensGeneration_.end() ? it->second : 0;
}

void Manager::RequestInlayHints(text::Buffer& buffer, std::size_t viewportStartByte, std::size_t viewportEndByte,
                                   const std::string& serverKey) {
    if (!InlayHintsEnabled()) {
        return;
    }
    BufferSyncState* state = ResolveSyncState(buffer, serverKey);
    if (!state || !state->opened) {
        return;
    }
    if (inlayHintsUnsupported_.contains(state->connectionKey)) {
        return; // learned once that this server doesn't support textDocument/inlayHint
    }
    // sync-debounce follow-up: see RequestSemanticTokens' own doc
    // comment for why this guard exists now.
    if (state->lastSyncedGeneration != buffer.ContentGeneration()) {
        return;
    }
    ViewportCoverage&                                        coverage = inlayHintCoverage_[&buffer];
    const std::optional<std::pair<std::size_t, std::size_t>> request =
        UncoveredRequestRange(coverage, buffer, viewportStartByte, viewportEndByte);
    if (!request) {
        return; // already answered, or already on the wire
    }
    const std::size_t requestStart = request->first;
    const std::size_t requestEnd   = request->second;

    Client* client = ExistingClientForLanguage(state->connectionKey);
    if (!client) {
        return;
    }

    coverage.inFlight           = std::pair{requestStart, requestEnd};
    const std::size_t requestId = ++inlayHintsRequestCounter_[&buffer];

    const text::ITextStorage& content       = buffer.Content();
    const Position            start         = BytePositionToLsp(content, requestStart);
    const Position            end           = BytePositionToLsp(content, requestEnd);
    text::Buffer* const       bufferPtr     = &buffer;
    const std::string         connectionKey = state->connectionKey; // per-connection latch, see RequestSemanticTokens
    // Captured so the response handler can convert the server's
    // {line, character} positions against exactly the document it was asked
    // about, however long it takes to arrive. Only for that conversion --
    // carrying the result onto live content is CarryForward's job, off the
    // buffer's own edit journal.
    // O(1): ITextStorage::Clone() is structurally shared, never materialized.
    std::shared_ptr<const text::ITextStorage> requestedContent    = buffer.Content().Clone();
    const std::size_t                         requestedGeneration = buffer.ContentGeneration();
    const Json                                params              = {
        {"textDocument", {{"uri", state->uri}}},
        {"range", {{"start", {{"line", start.line}, {"character", start.character}}}, {"end", {{"line", end.line}, {"character", end.character}}}}},
    };
    client->SendRequest(
        "textDocument/inlayHint", params,
        [this, bufferPtr, requestId, connectionKey, requestedContent, requestedGeneration,
         requestedStartByte = requestStart,
         requestedEndByte   = requestEnd](std::optional<Json> result, std::optional<Json> error) {
            const auto counterIt = inlayHintsRequestCounter_.find(bufferPtr);
            if (counterIt == inlayHintsRequestCounter_.end() || counterIt->second != requestId) {
                return; // superseded by a newer request for this buffer
            }
            if (error) {
                // A real error response is this server's own proof it
                // doesn't implement the method -- stop asking for the rest
                // of this connection's lifetime rather than re-erroring on
                // every viewport change.
                inlayHintsUnsupported_.insert(connectionKey);
                SettleInlayHintRequest(*bufferPtr, requestedGeneration, /*answered=*/false, requestedStartByte, requestedEndByte);
                return;
            }
            if (!result) {
                SettleInlayHintRequest(*bufferPtr, requestedGeneration, /*answered=*/false, requestedStartByte, requestedEndByte);
                return;
            }
            // Positions are resolved against *requestedContent -- exactly the
            // document the server was asked about -- never against
            // bufferPtr->Content() here, which may already be a different
            // document by the time this response lands. CarryForward is what
            // takes the result from there onto whatever the buffer has
            // become, dropping anything the carry can't trust rather than
            // guessing.
            const std::vector<InlayHint>   hints   = ExtractInlayHints(*result);
            const text::ITextStorage&      content = *requestedContent;
            std::vector<ResolvedInlayHint> resolved;
            resolved.reserve(hints.size());
            // Traced inside the conversion, before the sort below reorders
            // it: the server's own position is the only thing that separates
            // "the server told us the wrong place" from "we converted it
            // wrongly", and after sorting the two are no longer lined up.
            const char* const rawTrace = std::getenv("NED_DEBUG_LSP_HINTS");
            std::ofstream     rawOut;
            if (rawTrace != nullptr && *rawTrace != '\0') {
                rawOut.open(rawTrace, std::ios::app);
                rawOut << "  raw response against docBytes=" << content.ByteLength() << '\n';
            }
            for (const InlayHint& hint : hints) {
                const std::size_t byteOffset = PositionToByte(content, hint.position);
                if (rawOut) {
                    rawOut << "    raw serverPos=" << hint.position.line << ':' << hint.position.character
                           << " lineStart=" << content.LineToByteOffset(hint.position.line) << " byte=" << byteOffset
                           << " label=" << hint.label << '\n';
                }
                resolved.push_back(ResolvedInlayHint{.byteOffset = byteOffset, .label = hint.label});
            }
            std::sort(resolved.begin(), resolved.end(),
                      [](const ResolvedInlayHint& a, const ResolvedInlayHint& b) { return a.byteOffset < b.byteOffset; });
            std::size_t       resolvedAt = requestedGeneration;
            const std::size_t beforeCarry = resolved.empty() ? 0 : resolved.front().byteOffset;
            const bool        carried     = CarryForward(resolved, resolvedAt, *bufferPtr,
                                                  [](ResolvedInlayHint& hint, const std::vector<text::EditOp>& ops) {
                                                      return RelocatePoint(hint.byteOffset, ops, kInlayHintAnchor);
                                                  });
            // NED_DEBUG_LSP_HINTS=<path>: the numbers behind a hint landing in
            // the wrong column. A carry that did not happen and one that
            // happened by the wrong amount look identical on screen.
            if (const char* tracePath = std::getenv("NED_DEBUG_LSP_HINTS"); tracePath != nullptr && *tracePath != '\0') {
                if (std::ofstream trace{tracePath, std::ios::app}) {
                    const std::optional<std::vector<text::EditOp>> ops = bufferPtr->Edits().OpsSince(requestedGeneration);
                    trace << "inlayHint response: requestedGeneration=" << requestedGeneration
                          << " liveGeneration=" << bufferPtr->ContentGeneration() << " opsSince="
                          << (ops ? std::to_string(ops->size()) : std::string("UNREACHABLE")) << " carried=" << carried
                          << " hints=" << resolved.size() << " firstOffset " << beforeCarry << " -> "
                          << (resolved.empty() ? 0 : resolved.front().byteOffset) << '\n';
                    if (ops) {
                        for (const text::EditOp& op : *ops) {
                            trace << "    op gen=" << op.generation << " offset=" << op.offset << " old=" << op.oldLength
                                  << " new=" << op.newLength << " barrier=" << op.barrier << '\n';
                        }
                    }
                    // Every hint as stored -- the first one converting
                    // correctly says nothing about the rest. Logged after the
                    // sort/carry, so this is exactly what the painter reads;
                    // the server's own positions are not lined up with it any
                    // more and would mislead rather than help.
                    for (const ResolvedInlayHint& hint : resolved) {
                        trace << "    hint byte=" << hint.byteOffset << " label=" << hint.label << '\n';
                    }
                }
            }
            if (!carried) {
                // The journal could not reach back to the generation this
                // response was computed against, so there is no honest place
                // to put these hints -- and no honest range to clear either.
                // Leaving the retained set alone keeps whatever is on screen
                // rather than punching a hole in it.
                SettleInlayHintRequest(*bufferPtr, requestedGeneration, /*answered=*/false, requestedStartByte, requestedEndByte);
                return;
            }
            // The answered range travels the same path its hints did: it is
            // stated in the requested document's coordinates, and the merge
            // below compares it against anchors that live in the present.
            std::size_t rangeStart = requestedStartByte;
            std::size_t rangeEnd   = requestedEndByte;
            if (const auto ops = bufferPtr->Edits().OpsSince(requestedGeneration)) {
                RelocateRange(rangeStart, rangeEnd, *ops, text::InsideDelete::Clamp);
            }
            MergeInlayHints(*bufferPtr, std::move(resolved), rangeStart, rangeEnd);
            SettleInlayHintRequest(*bufferPtr, requestedGeneration, /*answered=*/true, requestedStartByte, requestedEndByte);
        });
}

bool Manager::RelocatePoint(std::size_t& offset, const std::vector<text::EditOp>& ops, text::AnchorPolicy policy) {
    const std::optional<std::size_t> moved = text::RelocateThroughAll(offset, ops, policy);
    if (!moved) {
        return false;
    }
    offset = *moved;
    return true;
}

bool Manager::RelocateRange(std::size_t& startByte, std::size_t& endByte, const std::vector<text::EditOp>& ops,
                            text::InsideDelete insideDelete) {
    const std::optional<std::size_t> movedStart =
        text::RelocateThroughAll(startByte, ops, {.gravity = text::Gravity::Right, .insideDelete = insideDelete});
    const std::optional<std::size_t> movedEnd =
        text::RelocateThroughAll(endByte, ops, {.gravity = text::Gravity::Left, .insideDelete = insideDelete});
    if (!movedStart || !movedEnd) {
        return false;
    }
    startByte = *movedStart;
    // The two gravities can cross when an edit eats the range from both
    // sides; a degenerate range is kept rather than dropped, matching what
    // the diff path did, and leaves the decision to whoever renders it.
    endByte = std::max(*movedStart, *movedEnd);
    return true;
}

const std::vector<Manager::ResolvedInlayHint>& Manager::InlayHintSpans(const text::Buffer& buffer) const {
    static const std::vector<ResolvedInlayHint> kEmpty;
    text::Buffer* const                         key       = const_cast<text::Buffer*>(&buffer);
    const auto                                  anchorsIt = inlayHintAnchors_.find(key);
    if (anchorsIt == inlayHintAnchors_.end()) {
        return kEmpty;
    }

    const std::size_t generation = buffer.ContentGeneration();
    const auto        revisionIt = inlayHintRevision_.find(key);
    const std::size_t revision   = revisionIt != inlayHintRevision_.end() ? revisionIt->second : 0;

    InlayHintView& view = inlayHintView_[key];
    if (view.valid && view.builtAtGeneration == generation && view.builtAtRevision == revision) {
        return view.hints; // nothing moved and nothing merged since -- a pure cache hit
    }

    // A rebuild, not a relocation: the buffer moved these anchors when the
    // edit happened. A hint whose anchor is gone is one whose own text an
    // edit rewrote (kInlayHintAnchor invalidates rather than clamps), and it
    // is dropped here -- the slot itself is reclaimed at the next merge,
    // which is the next moment a non-const buffer is in hand.
    view.hints.clear();
    view.hints.reserve(anchorsIt->second.size());
    for (const AnchoredInlayHint& hint : anchorsIt->second) {
        if (const std::optional<std::size_t> offset = buffer.AnchorOffset(hint.anchor)) {
            view.hints.push_back(ResolvedInlayHint{.byteOffset = *offset, .label = hint.label});
        }
    }
    view.builtAtGeneration = generation;
    view.builtAtRevision   = revision;
    view.valid             = true;
    return view.hints;
}

void Manager::MergeInlayHints(text::Buffer& buffer, std::vector<ResolvedInlayHint> resolved, std::size_t rangeStart,
                              std::size_t rangeEnd) {
    std::vector<AnchoredInlayHint>& retained = inlayHintAnchors_[&buffer];
    std::vector<AnchoredInlayHint>  kept;
    kept.reserve(retained.size() + resolved.size());
    for (AnchoredInlayHint& hint : retained) {
        const std::optional<std::size_t> offset = buffer.AnchorOffset(hint.anchor);
        // Superseded two ways: the anchor is gone (an edit rewrote the text
        // it named), or it sits inside the range this response just answered
        // for and the response is now the better answer for that region --
        // including when the response has no hint there at all, which is how
        // a hint the server withdrew actually disappears.
        if (!offset || (*offset >= rangeStart && *offset < rangeEnd)) {
            buffer.DestroyAnchor(hint.anchor);
            continue;
        }
        kept.push_back(std::move(hint));
    }
    for (ResolvedInlayHint& hint : resolved) {
        kept.push_back(AnchoredInlayHint{.anchor = buffer.CreateAnchor(hint.byteOffset, kInlayHintAnchor),
                                         .label  = std::move(hint.label)});
    }
    EvictInlayHintsBeyondCap(buffer, kept, rangeStart);
    // Sorted here, once, so the read path stays a linear copy: anchor
    // relocation is monotonic, so an ordering established now survives every
    // edit until the next merge disturbs it.
    std::sort(kept.begin(), kept.end(), [&buffer](const AnchoredInlayHint& a, const AnchoredInlayHint& b) {
        return buffer.AnchorOffset(a.anchor).value_or(0) < buffer.AnchorOffset(b.anchor).value_or(0);
    });
    retained = std::move(kept);
    ++inlayHintRevision_[&buffer];
}

std::optional<std::pair<std::size_t, std::size_t>> Manager::UncoveredRequestRange(ViewportCoverage&   coverage,
                                                                                  const text::Buffer& buffer,
                                                                                  std::size_t         viewportStartByte,
                                                                                  std::size_t         viewportEndByte) {
    if (coverage.generation != buffer.ContentGeneration()) {
        // An edit anywhere can change an answer anywhere, so nothing said
        // about the old content is still an answer about this one.
        coverage.generation = buffer.ContentGeneration();
        coverage.ranges.clear();
        coverage.inFlight.reset();
    }
    if (RangeIsCovered(coverage.ranges, viewportStartByte, viewportEndByte) ||
        (coverage.inFlight && coverage.inFlight->first <= viewportStartByte &&
         coverage.inFlight->second >= viewportEndByte)) {
        return std::nullopt;
    }

    // Asked for a screenful either side of what is actually visible, so the
    // next wheel notch in either direction lands on ground already covered
    // and sends nothing at all. The viewport's own byte span is the unit
    // because it is exactly one screenful by construction, with no viewport
    // height to plumb down here to say so.
    const std::size_t margin = viewportEndByte - viewportStartByte;
    return std::pair{viewportStartByte > margin ? viewportStartByte - margin : 0,
                     std::min(viewportEndByte + margin, buffer.Content().ByteLength())};
}

void Manager::SettleCoverage(ViewportCoverage& coverage, std::size_t requestedGeneration, bool answered,
                             std::size_t rangeStart, std::size_t rangeEnd) {
    if (coverage.generation != requestedGeneration) {
        return; // coverage for that generation is already gone, in-flight entry with it
    }
    coverage.inFlight.reset();
    if (answered) {
        AddCoveredRange(coverage.ranges, rangeStart, rangeEnd);
    }
}

void Manager::SettleInlayHintRequest(text::Buffer& buffer, std::size_t requestedGeneration, bool answered,
                                     std::size_t rangeStart, std::size_t rangeEnd) {
    if (const auto it = inlayHintCoverage_.find(&buffer); it != inlayHintCoverage_.end()) {
        SettleCoverage(it->second, requestedGeneration, answered, rangeStart, rangeEnd);
    }
}

void Manager::EvictInlayHintsBeyondCap(text::Buffer& buffer, std::vector<AnchoredInlayHint>& hints,
                                       std::size_t anchorByte) {
    if (hints.size() <= kMaxRetainedInlayHints) {
        return;
    }
    const auto distance = [&buffer, anchorByte](const AnchoredInlayHint& hint) {
        const std::size_t offset = buffer.AnchorOffset(hint.anchor).value_or(0);
        return offset > anchorByte ? offset - anchorByte : anchorByte - offset;
    };
    std::nth_element(hints.begin(), hints.begin() + static_cast<std::ptrdiff_t>(kMaxRetainedInlayHints), hints.end(),
                     [&distance](const AnchoredInlayHint& a, const AnchoredInlayHint& b) {
                         return distance(a) < distance(b);
                     });
    for (auto it = hints.begin() + static_cast<std::ptrdiff_t>(kMaxRetainedInlayHints); it != hints.end(); ++it) {
        buffer.DestroyAnchor(it->anchor);
    }
    hints.erase(hints.begin() + static_cast<std::ptrdiff_t>(kMaxRetainedInlayHints), hints.end());
}

void Manager::RequestCodeLenses(text::Buffer& buffer, const std::string& serverKey) {
    if (!CodeLensEnabled()) {
        return;
    }
    BufferSyncState* state = ResolveSyncState(buffer, serverKey);
    if (!state || !state->opened) {
        return;
    }
    if (codeLensUnsupported_.contains(state->connectionKey)) {
        return; // learned once that this server doesn't support textDocument/codeLens
    }
    // sync-debounce follow-up: see RequestSemanticTokens' own doc
    // comment for why this guard exists now.
    if (state->lastSyncedGeneration != buffer.ContentGeneration()) {
        return;
    }
    if (const auto it = codeLensRequestedGeneration_.find(&buffer);
        it != codeLensRequestedGeneration_.end() && it->second == buffer.ContentGeneration()) {
        return; // already requested for this exact content
    }
    Client* client = ExistingClientForLanguage(state->connectionKey);
    if (!client) {
        return;
    }

    codeLensRequestedGeneration_[&buffer] = buffer.ContentGeneration();
    const std::size_t requestId           = ++codeLensRequestCounter_[&buffer];

    text::Buffer* const bufferPtr     = &buffer;
    const std::string   connectionKey = state->connectionKey; // per-connection latch, see RequestSemanticTokens
    const Json          params        = {{"textDocument", {{"uri", state->uri}}}};
    // stale-offset-class follow-up. The document as it stands at *request*
    // time, which is the one the server will answer about -- the guard above
    // already refuses to ask unless the server is in sync with it. Kept so the
    // response's {line, character} positions can be converted against the
    // right text however long it takes to arrive; carrying the result onto
    // live content is CarryForward's job, off the buffer's own edit journal.
    //
    // Clone() is O(1): storage is structurally shared and never materialized
    // (see Buffer's own use of it across undo), so this costs a pointer, not a
    // copy of the document -- which is what makes holding it per in-flight
    // request reasonable at all.
    std::shared_ptr<const text::ITextStorage> requestedContent    = buffer.Content().Clone();
    const std::size_t                         requestedGeneration = buffer.ContentGeneration();
    client->SendRequest(
        "textDocument/codeLens", params,
        [this, bufferPtr, requestId, connectionKey, requestedContent,
         requestedGeneration](std::optional<Json> result, std::optional<Json> error) {
            const auto counterIt = codeLensRequestCounter_.find(bufferPtr);
            if (counterIt == codeLensRequestCounter_.end() || counterIt->second != requestId) {
                return; // superseded by a newer request for this buffer
            }
            if (error) {
                codeLensUnsupported_.insert(connectionKey);
                return;
            }
            if (!result) {
                return;
            }
            const std::vector<CodeLens>   lenses  = ExtractCodeLenses(*result);
            const text::ITextStorage&     content = *requestedContent;
            std::vector<ResolvedCodeLens> resolved;
            resolved.reserve(lenses.size());
            for (const CodeLens& lens : lenses) {
                resolved.push_back(ResolvedCodeLens{
                    .startByte        = PositionToByte(content, lens.start),
                    .endByte          = PositionToByte(content, lens.end),
                    .title            = lens.title,
                    .commandName      = lens.commandName,
                    .commandArguments = lens.commandArguments,
                    .hasCommand       = lens.hasCommand,
                    .raw              = lens.raw,
                });
            }
            std::sort(resolved.begin(), resolved.end(),
                      [](const ResolvedCodeLens& a, const ResolvedCodeLens& b) { return a.startByte < b.startByte; });
            // Resolved against the requested document; carry them forward to
            // whatever the buffer is now, and record the generation they are
            // valid against so CodeLensSpans can keep doing that as editing
            // continues.
            std::size_t resolvedAt = requestedGeneration;
            CarryForward(resolved, resolvedAt, *bufferPtr, [](ResolvedCodeLens& lens, const std::vector<text::EditOp>& ops) {
                return RelocateRange(lens.startByte, lens.endByte, ops, text::InsideDelete::Clamp);
            });
            codeLensSpans_[bufferPtr]           = std::move(resolved);
            codeLensSpansGeneration_[bufferPtr] = bufferPtr->ContentGeneration();
        });
}

const std::vector<Manager::ResolvedCodeLens>& Manager::CodeLensSpans(const text::Buffer& buffer) const {
    static const std::vector<ResolvedCodeLens> kEmpty;
    text::Buffer* const                        key = const_cast<text::Buffer*>(&buffer);
    const auto                                 it  = codeLensSpans_.find(key);
    if (it == codeLensSpans_.end()) {
        return kEmpty;
    }

    // Lazy catch-up, the last member of the stale-offset family. A lens owns a
    // whole extra screen row above the line it annotates, so a stale offset
    // does not merely misplace a label -- it puts that row above the wrong
    // line, and every line below moves.
    //
    // Carried forward rather than suppressed, for the same reason diagnostics
    // are: blanking the set would make the row itself blink in and out as you
    // type, which is the movement this is meant to stop. Done here on read
    // rather than at each edit because Manager has no hook into Buffer's own
    // edits -- and it is cheap: replaying however few ops have landed since,
    // amortized across however many reads that generation sees (Paint asks
    // twice a frame).
    //
    // Clamped, not invalidated, unlike an inlay hint: a lens owns a whole
    // extra row rather than columns inside a line, so one sitting at an edit
    // point is misplaced but never garbles the text it sits above -- and a
    // lens that vanished as you typed inside the function it counts would be
    // the flicker this is here to prevent.
    const auto generationIt = codeLensSpansGeneration_.find(key);
    if (generationIt != codeLensSpansGeneration_.end()) {
        CarryForward(it->second, generationIt->second, buffer,
                     [](ResolvedCodeLens& lens, const std::vector<text::EditOp>& ops) {
                         return RelocateRange(lens.startByte, lens.endByte, ops, text::InsideDelete::Clamp);
                     });
    }
    return it->second;
}

void Manager::ResolveCodeLens(text::Buffer& buffer, const ResolvedCodeLens& lens, ResolveCodeLensCallback callback,
                                 const std::string& serverKey) {
    BufferSyncState* state = ResolveSyncState(buffer, serverKey);
    if (!state || !state->opened) {
        callback(std::nullopt);
        return;
    }
    Client* client = ExistingClientForLanguage(state->connectionKey);
    if (!client) {
        callback(std::nullopt);
        return;
    }

    const std::size_t startByte = lens.startByte;
    const std::size_t endByte   = lens.endByte;
    client->SendRequest("codeLens/resolve", lens.raw,
                        [callback = std::move(callback), startByte, endByte](std::optional<Json> result, std::optional<Json> error) {
                            if (error || !result) {
                                callback(std::nullopt);
                                return;
                            }
                            const CodeLens resolved = ExtractSingleCodeLens(*result);
                            callback(ResolvedCodeLens{
                                .startByte        = startByte,
                                .endByte          = endByte,
                                .title            = resolved.title,
                                .commandName      = resolved.commandName,
                                .commandArguments = resolved.commandArguments,
                                .hasCommand       = resolved.hasCommand,
                                .raw              = resolved.raw,
                            });
                        });
}

namespace {
    // documentLink follow-up: the one place a raw DocumentLink target turns
    // into the path/url split ResolvedDocumentLink documents -- shared by
    // RequestDocumentLinks and ResolveDocumentLink so a resolved link is
    // classified exactly like an inline one. A target that claims to be a
    // file:// URI but doesn't parse (UriToPath returning nullopt) is left
    // with neither field set rather than passed to OpenUrl as a URL, which
    // it isn't.
    void ApplyDocumentLinkTarget(Manager::ResolvedDocumentLink& out, const DocumentLink& link) {
        out.needsResolve = !link.hasTarget;
        if (!link.hasTarget) {
            return;
        }
        if (link.target.starts_with("file://")) {
            if (const std::optional<std::filesystem::path> path = UriToPath(link.target)) {
                out.path = *path;
            }
            return;
        }
        out.url = link.target;
    }
} // namespace

void Manager::RequestDocumentLinks(text::Buffer& buffer, DocumentLinkCallback callback, const std::string& serverKey) {
    BufferSyncState* state = ResolveSyncState(buffer, serverKey);
    if (!state || !state->opened) {
        callback({});
        return;
    }
    if (documentLinkUnsupported_.contains(state->connectionKey)) {
        callback({}); // learned once that this server doesn't support textDocument/documentLink
        return;
    }
    Client* client = ExistingClientForLanguage(state->connectionKey);
    if (!client) {
        callback({});
        return;
    }

    text::Buffer* const bufferPtr     = &buffer;
    const std::string   connectionKey = state->connectionKey;
    const Json          params        = {{"textDocument", {{"uri", state->uri}}}};
    client->SendRequest("textDocument/documentLink", params,
                        [this, bufferPtr, connectionKey, callback = std::move(callback)](std::optional<Json> result,
                                                                                         std::optional<Json> error) {
                            if (error) {
                                documentLinkUnsupported_.insert(connectionKey);
                                LogError(connectionKey, ExtractErrorMessage(*error));
                                callback({});
                                return;
                            }
                            if (!result) {
                                callback({});
                                return;
                            }
                            // The buffer may have been edited (or closed and a
                            // new one allocated at the same address) while the
                            // request was in flight -- the caller's own
                            // staleness guard is what decides whether to act on
                            // these at all; converting positions against
                            // whatever content is here now is still the only
                            // meaningful thing to do with the response.
                            const text::ITextStorage&         content = bufferPtr->Content();
                            std::vector<ResolvedDocumentLink> resolved;
                            for (const DocumentLink& link : ExtractDocumentLinks(*result)) {
                                ResolvedDocumentLink entry{
                                    .startByte = PositionToByte(content, link.start),
                                    .endByte   = PositionToByte(content, link.end),
                                    .raw       = link.raw,
                                };
                                ApplyDocumentLinkTarget(entry, link);
                                resolved.push_back(std::move(entry));
                            }
                            std::sort(resolved.begin(), resolved.end(),
                                      [](const ResolvedDocumentLink& a, const ResolvedDocumentLink& b) {
                                          return a.startByte < b.startByte;
                                      });
                            callback(std::move(resolved));
                        });
}

void Manager::ResolveDocumentLink(text::Buffer& buffer, const ResolvedDocumentLink& link, ResolveDocumentLinkCallback callback,
                                     const std::string& serverKey) {
    BufferSyncState* state = ResolveSyncState(buffer, serverKey);
    if (!state || !state->opened) {
        callback(std::nullopt);
        return;
    }
    Client* client = ExistingClientForLanguage(state->connectionKey);
    if (!client) {
        callback(std::nullopt);
        return;
    }

    const std::size_t startByte = link.startByte;
    const std::size_t endByte   = link.endByte;
    client->SendRequest("documentLink/resolve", link.raw,
                        [callback = std::move(callback), startByte, endByte](std::optional<Json> result, std::optional<Json> error) {
                            if (error || !result) {
                                callback(std::nullopt);
                                return;
                            }
                            const DocumentLink   parsed = ExtractSingleDocumentLink(*result);
                            ResolvedDocumentLink entry{
                                .startByte = startByte, // the original range stands -- resolve only fills in a target
                                .endByte   = endByte,
                                .raw       = parsed.raw,
                            };
                            ApplyDocumentLinkTarget(entry, parsed);
                            callback(std::move(entry));
                        });
}

void Manager::PushMergedDiagnostics(text::Buffer& buffer) {
    std::vector<text::Buffer::Diagnostic> merged;
    if (const auto it = diagnosticsBySource_.find(&buffer); it != diagnosticsBySource_.end()) {
        for (auto& perSource : it->second) {
            // debounce-window-drift follow-up. A slice's offsets were resolved
            // against the document at resolvedAtGeneration, which is not the
            // document they are about to be applied to: this buffer kept being
            // typed into through DiagnosticsDebounceMs(), and a slice from
            // another source may have been sitting here far longer than that,
            // re-pushed verbatim every time any source fires. Remap onto the
            // live content and rebase the slice onto it, so each stored offset
            // is only ever one edit-span behind and the next push starts from
            // here rather than from the original parse.
            RebaseSliceOntoLiveContent(buffer, perSource.second);
            merged.insert(merged.end(), perSource.second.diagnostics.begin(), perSource.second.diagnostics.end());
        }
    }
    buffer.SetDiagnostics(std::move(merged));
}

void Manager::RebaseSliceOntoLiveContent(const text::Buffer& buffer, DiagnosticSlice& slice) const {
    // Gravity is what the diff path had to hand-special-case here: a
    // diagnostic start has right gravity for the same reason
    // Buffer::RelocateDiagnosticsForInsert gives it one -- text typed at a
    // flagged token's first byte was not part of what the server flagged, so
    // the underline moves along rather than growing over it -- and its end
    // has left gravity, so the underline does not swallow text typed just
    // past it either. RelocateRange pairs exactly that.
    CarryForward(slice.diagnostics, slice.resolvedAtGeneration, buffer,
                 [](text::Buffer::Diagnostic& diagnostic, const std::vector<text::EditOp>& ops) {
                     return RelocateRange(diagnostic.startByte, diagnostic.endByte, ops, kDiagnosticInsideDelete);
                 });
}

void Manager::HandleProgress(const std::string& connectionKey, const Json& params) {
    if (!params.contains("token") || !params.contains("value") || !params["value"].is_object()) {
        return;
    }
    const std::string key   = connectionKey + '\x1f' + params["token"].dump();
    const Json&       value = params["value"];
    const std::string kind  = value.value("kind", std::string());

    if (kind == "begin") {
        if (activeProgress_.contains(key)) {
            return; // duplicate begin for a live token -- ignore rather than double-count
        }
        activeProgress_[key] = value.value("title", std::string());
        BeginBackgroundActivity(kLspActivity);
    }

    const auto it = activeProgress_.find(key);
    if (it == activeProgress_.end()) {
        return; // report/end for a token that never began (or already ended)
    }

    if (kind == "end") {
        activeProgress_.erase(it);
        EndBackgroundActivity(kLspActivity);
        if (activeProgress_.empty()) {
            // No live progress session left to describe -- drop the stale
            // detail rather than letting it caption a plain request spinner.
            // A no-op if nothing is active at all (the entry is already gone).
            SetBackgroundActivityDetail(kLspActivity, std::string());
        }
        return;
    }

    // "begin" or "report": refresh the detail text. Percentage beats
    // message when both are present -- it's the more glanceable of the two.
    std::string detail = it->second;
    if (value.contains("percentage") && value["percentage"].is_number()) {
        const std::string percent = std::to_string(value["percentage"].get<int>()) + "%";
        detail += detail.empty() ? percent : " (" + percent + ")";
    }
    else if (const std::string message = value.value("message", std::string()); !message.empty()) {
        detail += detail.empty() ? message : ": " + message;
    }
    SetBackgroundActivityDetail(kLspActivity, std::move(detail));
}

void Manager::RequestHover(text::Buffer& buffer, std::size_t byteOffset, HoverCallback callback, const std::string& serverKey) {
    BufferSyncState* state = ResolveSyncState(buffer, serverKey);
    if (!state || !state->opened) {
        callback(std::nullopt); // never synced to a server -- nothing to ask
        return;
    }
    Client* client = ExistingClientForLanguage(state->connectionKey);
    if (!client) {
        callback(std::nullopt);
        return;
    }

    const std::string language = state->connectionKey;
    const Position position = BytePositionToLsp(buffer.Content(), byteOffset);
    const Json        params   = {
        {"textDocument", {{"uri", state->uri}}},
        {"position", {{"line", position.line}, {"character", position.character}}},
    };
    client->SendRequest("textDocument/hover", params,
                        [this, language, callback = std::move(callback)](std::optional<Json> result, std::optional<Json> error) {
                            if (error) {
                                LogError(language, ExtractErrorMessage(*error));
                                callback(std::nullopt);
                                return;
                            }
                            if (!result) {
                                callback(std::nullopt);
                                return;
                            }
                            callback(ExtractHoverText(*result));
                        });
}

void Manager::RequestCompletion(text::Buffer& buffer, std::size_t byteOffset, CompletionCallback callback, const std::string& serverKey,
                                   const std::string& triggerCharacter) {
    BufferSyncState* state = ResolveSyncState(buffer, serverKey);
    if (!state || !state->opened) {
        callback({});
        return;
    }
    Client* client = ExistingClientForLanguage(state->connectionKey);
    if (!client) {
        callback({});
        return;
    }

    const std::string language = state->connectionKey;
    const Position position = BytePositionToLsp(buffer.Content(), byteOffset);
    // completion-context follow-up, corrected by completion-trigger-characters:
    // triggerKind was hardcoded to 1 (Invoked) for every request, including
    // the ones a real server-declared trigger character caused -- a server
    // is entitled to answer those two cases differently (a "." request
    // should return members, not every in-scope symbol), and it has no
    // other way to tell them apart. Omitting "context" entirely, as this
    // did before that follow-up, left a strict server with no signal at all.
    Json context = {{"triggerKind", triggerCharacter.empty() ? 1 : 2}};
    if (!triggerCharacter.empty()) {
        context["triggerCharacter"] = triggerCharacter;
    }
    const Json params = {
        {"textDocument", {{"uri", state->uri}}},
        {"position", {{"line", position.line}, {"character", position.character}}},
        {"context", std::move(context)},
    };
    // completion-resolve follow-up: an item that declared no
    // commitCharacters of its own inherits the server's list-wide
    // allCommitCharacters. Applied here rather than in Content because
    // that layer only ever sees one response, never the initialize
    // capabilities this tier comes from -- and applied at receipt so every
    // consumer downstream reads one already-resolved field.
    std::vector<std::string> allCommitCharacters;
    if (const auto provider = CompletionProviderFor(state->connectionKey)) {
        allCommitCharacters = provider->allCommitCharacters;
    }
    client->SendRequest("textDocument/completion", params,
                        [this, language, allCommitCharacters = std::move(allCommitCharacters), callback = std::move(callback)](
                            std::optional<Json> result, std::optional<Json> error) {
                            if (error) {
                                LogError(language, ExtractErrorMessage(*error));
                                callback({});
                                return;
                            }
                            if (!result) {
                                callback({});
                                return;
                            }
                            CompletionList list = ExtractCompletionList(*result);
                            if (!allCommitCharacters.empty()) {
                                for (CompletionItem& item : list.items) {
                                    if (item.commitCharacters.empty()) {
                                        item.commitCharacters = allCommitCharacters;
                                    }
                                }
                            }
                            callback(std::move(list));
                        });
}

void Manager::ResolveCompletionItem(text::Buffer& buffer, const CompletionItem& item, ResolveCompletionCallback callback,
                                       const std::string& serverKey) {
    if (item.raw.is_null()) {
        callback(std::nullopt); // synthesized (dabbrev/Janet) item -- there is nothing to hand back
        return;
    }
    BufferSyncState* state = ResolveSyncState(buffer, serverKey);
    if (!state || !state->opened) {
        callback(std::nullopt);
        return;
    }
    Client* client = ExistingClientForLanguage(state->connectionKey);
    if (!client) {
        callback(std::nullopt);
        return;
    }

    const std::string language = state->connectionKey;
    client->SendRequest("completionItem/resolve", item.raw,
                        [this, language, callback = std::move(callback)](std::optional<Json> result, std::optional<Json> error) {
                            if (error) {
                                LogError(language, ExtractErrorMessage(*error));
                                callback(std::nullopt);
                                return;
                            }
                            if (!result) {
                                callback(std::nullopt);
                                return;
                            }
                            CompletionItem resolved = ExtractSingleCompletionItem(*result);
                            if (resolved.label.empty()) {
                                callback(std::nullopt); // unparseable/empty response -- nothing to merge
                                return;
                            }
                            callback(std::move(resolved));
                        });
}

void Manager::RequestCodeActions(text::Buffer& buffer, std::size_t rangeStartByte, std::size_t rangeEndByte, CodeActionCallback callback,
                                    const std::string& serverKey) {
    BufferSyncState* state = ResolveSyncState(buffer, serverKey);
    if (!state || !state->opened) {
        callback({});
        return;
    }
    Client* client = ExistingClientForLanguage(state->connectionKey);
    if (!client) {
        callback({});
        return;
    }

    const text::ITextStorage& content = buffer.Content();
    const Position         start   = BytePositionToLsp(content, rangeStartByte);
    const Position         end     = BytePositionToLsp(content, rangeEndByte);

    Json diagnostics = Json::array();
    for (const text::Buffer::Diagnostic& diagnostic : buffer.Diagnostics()) {
        if (diagnostic.endByte <= rangeStartByte || diagnostic.startByte >= rangeEndByte) {
            continue; // doesn't overlap the requested range
        }
        diagnostics.push_back(DiagnosticToLsp(diagnostic, content));
    }

    const std::string language = state->connectionKey;
    const std::string uri      = state->uri;
    const Json        params   = {
        {"textDocument", {{"uri", uri}}},
        {"range", {{"start", {{"line", start.line}, {"character", start.character}}}, {"end", {{"line", end.line}, {"character", end.character}}}}},
        {"context", {{"diagnostics", diagnostics}}},
    };
    client->SendRequest("textDocument/codeAction", params,
                        [this, language, callback = std::move(callback), uri](std::optional<Json> result, std::optional<Json> error) {
                            if (error) {
                                LogError(language, ExtractErrorMessage(*error));
                                callback({});
                                return;
                            }
                            if (!result) {
                                callback({});
                                return;
                            }
                            callback(ExtractCodeActions(*result, uri));
                        });
}

void Manager::ResolveCodeAction(text::Buffer& buffer, const CodeAction& action, ResolveCallback callback, const std::string& serverKey) {
    BufferSyncState* state = ResolveSyncState(buffer, serverKey);
    if (!state || !state->opened) {
        callback(std::nullopt);
        return;
    }
    Client* client = ExistingClientForLanguage(state->connectionKey);
    if (!client) {
        callback(std::nullopt);
        return;
    }

    const std::string language = state->connectionKey;
    const std::string uri      = state->uri;
    client->SendRequest("codeAction/resolve", action.raw,
                        [this, language, callback = std::move(callback), uri](std::optional<Json> result, std::optional<Json> error) {
                            if (error) {
                                LogError(language, ExtractErrorMessage(*error));
                                callback(std::nullopt);
                                return;
                            }
                            if (!result) {
                                callback(std::nullopt);
                                return;
                            }
                            callback(ExtractSingleCodeAction(*result, uri));
                        });
}

void Manager::ExecuteCommand(text::Buffer& buffer, const std::string& serverKey, const std::string& command, Json arguments,
                                ExecuteCommandCallback callback) {
    BufferSyncState* state = ResolveSyncState(buffer, serverKey);
    if (!state || !state->opened) {
        callback(false);
        return;
    }
    Client* client = ExistingClientForLanguage(state->connectionKey);
    if (!client) {
        callback(false);
        return;
    }

    const std::string language = state->connectionKey;
    const Json        params   = {{"command", command}, {"arguments", std::move(arguments)}};
    client->SendRequest("workspace/executeCommand", params,
                        [this, language, callback = std::move(callback)](std::optional<Json> result, std::optional<Json> error) {
                            (void)result; // discarded -- see this method's own doc comment in Manager.h
                            if (error) {
                                LogError(language, ExtractErrorMessage(*error));
                                callback(false);
                                return;
                            }
                            callback(true);
                        });
}

void Manager::SendLocationRequest(const std::string& method, text::Buffer& buffer, std::size_t byteOffset,
                                     DefinitionCallback callback, const std::string& serverKey, const Json& extraParams) {
    BufferSyncState* state = ResolveSyncState(buffer, serverKey);
    if (!state || !state->opened) {
        callback({});
        return;
    }
    Client* client = ExistingClientForLanguage(state->connectionKey);
    if (!client) {
        callback({});
        return;
    }

    const std::string language = state->connectionKey;
    const Position position = BytePositionToLsp(buffer.Content(), byteOffset);
    Json              params   = {
        {"textDocument", {{"uri", state->uri}}},
        {"position", {{"line", position.line}, {"character", position.character}}},
    };
    // find-references follow-up: extraParams merges in textDocument/references'
    // own "context" field ({"includeDeclaration": true}), the one place this
    // request's shape diverges from definition/declaration/typeDefinition/
    // implementation -- empty (every other caller) is a no-op merge.
    if (!extraParams.empty()) {
        params.merge_patch(extraParams);
    }
    client->SendRequest(method, params,
                        [this, language, callback = std::move(callback)](std::optional<Json> result, std::optional<Json> error) {
                            if (error) {
                                LogError(language, ExtractErrorMessage(*error));
                                callback({});
                                return;
                            }
                            if (!result) {
                                callback({});
                                return;
                            }
                            std::vector<ResolvedLocation> resolved;
                            for (const DefinitionLocation& location : ExtractDefinitionLocations(*result)) {
                                if (const std::optional<std::filesystem::path> path = UriToPath(location.uri)) {
                                    resolved.push_back(ResolvedLocation{.path = *path, .position = location.position});
                                }
                                // an unresolvable uri is dropped -- see ResolvedLocation's own doc comment
                            }
                            callback(std::move(resolved));
                        });
}

void Manager::RequestDefinition(text::Buffer& buffer, std::size_t byteOffset, DefinitionCallback callback, const std::string& serverKey) {
    SendLocationRequest("textDocument/definition", buffer, byteOffset, std::move(callback), serverKey);
}

void Manager::RequestDeclaration(text::Buffer& buffer, std::size_t byteOffset, DefinitionCallback callback,
                                    const std::string& serverKey) {
    SendLocationRequest("textDocument/declaration", buffer, byteOffset, std::move(callback), serverKey);
}

void Manager::RequestTypeDefinition(text::Buffer& buffer, std::size_t byteOffset, DefinitionCallback callback,
                                       const std::string& serverKey) {
    SendLocationRequest("textDocument/typeDefinition", buffer, byteOffset, std::move(callback), serverKey);
}

void Manager::RequestImplementation(text::Buffer& buffer, std::size_t byteOffset, DefinitionCallback callback,
                                       const std::string& serverKey) {
    SendLocationRequest("textDocument/implementation", buffer, byteOffset, std::move(callback), serverKey);
}

void Manager::RequestReferences(text::Buffer& buffer, std::size_t byteOffset, DefinitionCallback callback,
                                   const std::string& serverKey) {
    SendLocationRequest("textDocument/references", buffer, byteOffset, std::move(callback), serverKey,
                        Json{{"context", {{"includeDeclaration", true}}}});
}

void Manager::RequestSignatureHelp(text::Buffer& buffer, std::size_t byteOffset, HoverCallback callback, const std::string& serverKey) {
    BufferSyncState* state = ResolveSyncState(buffer, serverKey);
    if (!state || !state->opened) {
        callback(std::nullopt);
        return;
    }
    Client* client = ExistingClientForLanguage(state->connectionKey);
    if (!client) {
        callback(std::nullopt);
        return;
    }

    const std::string language = state->connectionKey;
    const Position position = BytePositionToLsp(buffer.Content(), byteOffset);
    const Json        params   = {
        {"textDocument", {{"uri", state->uri}}},
        {"position", {{"line", position.line}, {"character", position.character}}},
    };
    client->SendRequest("textDocument/signatureHelp", params,
                        [this, language, callback = std::move(callback)](std::optional<Json> result, std::optional<Json> error) {
                            if (error) {
                                LogError(language, ExtractErrorMessage(*error));
                                callback(std::nullopt);
                                return;
                            }
                            if (!result) {
                                callback(std::nullopt);
                                return;
                            }
                            callback(ExtractSignatureHelp(*result));
                        });
}

void Manager::RequestDocumentHighlight(text::Buffer& buffer, std::size_t byteOffset, DocumentHighlightCallback callback,
                                          const std::string& serverKey) {
    BufferSyncState* state = ResolveSyncState(buffer, serverKey);
    if (!state || !state->opened) {
        callback({});
        return;
    }
    Client* client = ExistingClientForLanguage(state->connectionKey);
    if (!client) {
        callback({});
        return;
    }

    const std::string language = state->connectionKey;
    const Position position = BytePositionToLsp(buffer.Content(), byteOffset);
    const Json        params   = {
        {"textDocument", {{"uri", state->uri}}},
        {"position", {{"line", position.line}, {"character", position.character}}},
    };
    client->SendRequest("textDocument/documentHighlight", params,
                        [this, language, callback = std::move(callback)](std::optional<Json> result, std::optional<Json> error) {
                            if (error) {
                                LogError(language, ExtractErrorMessage(*error));
                                callback({});
                                return;
                            }
                            if (!result) {
                                callback({});
                                return;
                            }
                            callback(ExtractDocumentHighlights(*result));
                        });
}

void Manager::RequestLinkedEditingRange(text::Buffer& buffer, std::size_t byteOffset, LinkedEditingRangeCallback callback,
                                           const std::string& serverKey) {
    BufferSyncState* state = ResolveSyncState(buffer, serverKey);
    if (!state || !state->opened) {
        callback({});
        return;
    }
    Client* client = ExistingClientForLanguage(state->connectionKey);
    if (!client) {
        callback({});
        return;
    }

    const std::string language = state->connectionKey;
    const Position position = BytePositionToLsp(buffer.Content(), byteOffset);
    const Json        params   = {
        {"textDocument", {{"uri", state->uri}}},
        {"position", {{"line", position.line}, {"character", position.character}}},
    };
    client->SendRequest("textDocument/linkedEditingRange", params,
                        [this, language, callback = std::move(callback)](std::optional<Json> result, std::optional<Json> error) {
                            if (error) {
                                LogError(language, ExtractErrorMessage(*error));
                                callback({});
                                return;
                            }
                            if (!result) {
                                callback({});
                                return;
                            }
                            callback(ExtractLinkedEditingRanges(*result));
                        });
}

void Manager::RequestSwitchSourceHeader(text::Buffer& buffer, SwitchHeaderCallback callback) {
    BufferSyncState* state = PrimarySyncState(buffer);
    if (!state || !state->opened) {
        callback(std::nullopt);
        return;
    }
    Client* client = ExistingClientForLanguage(state->connectionKey);
    if (!client) {
        callback(std::nullopt);
        return;
    }

    const std::string language = state->connectionKey;
    const Json        params   = {{"uri", state->uri}}; // bare TextDocumentIdentifier -- see this method's own header doc comment
    client->SendRequest("textDocument/switchSourceHeader", params,
                        [this, language, callback = std::move(callback)](std::optional<Json> result, std::optional<Json> error) {
                            if (error) {
                                LogError(language, ExtractErrorMessage(*error));
                                callback(std::nullopt);
                                return;
                            }
                            if (!result || !result->is_string()) {
                                callback(std::nullopt);
                                return;
                            }
                            callback(UriToPath(result->get<std::string>()));
                        });
}

void Manager::RequestPrepareRename(text::Buffer& buffer, std::size_t byteOffset, PrepareRenameCallback callback,
                                      const std::string& serverKey) {
    BufferSyncState* state = ResolveSyncState(buffer, serverKey);
    if (!state || !state->opened) {
        callback(std::nullopt);
        return;
    }
    Client* client = ExistingClientForLanguage(state->connectionKey);
    if (!client) {
        callback(std::nullopt);
        return;
    }

    const std::string language = state->connectionKey;
    const Position position = BytePositionToLsp(buffer.Content(), byteOffset);
    const Json        params   = {
        {"textDocument", {{"uri", state->uri}}},
        {"position", {{"line", position.line}, {"character", position.character}}},
    };
    client->SendRequest("textDocument/prepareRename", params,
                        [this, language, callback = std::move(callback)](std::optional<Json> result, std::optional<Json> error) {
                            if (error) {
                                LogError(language, ExtractErrorMessage(*error));
                                callback(std::nullopt);
                                return;
                            }
                            if (!result) {
                                callback(std::nullopt);
                                return;
                            }
                            callback(ExtractPrepareRenameResult(*result));
                        });
}

void Manager::RequestRename(text::Buffer& buffer, std::size_t byteOffset, const std::string& newName, RenameCallback callback,
                               const std::string& serverKey) {
    BufferSyncState* state = ResolveSyncState(buffer, serverKey);
    if (!state || !state->opened) {
        callback(std::nullopt);
        return;
    }
    Client* client = ExistingClientForLanguage(state->connectionKey);
    if (!client) {
        callback(std::nullopt);
        return;
    }

    const std::string language = state->connectionKey;
    const Position position = BytePositionToLsp(buffer.Content(), byteOffset);
    const Json        params   = {
        {"textDocument", {{"uri", state->uri}}},
        {"position", {{"line", position.line}, {"character", position.character}}},
        {"newName", newName},
    };
    client->SendRequest("textDocument/rename", params,
                        [this, language, callback = std::move(callback)](std::optional<Json> result, std::optional<Json> error) {
                            if (error) {
                                LogError(language, ExtractErrorMessage(*error));
                                callback(std::nullopt);
                                return;
                            }
                            if (!result) {
                                callback(std::nullopt);
                                return;
                            }
                            const RenameResult parsed = ExtractRenameEdits(*result);
                            ResolvedRename     resolved;
                            resolved.touchesUnsupportedForm = parsed.touchesUnsupportedForm;
                            for (const RenameEdit& edit : parsed.edits) {
                                const std::optional<std::filesystem::path> path = UriToPath(edit.uri);
                                if (!path) {
                                    // See ResolvedRenameEdit's own doc comment: an unresolvable
                                    // uri means the whole result can't be safely applied, not
                                    // just this one file's edits.
                                    callback(std::nullopt);
                                    return;
                                }
                                resolved.edits.push_back(ResolvedRenameEdit{.path = *path, .edits = edit.edits});
                            }
                            if (!parsed.documentChangeOps.empty()) {
                                const std::optional<std::vector<ResolvedDocumentChangeOp>> resolvedOps =
                                    ResolveDocumentChangeOps(parsed.documentChangeOps);
                                if (!resolvedOps) {
                                    callback(std::nullopt); // see ResolvedDocumentChangeOp's own doc comment -- same refuse-wholesale contract
                                    return;
                                }
                                resolved.documentChangeOps = std::move(*resolvedOps);
                            }
                            resolved.hasEdit = !resolved.edits.empty() || !resolved.documentChangeOps.empty();
                            callback(std::move(resolved));
                        });
}

void Manager::RequestWillRenameFiles(const std::vector<FileRenameEntry>& files, RenameCallback callback) {
    std::vector<std::pair<std::string, Client*>> targets;
    for (const auto& [serverKey, client] : clients_) {
        const auto capsIt = fileOperationFilters_.find(serverKey);
        if (capsIt == fileOperationFilters_.end() || capsIt->second.willRenameGlobs.empty()) {
            continue;
        }
        const bool matchesAny = std::any_of(files.begin(), files.end(), [&](const FileRenameEntry& entry) {
            return MatchesFileOperationGlob(entry.oldPath, capsIt->second.willRenameGlobs);
        });
        if (matchesAny) {
            targets.emplace_back(serverKey, client.get());
        }
    }
    if (targets.empty()) {
        callback(std::nullopt);
        return;
    }

    Json fileList = Json::array();
    for (const FileRenameEntry& entry : files) {
        fileList.push_back({{"oldUri", PathToUri(entry.oldPath)}, {"newUri", PathToUri(entry.newPath)}});
    }
    const Json params = {{"files", fileList}};

    // Every matching server's response is resolved/merged into one shared
    // ResolvedRename as it arrives; callback fires only once the last one
    // in flight has answered (order-independent, since outstanding is a
    // plain countdown). A response that fails to resolve (an unresolvable
    // uri, an unsupported edit form) is dropped silently rather than
    // failing every other server's already-good response -- unlike
    // RequestRename's single-server refuse-wholesale contract, this is
    // fundamentally a fan-out across independent servers, so one server's
    // bad answer shouldn't cost every other server's good one.
    struct PendingWillRename {
        int            outstanding = 0;
        ResolvedRename merged;
        bool           anyEdit = false;
    };
    auto pending         = std::make_shared<PendingWillRename>();
    pending->outstanding = static_cast<int>(targets.size());
    for (auto& [serverKey, client] : targets) {
        client->SendRequest(
            "workspace/willRenameFiles", params,
            [this, serverKey, pending, callback](std::optional<Json> result, std::optional<Json> error) {
                if (error) {
                    LogError(serverKey, ExtractErrorMessage(*error));
                }
                else if (result && !result->is_null()) {
                    const RenameResult parsed = ExtractRenameEdits(*result);
                    if (!parsed.touchesUnsupportedForm) {
                        std::vector<ResolvedRenameEdit> edits;
                        bool                            ok = true;
                        for (const RenameEdit& edit : parsed.edits) {
                            const std::optional<std::filesystem::path> path = UriToPath(edit.uri);
                            if (!path) {
                                ok = false;
                                break;
                            }
                            edits.push_back(ResolvedRenameEdit{.path = *path, .edits = edit.edits});
                        }
                        std::vector<ResolvedDocumentChangeOp> ops;
                        if (ok && !parsed.documentChangeOps.empty()) {
                            const std::optional<std::vector<ResolvedDocumentChangeOp>> resolvedOps =
                                ResolveDocumentChangeOps(parsed.documentChangeOps);
                            if (!resolvedOps) {
                                ok = false;
                            }
                            else {
                                ops = std::move(*resolvedOps);
                            }
                        }
                        if (ok && (!edits.empty() || !ops.empty())) {
                            pending->merged.edits.insert(pending->merged.edits.end(), std::make_move_iterator(edits.begin()),
                                                         std::make_move_iterator(edits.end()));
                            pending->merged.documentChangeOps.insert(pending->merged.documentChangeOps.end(),
                                                                     std::make_move_iterator(ops.begin()), std::make_move_iterator(ops.end()));
                            pending->anyEdit = true;
                        }
                    }
                }
                if (--pending->outstanding == 0) {
                    if (!pending->anyEdit) {
                        callback(std::nullopt);
                        return;
                    }
                    pending->merged.hasEdit = true;
                    callback(std::move(pending->merged));
                }
            });
    }
}

void Manager::NotifyFilesRenamed(const std::vector<FileRenameEntry>& files) {
    Json fileList = Json::array();
    for (const FileRenameEntry& entry : files) {
        fileList.push_back({{"oldUri", PathToUri(entry.oldPath)}, {"newUri", PathToUri(entry.newPath)}});
    }
    const Json params = {{"files", fileList}};
    for (const auto& [serverKey, client] : clients_) {
        const auto capsIt = fileOperationFilters_.find(serverKey);
        if (capsIt == fileOperationFilters_.end() || capsIt->second.didRenameGlobs.empty()) {
            continue;
        }
        const bool matchesAny = std::any_of(files.begin(), files.end(), [&](const FileRenameEntry& entry) {
            return MatchesFileOperationGlob(entry.newPath, capsIt->second.didRenameGlobs);
        });
        if (matchesAny) {
            client->SendNotification("workspace/didRenameFiles", params);
        }
    }
}

std::optional<std::vector<Manager::ResolvedRenameEdit>> Manager::ResolveCodeActionEdits(const CodeAction& action) {
    if (action.touchesUnsupportedForm || !action.hasEdit) {
        return std::nullopt;
    }
    std::vector<ResolvedRenameEdit> resolved;
    resolved.reserve(action.edits.size());
    for (const RenameEdit& edit : action.edits) {
        const std::optional<std::filesystem::path> path = UriToPath(edit.uri);
        if (!path) {
            return std::nullopt; // see ResolvedRenameEdit's own doc comment -- refused wholesale, not partially
        }
        resolved.push_back(ResolvedRenameEdit{.path = *path, .edits = edit.edits});
    }
    return resolved;
}

std::optional<std::vector<Manager::ResolvedDocumentChangeOp>>
Manager::ResolveDocumentChangeOps(const std::vector<DocumentChangeOp>& ops) {
    std::vector<ResolvedDocumentChangeOp> resolved;
    resolved.reserve(ops.size());
    for (const DocumentChangeOp& op : ops) {
        const std::optional<std::filesystem::path> path = UriToPath(op.uri);
        if (!path) {
            return std::nullopt; // refused wholesale -- see ResolvedDocumentChangeOp's own doc comment
        }
        ResolvedDocumentChangeOp resolvedOp{
            .kind              = op.kind,
            .path              = *path,
            .edits             = op.edits,
            .overwrite         = op.overwrite,
            .ignoreIfExists    = op.ignoreIfExists,
            .ignoreIfNotExists = op.ignoreIfNotExists,
        };
        if (op.kind == DocumentChangeOp::Kind::RenameFile) {
            const std::optional<std::filesystem::path> oldPath = UriToPath(op.oldUri);
            if (!oldPath) {
                return std::nullopt;
            }
            resolvedOp.oldPath = *oldPath;
        }
        resolved.push_back(std::move(resolvedOp));
    }
    return resolved;
}

namespace {
    // formatting follow-up: fixed per plan decision -- this codebase has no
    // per-buffer tabs-vs-spaces concept yet, so insertSpaces is hardcoded
    // true; tabSize mirrors the display-only TabWidth() setting (advisory
    // only anyway -- a server commonly falls back to its own config file,
    // e.g. .clang-format/rustfmt.toml, when present).
    Json FormattingOptionsJson() {
        return Json{{"tabSize", editor::TabWidth()}, {"insertSpaces", true}};
    }
} // namespace

void Manager::RequestFormatting(text::Buffer& buffer, FormattingCallback callback, const std::string& serverKey) {
    BufferSyncState* state = ResolveSyncState(buffer, serverKey);
    if (!state || !state->opened) {
        callback(std::nullopt);
        return;
    }
    Client* client = ExistingClientForLanguage(state->connectionKey);
    if (!client) {
        callback(std::nullopt);
        return;
    }

    const std::string language = state->connectionKey;
    const Json        params   = {
        {"textDocument", {{"uri", state->uri}}},
        {"options", FormattingOptionsJson()},
    };
    client->SendRequest("textDocument/formatting", params,
                        [this, language, callback = std::move(callback)](std::optional<Json> result, std::optional<Json> error) {
                            if (error) {
                                LogError(language, ExtractErrorMessage(*error));
                                callback(std::nullopt);
                                return;
                            }
                            if (!result) {
                                callback(std::nullopt);
                                return;
                            }
                            callback(ExtractFormattingEdits(*result));
                        });
}

void Manager::RequestRangeFormatting(text::Buffer& buffer, std::size_t rangeStartByte, std::size_t rangeEndByte,
                                        FormattingCallback callback, const std::string& serverKey) {
    BufferSyncState* state = ResolveSyncState(buffer, serverKey);
    if (!state || !state->opened) {
        callback(std::nullopt);
        return;
    }
    Client* client = ExistingClientForLanguage(state->connectionKey);
    if (!client) {
        callback(std::nullopt);
        return;
    }

    const text::ITextStorage& content  = buffer.Content();
    const Position         start    = BytePositionToLsp(content, rangeStartByte);
    const Position         end      = BytePositionToLsp(content, rangeEndByte);
    const std::string         language = state->connectionKey;
    const Json                params   = {
        {"textDocument", {{"uri", state->uri}}},
        {"range", {{"start", {{"line", start.line}, {"character", start.character}}}, {"end", {{"line", end.line}, {"character", end.character}}}}},
        {"options", FormattingOptionsJson()},
    };
    client->SendRequest("textDocument/rangeFormatting", params,
                        [this, language, callback = std::move(callback)](std::optional<Json> result, std::optional<Json> error) {
                            if (error) {
                                LogError(language, ExtractErrorMessage(*error));
                                callback(std::nullopt);
                                return;
                            }
                            if (!result) {
                                callback(std::nullopt);
                                return;
                            }
                            callback(ExtractFormattingEdits(*result));
                        });
}

void Manager::RequestOnTypeFormatting(text::Buffer& buffer, std::size_t byteOffset, const std::string& ch, FormattingCallback callback,
                                         const std::string& serverKey) {
    BufferSyncState* state = ResolveSyncState(buffer, serverKey);
    if (!state || !state->opened) {
        callback(std::nullopt);
        return;
    }
    Client* client = ExistingClientForLanguage(state->connectionKey);
    if (!client) {
        callback(std::nullopt);
        return;
    }

    const std::string language = state->connectionKey;
    const Position position = BytePositionToLsp(buffer.Content(), byteOffset);
    const Json        params   = {
        {"textDocument", {{"uri", state->uri}}},
        {"position", {{"line", position.line}, {"character", position.character}}},
        {"ch", ch},
        {"options", FormattingOptionsJson()},
    };
    client->SendRequest("textDocument/onTypeFormatting", params,
                        [this, language, callback = std::move(callback)](std::optional<Json> result, std::optional<Json> error) {
                            if (error) {
                                LogError(language, ExtractErrorMessage(*error));
                                callback(std::nullopt);
                                return;
                            }
                            if (!result) {
                                callback(std::nullopt);
                                return;
                            }
                            callback(ExtractFormattingEdits(*result));
                        });
}

void Manager::RequestDocumentSymbols(text::Buffer& buffer, SymbolCallback callback, const std::string& serverKey) {
    BufferSyncState* state = ResolveSyncState(buffer, serverKey);
    if (!state || !state->opened) {
        callback({});
        return;
    }
    Client* client = ExistingClientForLanguage(state->connectionKey);
    if (!client) {
        callback({});
        return;
    }

    const std::string language = state->connectionKey;
    const std::string uri      = state->uri;
    const Json        params   = {{"textDocument", {{"uri", uri}}}};
    client->SendRequest("textDocument/documentSymbol", params,
                        [this, language, uri, callback = std::move(callback)](std::optional<Json> result, std::optional<Json> error) {
                            if (error) {
                                LogError(language, ExtractErrorMessage(*error));
                                callback({});
                                return;
                            }
                            if (!result) {
                                callback({});
                                return;
                            }
                            std::vector<SymbolResult> resolved;
                            for (const SymbolEntry& entry : ExtractSymbols(*result, uri)) {
                                if (const std::optional<std::filesystem::path> path = UriToPath(entry.uri)) {
                                    resolved.push_back(SymbolResult{.name          = entry.name,
                                                                    .containerName = entry.containerName,
                                                                    .kind          = entry.kind,
                                                                    .path          = *path,
                                                                    .position      = entry.position});
                                }
                            }
                            callback(std::move(resolved));
                        });
}

void Manager::RequestWorkspaceSymbols(text::Buffer& buffer, const std::string& query, SymbolCallback callback,
                                         const std::string& serverKey) {
    BufferSyncState* state = ResolveSyncState(buffer, serverKey);
    if (!state || !state->opened) {
        callback({});
        return;
    }
    Client* client = ExistingClientForLanguage(state->connectionKey);
    if (!client) {
        callback({});
        return;
    }

    const std::string language = state->connectionKey;
    const Json        params   = {{"query", query}};
    client->SendRequest("workspace/symbol", params,
                        [this, language, callback = std::move(callback)](std::optional<Json> result, std::optional<Json> error) {
                            if (error) {
                                LogError(language, ExtractErrorMessage(*error));
                                callback({});
                                return;
                            }
                            if (!result) {
                                callback({});
                                return;
                            }
                            std::vector<SymbolResult> resolved;
                            for (const SymbolEntry& entry : ExtractSymbols(*result)) {
                                if (const std::optional<std::filesystem::path> path = UriToPath(entry.uri)) {
                                    resolved.push_back(SymbolResult{.name          = entry.name,
                                                                    .containerName = entry.containerName,
                                                                    .kind          = entry.kind,
                                                                    .path          = *path,
                                                                    .position      = entry.position});
                                }
                            }
                            callback(std::move(resolved));
                        });
}

namespace {

    // call/type-hierarchy follow-up: shared by RequestPrepareCallHierarchy/
    // RequestPrepareTypeHierarchy's own resolve-uri-to-path step and
    // RequestIncomingCalls/RequestOutgoingCalls/RequestSupertypes/
    // RequestSubtypes' own -- SymbolResult's own "drop, don't keep with a
    // nonsense path" convention.
    std::vector<Manager::ResolvedHierarchyItem> ResolveHierarchyItems(std::vector<HierarchyItem> items) {
        std::vector<Manager::ResolvedHierarchyItem> resolved;
        resolved.reserve(items.size());
        for (HierarchyItem& item : items) {
            if (const std::optional<std::filesystem::path> path = UriToPath(item.uri)) {
                resolved.push_back(Manager::ResolvedHierarchyItem{.item = std::move(item), .path = *path});
            }
        }
        return resolved;
    }

} // namespace

void Manager::SendHierarchyPrepareRequest(const std::string& method, text::Buffer& buffer, std::size_t byteOffset,
                                             HierarchyItemsCallback callback, const std::string& serverKey) {
    BufferSyncState* state = ResolveSyncState(buffer, serverKey);
    if (!state || !state->opened) {
        callback({});
        return;
    }
    Client* client = ExistingClientForLanguage(state->connectionKey);
    if (!client) {
        callback({});
        return;
    }

    const std::string language = state->connectionKey;
    const Position position = BytePositionToLsp(buffer.Content(), byteOffset);
    const Json        params   = {
        {"textDocument", {{"uri", state->uri}}},
        {"position", {{"line", position.line}, {"character", position.character}}},
    };
    client->SendRequest(method, params,
                        [this, language, callback = std::move(callback)](std::optional<Json> result, std::optional<Json> error) {
                            if (error) {
                                LogError(language, ExtractErrorMessage(*error));
                                callback({});
                                return;
                            }
                            if (!result) {
                                callback({});
                                return;
                            }
                            callback(ResolveHierarchyItems(ExtractHierarchyItems(*result)));
                        });
}

void Manager::RequestPrepareCallHierarchy(text::Buffer& buffer, std::size_t byteOffset, HierarchyItemsCallback callback,
                                             const std::string& serverKey) {
    SendHierarchyPrepareRequest("textDocument/prepareCallHierarchy", buffer, byteOffset, std::move(callback), serverKey);
}

void Manager::RequestPrepareTypeHierarchy(text::Buffer& buffer, std::size_t byteOffset, HierarchyItemsCallback callback,
                                             const std::string& serverKey) {
    SendHierarchyPrepareRequest("textDocument/prepareTypeHierarchy", buffer, byteOffset, std::move(callback), serverKey);
}

void Manager::RequestIncomingCalls(text::Buffer& buffer, const HierarchyItem& item, HierarchyCallsCallback callback,
                                      const std::string& serverKey) {
    BufferSyncState* state = ResolveSyncState(buffer, serverKey);
    if (!state || !state->opened) {
        callback({});
        return;
    }
    Client* client = ExistingClientForLanguage(state->connectionKey);
    if (!client) {
        callback({});
        return;
    }

    const std::string language = state->connectionKey;
    const Json        params   = {{"item", item.raw}};
    client->SendRequest("callHierarchy/incomingCalls", params,
                        [this, language, callback = std::move(callback)](std::optional<Json> result, std::optional<Json> error) {
                            if (error) {
                                LogError(language, ExtractErrorMessage(*error));
                                callback({});
                                return;
                            }
                            if (!result) {
                                callback({});
                                return;
                            }
                            std::vector<ResolvedHierarchyCall> resolved;
                            for (HierarchyCall& call : ExtractIncomingCalls(*result)) {
                                if (const std::optional<std::filesystem::path> path = UriToPath(call.item.uri)) {
                                    resolved.push_back(ResolvedHierarchyCall{
                                        .item      = ResolvedHierarchyItem{.item = std::move(call.item), .path = *path},
                                        .callSites = std::move(call.callSites)});
                                }
                            }
                            callback(std::move(resolved));
                        });
}

void Manager::RequestOutgoingCalls(text::Buffer& buffer, const HierarchyItem& item, HierarchyCallsCallback callback,
                                      const std::string& serverKey) {
    BufferSyncState* state = ResolveSyncState(buffer, serverKey);
    if (!state || !state->opened) {
        callback({});
        return;
    }
    Client* client = ExistingClientForLanguage(state->connectionKey);
    if (!client) {
        callback({});
        return;
    }

    const std::string language = state->connectionKey;
    const Json        params   = {{"item", item.raw}};
    client->SendRequest("callHierarchy/outgoingCalls", params,
                        [this, language, callback = std::move(callback)](std::optional<Json> result, std::optional<Json> error) {
                            if (error) {
                                LogError(language, ExtractErrorMessage(*error));
                                callback({});
                                return;
                            }
                            if (!result) {
                                callback({});
                                return;
                            }
                            std::vector<ResolvedHierarchyCall> resolved;
                            for (HierarchyCall& call : ExtractOutgoingCalls(*result)) {
                                if (const std::optional<std::filesystem::path> path = UriToPath(call.item.uri)) {
                                    resolved.push_back(ResolvedHierarchyCall{
                                        .item      = ResolvedHierarchyItem{.item = std::move(call.item), .path = *path},
                                        .callSites = std::move(call.callSites)});
                                }
                            }
                            callback(std::move(resolved));
                        });
}

void Manager::SendTypeHierarchyStepRequest(const std::string& method, text::Buffer& buffer, const HierarchyItem& item,
                                              HierarchyItemsCallback callback, const std::string& serverKey) {
    BufferSyncState* state = ResolveSyncState(buffer, serverKey);
    if (!state || !state->opened) {
        callback({});
        return;
    }
    Client* client = ExistingClientForLanguage(state->connectionKey);
    if (!client) {
        callback({});
        return;
    }

    const std::string language = state->connectionKey;
    const Json        params   = {{"item", item.raw}};
    client->SendRequest(method, params,
                        [this, language, callback = std::move(callback)](std::optional<Json> result, std::optional<Json> error) {
                            if (error) {
                                LogError(language, ExtractErrorMessage(*error));
                                callback({});
                                return;
                            }
                            if (!result) {
                                callback({});
                                return;
                            }
                            callback(ResolveHierarchyItems(ExtractHierarchyItems(*result)));
                        });
}

void Manager::RequestSupertypes(text::Buffer& buffer, const HierarchyItem& item, HierarchyItemsCallback callback,
                                   const std::string& serverKey) {
    SendTypeHierarchyStepRequest("typeHierarchy/supertypes", buffer, item, std::move(callback), serverKey);
}

void Manager::RequestSubtypes(text::Buffer& buffer, const HierarchyItem& item, HierarchyItemsCallback callback,
                                 const std::string& serverKey) {
    SendTypeHierarchyStepRequest("typeHierarchy/subtypes", buffer, item, std::move(callback), serverKey);
}

void Manager::Shutdown() {
    for (const auto& [language, client] : clients_) {
        if (brokerBackedLanguages_.contains(language)) {
            continue; // broker-owned -- must outlive this process, see brokerBackedLanguages_'s own doc comment
        }
        // Mirrors Broker::Shutdown()'s own TearDownEntry pattern exactly
        // -- fire both frames, don't wait for the shutdown response (no
        // live EventLoop::Run() left to wait with; see this method's own
        // doc comment in Manager.h). The callback is never expected to
        // run; passed only because SendRequest requires one.
        //
        // async-write-queue follow-up: PrepareForGracefulShutdown must be
        // called before these two sends, since the actual write is now
        // async (queued, not synchronous) -- it's what makes ~Client()
        // (run moments from now, when clients_ itself is destroyed as part
        // of this process's normal teardown) drain the queue instead of
        // applying its ordinary best-effort/no-drain policy.
        client->PrepareForGracefulShutdown();
        client->SendRequest("shutdown", Json::object(), [](std::optional<Json>, std::optional<Json>) {});
        client->SendNotification("exit", Json::object());
    }
}

} // namespace ned::editor::lsp
