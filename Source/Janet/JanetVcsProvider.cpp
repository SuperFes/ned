#include "JanetVcsProvider.h"

#include "Environment.h"
#include "Value.h"

namespace ned::janet {

namespace {

    // Every callback key a plugin's table may carry -- the constructor
    // walks exactly this list, so an unknown key in the table is silently
    // ignored rather than an error (same "degrade, don't crash" posture as
    // the field readers below; a typoed key surfaces naturally as its
    // operation reporting "not supported").
    constexpr const char* kCallbackKeys[] = {
        "detect",
        "blame-argv",
        "parse-blame",
        "log-argv",
        "parse-log",
        "diff-argv",
        "parse-diff",
        "working-diff-argv",
        "commit-diff-argv",
        "status-argv",
        "parse-status",
        "stage-argv",
        "unstage-argv",
        "staged-diff-argv",
        "stage-patch-argv",
        "unstage-patch-argv",
        "revert-patch-argv",
        "commit-argv",
        "branch-list-argv",
        "parse-branch-list",
        "branch-switch-argv",
        "branch-create-argv",
        "revert-argv",
        "stash-list-argv",
        "parse-stash-list",
        "stash-push-argv",
        "stash-pop-argv",
        "stash-drop-argv",
        "push-argv",
        "pull-argv",
        "fetch-argv",
        "ahead-behind-argv",
        "parse-ahead-behind",
    };

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

