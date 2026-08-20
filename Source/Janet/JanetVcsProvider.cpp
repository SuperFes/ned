#include "JanetVcsProvider.h"

#include "Value.h"

namespace ned::janet {

namespace {

    // Reads keyword key from entry (a struct {} or table @{}, either is a
    // valid Janet literal for a plugin author to write -- janet_get handles
    // both polymorphically), marshaled as a string; "" if the key is absent
    // or not a string -- a plugin's parse function is user/bundled Janet, a
    // malformed entry shouldn't crash the editor, just degrade to an empty
    // field.
    std::string StringField(Janet entry, const char* key) {
        const Janet value = janet_get(entry, janet_ckeywordv(key));
        if (!janet_checktype(value, JANET_STRING)) {
            return {};
        }
        return FromJanet<std::string>(value);
    }

    template <typename Entry>
    std::vector<Entry> ParseEntries(Janet result) {
        const Janet* items = nullptr;
        std::int32_t count = 0;
        if (!janet_indexed_view(result, &items, &count)) {
            throw std::runtime_error("ned: expected a vcs plugin parse function to return an array of tables");
        }
        std::vector<Entry> entries;
        entries.reserve(static_cast<std::size_t>(count));
        for (std::int32_t i = 0; i < count; ++i) {
            if (!janet_checktype(items[i], JANET_TABLE) && !janet_checktype(items[i], JANET_STRUCT)) {
                continue; // malformed entry -- skip rather than throw, same "degrade, don't crash" convention
            }
            entries.push_back(Entry{
                StringField(items[i], "hash"),
                StringField(items[i], "author"),
                StringField(items[i], "date"),
                StringField(items[i], "summary"),
            });
        }
        return entries;
    }

    editor::vcs::VcsCommandSpec ParseCommandSpec(Janet result) {
        return editor::vcs::VcsCommandSpec{FromJanet<std::vector<std::string>>(result)};
    }

    // Same "degrade, don't crash" convention as StringField -- 0 if the key
    // is absent or not a number (Janet numbers are always doubles; a
    // negative or fractional value from a malformed plugin still converts
    // rather than throwing, since a diff hunk's own downstream consumer
    // (BufferView's gutter) only ever compares/adds these, never trusts
    // them enough to index unchecked).
    std::size_t NumberField(Janet entry, const char* key) {
        const Janet value = janet_get(entry, janet_ckeywordv(key));
        if (!janet_checktype(value, JANET_NUMBER)) {
            return 0;
        }
        const double number = janet_unwrap_number(value);
        return number > 0 ? static_cast<std::size_t>(number) : 0;
    }

    std::vector<editor::vcs::VcsDiffHunk> ParseDiffHunks(Janet result) {
        const Janet* items = nullptr;
        std::int32_t count = 0;
        if (!janet_indexed_view(result, &items, &count)) {
            throw std::runtime_error("ned: expected a vcs plugin parse-diff function to return an array of tables");
        }
        std::vector<editor::vcs::VcsDiffHunk> hunks;
        hunks.reserve(static_cast<std::size_t>(count));
        for (std::int32_t i = 0; i < count; ++i) {
            if (!janet_checktype(items[i], JANET_TABLE) && !janet_checktype(items[i], JANET_STRUCT)) {
                continue;
            }
            hunks.push_back(editor::vcs::VcsDiffHunk{
                NumberField(items[i], "old-start"),
                NumberField(items[i], "old-count"),
                NumberField(items[i], "new-start"),
                NumberField(items[i], "new-count"),
            });
        }
        return hunks;
    }

} // namespace

JanetVcsProvider::JanetVcsProvider(JanetTable* env, std::string name, Janet detectFn, Janet blameArgvFn,
                                    Janet parseBlameFn, Janet logArgvFn, Janet parseLogFn, Janet diffArgvFn,
                                    Janet parseDiffFn)
    : env_(env), name_(std::move(name)), detectName_("ned/vcs-" + name_ + "-detect"),
      blameArgvName_("ned/vcs-" + name_ + "-blame-argv"), parseBlameName_("ned/vcs-" + name_ + "-parse-blame"),
      logArgvName_("ned/vcs-" + name_ + "-log-argv"), parseLogName_("ned/vcs-" + name_ + "-parse-log"),
      diffArgvName_("ned/vcs-" + name_ + "-diff-argv"), parseDiffName_("ned/vcs-" + name_ + "-parse-diff") {
    janet_def(env_, detectName_.c_str(), detectFn, "");
    janet_def(env_, blameArgvName_.c_str(), blameArgvFn, "");
    janet_def(env_, parseBlameName_.c_str(), parseBlameFn, "");
    janet_def(env_, logArgvName_.c_str(), logArgvFn, "");
    janet_def(env_, parseLogName_.c_str(), parseLogFn, "");
    janet_def(env_, diffArgvName_.c_str(), diffArgvFn, "");
    janet_def(env_, parseDiffName_.c_str(), parseDiffFn, "");
}

Janet JanetVcsProvider::CallWithString(const std::string& fnInternalName, const std::string& arg) const {
    const std::string argName = "ned/vcs-call-arg";
    const Janet       argValue =
        janet_wrap_string(janet_string(reinterpret_cast<const std::uint8_t*>(arg.data()), static_cast<std::int32_t>(arg.size())));
    janet_def(env_, argName.c_str(), argValue, "");

    const std::string invokeExpr = "(" + fnInternalName + " " + argName + ")";
    Janet             out;
    const int         signal = janet_dostring(env_, invokeExpr.c_str(), "ned-vcs", &out);
    if (signal != 0) {
        throw std::runtime_error("ned: error running vcs plugin callback '" + fnInternalName + "' (see stderr for details)");
    }
    return out;
}

bool JanetVcsProvider::Detect(const std::filesystem::path& root) const {
    return FromJanet<bool>(CallWithString(detectName_, root.string()));
}

editor::vcs::VcsCommandSpec JanetVcsProvider::BlameArgv(const std::filesystem::path& path) const {
    return ParseCommandSpec(CallWithString(blameArgvName_, path.string()));
}

std::vector<editor::vcs::VcsBlameLine> JanetVcsProvider::ParseBlame(const std::string& stdout_) const {
    return ParseEntries<editor::vcs::VcsBlameLine>(CallWithString(parseBlameName_, stdout_));
}

editor::vcs::VcsCommandSpec JanetVcsProvider::LogArgv(const std::filesystem::path& path) const {
    return ParseCommandSpec(CallWithString(logArgvName_, path.string()));
}

std::vector<editor::vcs::VcsLogEntry> JanetVcsProvider::ParseLog(const std::string& stdout_) const {
    return ParseEntries<editor::vcs::VcsLogEntry>(CallWithString(parseLogName_, stdout_));
}

editor::vcs::VcsCommandSpec JanetVcsProvider::DiffArgv(const std::filesystem::path& path) const {
    return ParseCommandSpec(CallWithString(diffArgvName_, path.string()));
}

std::vector<editor::vcs::VcsDiffHunk> JanetVcsProvider::ParseDiff(const std::string& stdout_) const {
    return ParseDiffHunks(CallWithString(parseDiffName_, stdout_));
}

} // namespace ned::janet
