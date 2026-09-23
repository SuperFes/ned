#include <catch2/catch_test_macros.hpp>

#include "Editor/CompletionSources.h"
#include "Editor/SnippetRegistry.h"

using ned::editor::BufferWordCompletions;
using ned::editor::ClearAllSnippets;
using ned::editor::Completion;
using ned::editor::CompletionSource;
using ned::editor::JanetBindingCompletions;
using ned::editor::MergeCompletions;
using ned::editor::RegisterSnippet;
using ned::editor::SnippetCompletions;

namespace {

// The snippet registry is process-wide static state -- SnippetRegistryTest's
// own guard, for the same reason.
struct SnippetRegistryGuard {
    SnippetRegistryGuard() {
        ClearAllSnippets();
    }
    ~SnippetRegistryGuard() {
        ClearAllSnippets();
    }
};

std::vector<std::string> Labels(const std::vector<Completion>& completions) {
    std::vector<std::string> labels;
    labels.reserve(completions.size());
    for (const Completion& completion : completions) {
        labels.push_back(completion.label);
    }
    return labels;
}

Completion Of(CompletionSource source, std::string label) {
    return Completion{.label = label, .filterText = label, .sortText = label, .insertText = std::move(label), .source = source};
}

} // namespace

TEST_CASE("BufferWordCompletions tags its candidates and keeps proximity order", "[CompletionSources]") {
    const std::string             content     = "counter countdown\ncou countless";
    const std::size_t             point       = 18; // start of the typed "cou"
    const std::vector<Completion> completions = BufferWordCompletions(content, point, "cou");

    REQUIRE(Labels(completions) == std::vector<std::string>{"countdown", "counter", "countless"});
    for (const Completion& completion : completions) {
        CHECK(completion.source == CompletionSource::BufferWord);
        CHECK(completion.insertText == completion.label);
        CHECK(!completion.isSnippet);
        CHECK(completion.raw.is_null());
    }
}

TEST_CASE("BufferWordCompletions requires a prefix", "[CompletionSources]") {
    CHECK(BufferWordCompletions("counter countdown", 0, "").empty());
}

TEST_CASE("BufferWordCompletions honors the caller's widened word rule", "[CompletionSources]") {
    // JanetSymbolPrefixStart reads "ned/register-command" as one token; the
    // default alnum/underscore rule would chop it into three words, none of
    // which the typed prefix could ever match.
    const std::string content = "(ned/register-command :x)\n(ned/reg";
    const std::size_t point   = content.size();

    CHECK(BufferWordCompletions(content, point, "ned/reg").empty());
    CHECK(Labels(BufferWordCompletions(content, point, "ned/reg", "-/")) ==
          std::vector<std::string>{"ned/register-command"});
}

TEST_CASE("SnippetCompletions carries the body as an expandable insertText", "[CompletionSources]") {
    const SnippetRegistryGuard guard;
    RegisterSnippet("cpp", "for", "for (int ${1:i} = 0;) {\n\t$0\n}");
    RegisterSnippet("cpp", "while", "while (${1:cond}) {\n\t$0\n}");

    const std::vector<Completion> completions = SnippetCompletions("cpp", "fo");
    REQUIRE(completions.size() == 1);
    CHECK(completions[0].label == "for");
    CHECK(completions[0].insertText == "for (int ${1:i} = 0;) {\n\t$0\n}");
    CHECK(completions[0].isSnippet);
    CHECK(completions[0].source == CompletionSource::Snippet);
}

TEST_CASE("SnippetCompletions keeps an exactly-typed trigger", "[CompletionSources]") {
    // The opposite of the buffer-word rule: "for" typed in full still has a
    // whole loop body to offer.
    const SnippetRegistryGuard guard;
    RegisterSnippet("cpp", "for", "for (;;) {}");

    REQUIRE(Labels(SnippetCompletions("cpp", "for")) == std::vector<std::string>{"for"});
}

TEST_CASE("SnippetCompletions sees the global tier and requires a prefix", "[CompletionSources]") {
    const SnippetRegistryGuard guard;
    RegisterSnippet("", "todo", "TODO($1): $0");
    RegisterSnippet("cpp", "for", "for (;;) {}");

    CHECK(Labels(SnippetCompletions("cpp", "to")) == std::vector<std::string>{"todo"});
    CHECK(SnippetCompletions("cpp", "").empty());
}

TEST_CASE("JanetBindingCompletions drops an exactly-typed name", "[CompletionSources]") {
    const std::vector<std::string> names = {"ned/set-keymap-style", "ned/set-theme"};

    CHECK(Labels(JanetBindingCompletions(names, "ned/set-")).size() == 2);
    CHECK(JanetBindingCompletions(names, "ned/set-theme").empty());
    CHECK(JanetBindingCompletions(names, "").empty());
}

TEST_CASE("MergeCompletions orders by source rank regardless of argument order", "[CompletionSources]") {
    std::vector<Completion> words    = {Of(CompletionSource::BufferWord, "word")};
    std::vector<Completion> server   = {Of(CompletionSource::Lsp, "server")};
    std::vector<Completion> snippets = {Of(CompletionSource::Snippet, "snip")};
    std::vector<Completion> bindings = {Of(CompletionSource::JanetBinding, "ned/bind")};

    const std::vector<Completion> merged = MergeCompletions({words, bindings, server, snippets});
    CHECK(Labels(merged) == std::vector<std::string>{"snip", "server", "ned/bind", "word"});
}

TEST_CASE("MergeCompletions suppresses a lower-ranked source's duplicate label", "[CompletionSources]") {
    std::vector<Completion> server = {Of(CompletionSource::Lsp, "push_back"), Of(CompletionSource::Lsp, "pop_back")};
    std::vector<Completion> words  = {Of(CompletionSource::BufferWord, "push_back"),
                                      Of(CompletionSource::BufferWord, "push_front")};

    const std::vector<Completion> merged = MergeCompletions({words, server});
    REQUIRE(Labels(merged) == std::vector<std::string>{"push_back", "pop_back", "push_front"});
    CHECK(merged[0].source == CompletionSource::Lsp);
}

TEST_CASE("MergeCompletions keeps duplicates within one source", "[CompletionSources]") {
    // Two "push_back" items from one server are two real overloads; dropping
    // either would hide a signature.
    std::vector<Completion> overloads = {Of(CompletionSource::Lsp, "push_back"), Of(CompletionSource::Lsp, "push_back")};

    CHECK(MergeCompletions({overloads}).size() == 2);
}

TEST_CASE("MergeCompletions preserves each source's own order", "[CompletionSources]") {
    std::vector<Completion> server = {Of(CompletionSource::Lsp, "zebra"), Of(CompletionSource::Lsp, "apple")};

    CHECK(Labels(MergeCompletions({server})) == std::vector<std::string>{"zebra", "apple"});
}

TEST_CASE("MergeCompletions tolerates empty lists", "[CompletionSources]") {
    std::vector<Completion> server = {Of(CompletionSource::Lsp, "server")};

    CHECK(Labels(MergeCompletions({{}, server, {}})) == std::vector<std::string>{"server"});
    CHECK(MergeCompletions({}).empty());
    CHECK(MergeCompletions({{}, {}}).empty());
}