    editor::vcs::CommandSpec ParseCommandSpec(Janet result) {
        return editor::vcs::CommandSpec{FromJanet<std::vector<std::string>>(result)};
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

    // false if the key is absent or not a boolean, same convention again.
    bool BoolField(Janet entry, const char* key) {
        const Janet value = janet_get(entry, janet_ckeywordv(key));
        return janet_checktype(value, JANET_BOOLEAN) && janet_unwrap_boolean(value);
    }

    std::vector<editor::vcs::DiffHunk> ParseDiffHunks(Janet result) {
        const Janet* items = nullptr;
        std::int32_t count = 0;
        if (!janet_indexed_view(result, &items, &count)) {
            throw std::runtime_error("ned: expected a vcs plugin parse-diff function to return an array of tables");
        }
        std::vector<editor::vcs::DiffHunk> hunks;
        hunks.reserve(static_cast<std::size_t>(count));
        for (std::int32_t i = 0; i < count; ++i) {
            if (!janet_checktype(items[i], JANET_TABLE) && !janet_checktype(items[i], JANET_STRUCT)) {
                continue;
            }
            hunks.push_back(editor::vcs::DiffHunk{
                NumberField(items[i], "old-start"),
                NumberField(items[i], "old-count"),
                NumberField(items[i], "new-start"),
                NumberField(items[i], "new-count"),
            });
        }
        return hunks;
    }

    std::vector<editor::vcs::StatusEntry> ParseStatusEntries(Janet result) {
        const Janet* items = nullptr;
        std::int32_t count = 0;
        if (!janet_indexed_view(result, &items, &count)) {
            throw std::runtime_error("ned: expected a vcs plugin parse-status function to return an array of tables");
        }
        std::vector<editor::vcs::StatusEntry> entries;
        entries.reserve(static_cast<std::size_t>(count));
        for (std::int32_t i = 0; i < count; ++i) {
            if (!janet_checktype(items[i], JANET_TABLE) && !janet_checktype(items[i], JANET_STRUCT)) {
                continue;
            }
            entries.push_back(editor::vcs::StatusEntry{
                StringField(items[i], "state"),
                StringField(items[i], "path"),
            });
        }
        return entries;
    }

    std::vector<editor::vcs::BranchEntry> ParseBranchEntries(Janet result) {
        const Janet* items = nullptr;
        std::int32_t count = 0;
        if (!janet_indexed_view(result, &items, &count)) {
            throw std::runtime_error("ned: expected a vcs plugin parse-branch-list function to return an array of tables");
        }
        std::vector<editor::vcs::BranchEntry> entries;
        entries.reserve(static_cast<std::size_t>(count));
        for (std::int32_t i = 0; i < count; ++i) {
            if (!janet_checktype(items[i], JANET_TABLE) && !janet_checktype(items[i], JANET_STRUCT)) {
                continue;
            }
            entries.push_back(editor::vcs::BranchEntry{
                StringField(items[i], "name"),
                BoolField(items[i], "current"),
            });
        }
        return entries;
    }

    std::vector<editor::vcs::StashEntry> ParseStashEntries(Janet result) {
        const Janet* items = nullptr;
        std::int32_t count = 0;
        if (!janet_indexed_view(result, &items, &count)) {
            throw std::runtime_error("ned: expected a vcs plugin parse-stash-list function to return an array of tables");
        }
        std::vector<editor::vcs::StashEntry> entries;
        entries.reserve(static_cast<std::size_t>(count));
        for (std::int32_t i = 0; i < count; ++i) {
            if (!janet_checktype(items[i], JANET_TABLE) && !janet_checktype(items[i], JANET_STRUCT)) {
                continue;
            }
            entries.push_back(editor::vcs::StashEntry{
                StringField(items[i], "ref"),
                StringField(items[i], "message"),
            });
        }
        return entries;
    }

    // Unlike every ParseX above (which return an array of entries), a
    // plugin's parse-ahead-behind returns one table directly -- there's
    // only ever one ahead/behind fact for the current branch.
    editor::vcs::AheadBehind ParseAheadBehindResult(Janet result) {
        if (!janet_checktype(result, JANET_TABLE) && !janet_checktype(result, JANET_STRUCT)) {
            throw std::runtime_error("ned: expected a vcs plugin parse-ahead-behind function to return a table");
        }
        return editor::vcs::AheadBehind{
            NumberField(result, "ahead"),
            NumberField(result, "behind"),
        };
    }

} // namespace

JanetVcsProvider::JanetVcsProvider(JanetTable* env, std::string name, Janet callbacks) : env_(env), name_(std::move(name)) {
    if (!janet_checktype(callbacks, JANET_TABLE) && !janet_checktype(callbacks, JANET_STRUCT)) {
        throw std::runtime_error("ned: vcs-register-provider expects a struct/table of callbacks keyed by keyword");
    }
    for (const char* key : kCallbackKeys) {
        const Janet callback = janet_get(callbacks, janet_ckeywordv(key));
        if (janet_checktype(callback, JANET_NIL)) {
            continue;
        }
        std::string internalName = "ned/vcs-" + name_ + "-" + key;
        janet_def(env_, internalName.c_str(), callback, "");
        callbackNames_.emplace(key, std::move(internalName));
    }
    if (!callbackNames_.contains("detect")) {
        throw std::runtime_error("ned: vcs provider \"" + name_ + "\" is missing the required :detect callback");
    }
}

const std::string* JanetVcsProvider::InternalName(const std::string& key) const {
    const auto found = callbackNames_.find(key);
    return found != callbackNames_.end() ? &found->second : nullptr;
}

Janet JanetVcsProvider::CallWithString(const std::string& fnInternalName, const std::string& arg) const {
    const std::string argName = "ned/vcs-call-arg";
    const Janet       argValue =
        janet_wrap_string(janet_string(reinterpret_cast<const std::uint8_t*>(arg.data()), static_cast<std::int32_t>(arg.size())));
    janet_def(env_, argName.c_str(), argValue, "");

    const std::string invokeExpr = "(" + fnInternalName + " " + argName + ")";
    Janet             out;
    std::string       capturedError;
    const int         signal = DoStringCapturingStacktrace(env_, invokeExpr, "ned-vcs", &out, &capturedError);
    if (signal != 0) {
        throw std::runtime_error("ned: error running vcs plugin callback '" + fnInternalName + "': " + capturedError);
    }
    return out;
}

Janet JanetVcsProvider::CallWithStrings(const std::string& fnInternalName, const std::string& first,
                                        const std::string& second) const {
    const std::string firstName  = "ned/vcs-call-arg";
    const std::string secondName = "ned/vcs-call-arg2";
    const Janet       firstValue = janet_wrap_string(
        janet_string(reinterpret_cast<const std::uint8_t*>(first.data()), static_cast<std::int32_t>(first.size())));
    const Janet secondValue = janet_wrap_string(
        janet_string(reinterpret_cast<const std::uint8_t*>(second.data()), static_cast<std::int32_t>(second.size())));
    janet_def(env_, firstName.c_str(), firstValue, "");
    janet_def(env_, secondName.c_str(), secondValue, "");

    const std::string invokeExpr = "(" + fnInternalName + " " + firstName + " " + secondName + ")";
    Janet             out;
    std::string       capturedError;
    const int         signal = DoStringCapturingStacktrace(env_, invokeExpr, "ned-vcs", &out, &capturedError);
    if (signal != 0) {
        throw std::runtime_error("ned: error running vcs plugin callback '" + fnInternalName + "': " + capturedError);
    }
    return out;
}

bool JanetVcsProvider::Detect(const std::filesystem::path& root) const {
    return FromJanet<bool>(CallWithString(*InternalName("detect"), root.string()));
}

editor::vcs::CommandSpec JanetVcsProvider::BlameArgv(const std::filesystem::path& path) const {
    const std::string* fn = InternalName("blame-argv");
    if (!fn) {
        return Provider::BlameArgv(path);
    }
    return ParseCommandSpec(CallWithString(*fn, path.string()));
}

std::vector<editor::vcs::BlameLine> JanetVcsProvider::ParseBlame(const std::string& stdout_) const {
    const std::string* fn = InternalName("parse-blame");
    if (!fn) {
        return Provider::ParseBlame(stdout_);
    }
    return ParseEntries<editor::vcs::BlameLine>(CallWithString(*fn, stdout_));
}

editor::vcs::CommandSpec JanetVcsProvider::LogArgv(const std::filesystem::path& path) const {
    const std::string* fn = InternalName("log-argv");
    if (!fn) {
        return Provider::LogArgv(path);
    }
    return ParseCommandSpec(CallWithString(*fn, path.string()));
}

std::vector<editor::vcs::LogEntry> JanetVcsProvider::ParseLog(const std::string& stdout_) const {
    const std::string* fn = InternalName("parse-log");
    if (!fn) {
        return Provider::ParseLog(stdout_);
    }
    return ParseEntries<editor::vcs::LogEntry>(CallWithString(*fn, stdout_));
}

editor::vcs::CommandSpec JanetVcsProvider::DiffArgv(const std::filesystem::path& path) const {
    const std::string* fn = InternalName("diff-argv");
    if (!fn) {
        return Provider::DiffArgv(path);
    }
    return ParseCommandSpec(CallWithString(*fn, path.string()));
}

std::vector<editor::vcs::DiffHunk> JanetVcsProvider::ParseDiff(const std::string& stdout_) const {
    const std::string* fn = InternalName("parse-diff");
    if (!fn) {
        return Provider::ParseDiff(stdout_);
    }
    return ParseDiffHunks(CallWithString(*fn, stdout_));
}

editor::vcs::CommandSpec JanetVcsProvider::WorkingDiffArgv(const std::filesystem::path& root) const {
    const std::string* fn = InternalName("working-diff-argv");
    if (!fn) {
        return Provider::WorkingDiffArgv(root);
    }
    return ParseCommandSpec(CallWithString(*fn, root.string()));
}

editor::vcs::CommandSpec JanetVcsProvider::CommitDiffArgv(const std::filesystem::path& root, const std::string& commitHash) const {
    const std::string* fn = InternalName("commit-diff-argv");
    if (!fn) {
        return Provider::CommitDiffArgv(root, commitHash);
    }
    return ParseCommandSpec(CallWithStrings(*fn, root.string(), commitHash));
}

editor::vcs::CommandSpec JanetVcsProvider::StatusArgv(const std::filesystem::path& root) const {
    const std::string* fn = InternalName("status-argv");
    if (!fn) {
        return Provider::StatusArgv(root);
    }
    return ParseCommandSpec(CallWithString(*fn, root.string()));
}

std::vector<editor::vcs::StatusEntry> JanetVcsProvider::ParseStatus(const std::string& stdout_) const {
    const std::string* fn = InternalName("parse-status");
    if (!fn) {
        return Provider::ParseStatus(stdout_);
    }
    return ParseStatusEntries(CallWithString(*fn, stdout_));
}

editor::vcs::CommandSpec JanetVcsProvider::StageArgv(const std::filesystem::path& path) const {
    const std::string* fn = InternalName("stage-argv");
    if (!fn) {
        return Provider::StageArgv(path);
    }
    return ParseCommandSpec(CallWithString(*fn, path.string()));
}

editor::vcs::CommandSpec JanetVcsProvider::UnstageArgv(const std::filesystem::path& path) const {
    const std::string* fn = InternalName("unstage-argv");
    if (!fn) {
        return Provider::UnstageArgv(path);
    }
    return ParseCommandSpec(CallWithString(*fn, path.string()));
}

editor::vcs::CommandSpec JanetVcsProvider::StagedDiffArgv(const std::filesystem::path& path) const {
    const std::string* fn = InternalName("staged-diff-argv");
    if (!fn) {
        return Provider::StagedDiffArgv(path);
    }
    return ParseCommandSpec(CallWithString(*fn, path.string()));
}

editor::vcs::CommandSpec JanetVcsProvider::StagePatchArgv(const std::filesystem::path& root,
                                                             const std::filesystem::path& patchPath) const {
    const std::string* fn = InternalName("stage-patch-argv");
    if (!fn) {
        return Provider::StagePatchArgv(root, patchPath);
    }
    return ParseCommandSpec(CallWithStrings(*fn, root.string(), patchPath.string()));
}

editor::vcs::CommandSpec JanetVcsProvider::UnstagePatchArgv(const std::filesystem::path& root,
                                                               const std::filesystem::path& patchPath) const {
    const std::string* fn = InternalName("unstage-patch-argv");
    if (!fn) {
        return Provider::UnstagePatchArgv(root, patchPath);
    }
    return ParseCommandSpec(CallWithStrings(*fn, root.string(), patchPath.string()));
}

editor::vcs::CommandSpec JanetVcsProvider::RevertPatchArgv(const std::filesystem::path& root,
                                                              const std::filesystem::path& patchPath) const {
    const std::string* fn = InternalName("revert-patch-argv");
    if (!fn) {
        return Provider::RevertPatchArgv(root, patchPath);
    }
    return ParseCommandSpec(CallWithStrings(*fn, root.string(), patchPath.string()));
}

editor::vcs::CommandSpec JanetVcsProvider::CommitArgv(const std::filesystem::path& root,
                                                         const std::string&           message) const {
    const std::string* fn = InternalName("commit-argv");
    if (!fn) {
        return Provider::CommitArgv(root, message);
    }
    return ParseCommandSpec(CallWithStrings(*fn, root.string(), message));
}

editor::vcs::CommandSpec JanetVcsProvider::BranchListArgv(const std::filesystem::path& root) const {
    const std::string* fn = InternalName("branch-list-argv");
    if (!fn) {
        return Provider::BranchListArgv(root);
    }
    return ParseCommandSpec(CallWithString(*fn, root.string()));
}

std::vector<editor::vcs::BranchEntry> JanetVcsProvider::ParseBranchList(const std::string& stdout_) const {
    const std::string* fn = InternalName("parse-branch-list");
    if (!fn) {
        return Provider::ParseBranchList(stdout_);
    }
    return ParseBranchEntries(CallWithString(*fn, stdout_));
}

editor::vcs::CommandSpec JanetVcsProvider::BranchSwitchArgv(const std::filesystem::path& root,
                                                               const std::string&           name) const {
    const std::string* fn = InternalName("branch-switch-argv");
    if (!fn) {
        return Provider::BranchSwitchArgv(root, name);
    }
    return ParseCommandSpec(CallWithStrings(*fn, root.string(), name));
}

editor::vcs::CommandSpec JanetVcsProvider::BranchCreateArgv(const std::filesystem::path& root,
                                                               const std::string&           name) const {
    const std::string* fn = InternalName("branch-create-argv");
    if (!fn) {
        return Provider::BranchCreateArgv(root, name);
    }
    return ParseCommandSpec(CallWithStrings(*fn, root.string(), name));
}

editor::vcs::CommandSpec JanetVcsProvider::RevertArgv(const std::filesystem::path& path) const {
    const std::string* fn = InternalName("revert-argv");
    if (!fn) {
        return Provider::RevertArgv(path);
    }
    return ParseCommandSpec(CallWithString(*fn, path.string()));
}

editor::vcs::CommandSpec JanetVcsProvider::StashListArgv(const std::filesystem::path& root) const {
    const std::string* fn = InternalName("stash-list-argv");
    if (!fn) {
        return Provider::StashListArgv(root);
    }
    return ParseCommandSpec(CallWithString(*fn, root.string()));
}

std::vector<editor::vcs::StashEntry> JanetVcsProvider::ParseStashList(const std::string& stdout_) const {
    const std::string* fn = InternalName("parse-stash-list");
    if (!fn) {
        return Provider::ParseStashList(stdout_);
    }
    return ParseStashEntries(CallWithString(*fn, stdout_));
}

editor::vcs::CommandSpec JanetVcsProvider::StashPushArgv(const std::filesystem::path& root, const std::string& message) const {
    const std::string* fn = InternalName("stash-push-argv");
    if (!fn) {
        return Provider::StashPushArgv(root, message);
    }
    return ParseCommandSpec(CallWithStrings(*fn, root.string(), message));
}

editor::vcs::CommandSpec JanetVcsProvider::StashPopArgv(const std::filesystem::path& root, const std::string& stashRef) const {
    const std::string* fn = InternalName("stash-pop-argv");
    if (!fn) {
        return Provider::StashPopArgv(root, stashRef);
    }
    return ParseCommandSpec(CallWithStrings(*fn, root.string(), stashRef));
}

editor::vcs::CommandSpec JanetVcsProvider::StashDropArgv(const std::filesystem::path& root, const std::string& stashRef) const {
    const std::string* fn = InternalName("stash-drop-argv");
    if (!fn) {
        return Provider::StashDropArgv(root, stashRef);
    }
    return ParseCommandSpec(CallWithStrings(*fn, root.string(), stashRef));
}

editor::vcs::CommandSpec JanetVcsProvider::PushArgv(const std::filesystem::path& root) const {
    const std::string* fn = InternalName("push-argv");
    if (!fn) {
        return Provider::PushArgv(root);
    }
    return ParseCommandSpec(CallWithString(*fn, root.string()));
}

editor::vcs::CommandSpec JanetVcsProvider::PullArgv(const std::filesystem::path& root) const {
    const std::string* fn = InternalName("pull-argv");
    if (!fn) {
        return Provider::PullArgv(root);
    }
    return ParseCommandSpec(CallWithString(*fn, root.string()));
}

editor::vcs::CommandSpec JanetVcsProvider::FetchArgv(const std::filesystem::path& root) const {
    const std::string* fn = InternalName("fetch-argv");
    if (!fn) {
        return Provider::FetchArgv(root);
    }
    return ParseCommandSpec(CallWithString(*fn, root.string()));
}

editor::vcs::CommandSpec JanetVcsProvider::AheadBehindArgv(const std::filesystem::path& root) const {
    const std::string* fn = InternalName("ahead-behind-argv");
    if (!fn) {
        return Provider::AheadBehindArgv(root);
    }
    return ParseCommandSpec(CallWithString(*fn, root.string()));
}

editor::vcs::AheadBehind JanetVcsProvider::ParseAheadBehind(const std::string& stdout_) const {
    const std::string* fn = InternalName("parse-ahead-behind");
    if (!fn) {
        return Provider::ParseAheadBehind(stdout_);
    }
    return ParseAheadBehindResult(CallWithString(*fn, stdout_));
}

} // namespace ned::janet
