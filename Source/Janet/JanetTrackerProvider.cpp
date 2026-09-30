#include "JanetTrackerProvider.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <stdexcept>
#include <string_view>

#include "Environment.h"
#include "Value.h"

namespace ned::janet {

namespace {

    Janet StringValue(const std::string& text) {
        return janet_stringv(reinterpret_cast<const std::uint8_t*>(text.data()), static_cast<std::int32_t>(text.size()));
    }

    std::string CallbackName(const std::string& provider, const char* key) {
        return "ned/tracker-" + provider + "-" + key;
    }

    // Lenient like JanetVcsProvider's field readers: a plugin bug degrades a
    // field to "", it doesn't drop the panel. Whole numbers are accepted
    // because GitHub issue numbers arrive from JSON as numbers.
    std::string TextField(Janet entry, const char* key) {
        const Janet value = janet_get(entry, janet_ckeywordv(key));
        if (janet_checktype(value, JANET_STRING) || janet_checktype(value, JANET_KEYWORD) || janet_checktype(value, JANET_SYMBOL)) {
            const std::uint8_t* bytes = janet_unwrap_string(value);
            return std::string(reinterpret_cast<const char*>(bytes), janet_string_length(bytes));
        }
        if (janet_checktype(value, JANET_NUMBER)) {
            const double number = janet_unwrap_number(value);
            if (std::trunc(number) == number && std::abs(number) < 1e15) {
                return std::to_string(static_cast<std::int64_t>(number));
            }
            return std::to_string(number);
        }
        return {};
    }

    std::vector<std::string> LabelsField(Janet entry) {
        const Janet  value = janet_get(entry, janet_ckeywordv("labels"));
        const Janet* items = nullptr;
        std::int32_t count = 0;
        if (!janet_indexed_view(value, &items, &count)) {
            return {};
        }
        std::vector<std::string> labels;
        for (std::int32_t i = 0; i < count; ++i) {
            if (janet_checktype(items[i], JANET_STRING)) {
                labels.push_back(FromJanet<std::string>(items[i]));
            }
        }
        return labels;
    }

    editor::tracker::Issue IssueFields(Janet entry) {
        return editor::tracker::Issue{
            .key      = TextField(entry, "key"),
            .title    = TextField(entry, "title"),
            .status   = TextField(entry, "status"),
            .assignee = TextField(entry, "assignee"),
            .labels   = LabelsField(entry),
            .url      = TextField(entry, "url"),
            .updated  = TextField(entry, "updated"),
        };
    }

    bool IsDictionary(Janet value) {
        return janet_checktype(value, JANET_TABLE) || janet_checktype(value, JANET_STRUCT);
    }

    // An argv array, or {:argv [...] :curl-credentials true} for a curl
    // command the Runner authenticates (Provider.h's CommandSpec).
    editor::tracker::CommandSpec CommandSpecFrom(Janet result) {
        if (IsDictionary(result)) {
            editor::tracker::CommandSpec spec{
                .argv            = FromJanet<std::vector<std::string>>(janet_get(result, janet_ckeywordv("argv"))),
                .curlCredentials = janet_truthy(janet_get(result, janet_ckeywordv("curl-credentials"))) != 0,
            };
            const Janet input = janet_get(result, janet_ckeywordv("input"));
            if (!janet_checktype(input, JANET_NIL)) {
                spec.input = FromJanet<std::string>(input);
            }
            return spec;
        }
        return editor::tracker::CommandSpec{.argv = FromJanet<std::vector<std::string>>(result)};
    }

    bool IsCallable(Janet value) {
        return janet_checktype(value, JANET_FUNCTION) || janet_checktype(value, JANET_CFUNCTION);
    }

    Janet ConnectionStruct(const editor::tracker::Connection& connection) {
        JanetKV* fields = janet_struct_begin(4);
        janet_struct_put(fields, janet_ckeywordv("name"), StringValue(connection.name));
        janet_struct_put(fields, janet_ckeywordv("provider"), StringValue(connection.provider));
        janet_struct_put(fields, janet_ckeywordv("url"), StringValue(connection.url));
        janet_struct_put(fields, janet_ckeywordv("email"), StringValue(connection.email));
        return janet_wrap_struct(janet_struct_end(fields));
    }

    // The callbacks each capability needs, all of them or none.
    struct CapabilityCallbacks {
        editor::tracker::Capability        capability;
        std::initializer_list<const char*> keys;
    };
    const CapabilityCallbacks kCapabilityCallbacks[] = {
        {editor::tracker::Capability::Transition, {"transitions-argv", "parse-transitions", "transition-argv"}},
        {editor::tracker::Capability::Assign, {"assignees-argv", "parse-assignees", "assign-argv"}},
        {editor::tracker::Capability::Comment, {"comment-argv"}},
        {editor::tracker::Capability::Worklog, {"worklog-argv"}},
        {editor::tracker::Capability::ProjectKeys, {"project-keys-argv", "parse-project-keys"}},
        {editor::tracker::Capability::Mine, {"mine-query"}},
    };

    std::vector<editor::tracker::Choice> ChoicesFrom(const std::string& provider, const char* callback, Janet result) {
        const Janet* items = nullptr;
        std::int32_t count = 0;
        if (!janet_indexed_view(result, &items, &count)) {
            throw std::runtime_error("tracker provider \"" + provider + "\": :" + callback + " must return an array of :id :name tables");
        }
        std::vector<editor::tracker::Choice> choices;
        for (std::int32_t i = 0; i < count; ++i) {
            if (IsDictionary(items[i])) {
                choices.push_back(editor::tracker::Choice{
                    .id = TextField(items[i], "id"), .name = TextField(items[i], "name"), .email = TextField(items[i], "email")});
            }
        }
        return choices;
    }

} // namespace

JanetTrackerProvider::JanetTrackerProvider(JanetTable* env, std::string name, Janet callbacks) : env_(env), name_(std::move(name)) {
    if (!IsDictionary(callbacks)) {
        throw std::runtime_error("ned: tracker-register-provider expects a struct/table of callbacks keyed by keyword");
    }
    // The name becomes part of a Janet symbol that is evaluated as source.
    const bool validName = !name_.empty() && std::ranges::all_of(name_, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    });
    if (!validName) {
        throw std::runtime_error("ned: tracker provider name \"" + name_ + "\" must be letters, digits, '-' or '_'");
    }
    for (const char* key : {"list-argv", "parse-list"}) {
        const Janet callback = janet_get(callbacks, janet_ckeywordv(key));
        if (!IsCallable(callback)) {
            throw std::runtime_error("ned: tracker provider \"" + name_ + "\" needs a :" + key + " function");
        }
        janet_def(env_, CallbackName(name_, key).c_str(), callback, "");
    }
    // The detail view is optional, but half of it is a plugin bug.
    const Janet viewArgv  = janet_get(callbacks, janet_ckeywordv("view-argv"));
    const Janet parseView = janet_get(callbacks, janet_ckeywordv("parse-view"));
    if (!janet_checktype(viewArgv, JANET_NIL) || !janet_checktype(parseView, JANET_NIL)) {
        if (!IsCallable(viewArgv) || !IsCallable(parseView)) {
            throw std::runtime_error("ned: tracker provider \"" + name_ + "\" needs :view-argv and :parse-view together, as functions");
        }
        janet_def(env_, CallbackName(name_, "view-argv").c_str(), viewArgv, "");
        janet_def(env_, CallbackName(name_, "parse-view").c_str(), parseView, "");
        hasView_ = true;
    }
    const Janet detect = janet_get(callbacks, janet_ckeywordv("detect"));
    if (!janet_checktype(detect, JANET_NIL)) {
        if (!IsCallable(detect)) {
            throw std::runtime_error("ned: tracker provider \"" + name_ + "\" :detect must be a function");
        }
        janet_def(env_, CallbackName(name_, "detect").c_str(), detect, "");
        hasDetect_ = true;
    }
    numericKeys_ = janet_truthy(janet_get(callbacks, janet_ckeywordv("numeric-keys"))) != 0;
    for (const CapabilityCallbacks& group : kCapabilityCallbacks) {
        std::size_t present = 0;
        for (const char* key : group.keys) {
            present += janet_checktype(janet_get(callbacks, janet_ckeywordv(key)), JANET_NIL) ? 0 : 1;
        }
        if (present == 0) {
            continue;
        }
        std::string names;
        for (const char* key : group.keys) {
            names += (names.empty() ? ":" : ", :") + std::string(key);
        }
        for (const char* key : group.keys) {
            const Janet callback = janet_get(callbacks, janet_ckeywordv(key));
            // :mine-query may be a fixed query rather than a function of the connection.
            const bool fixedQuery = std::string_view(key) == "mine-query" && janet_checktype(callback, JANET_STRING);
            if (!IsCallable(callback) && !fixedQuery) {
                throw std::runtime_error("ned: tracker provider \"" + name_ + "\" needs " + names + " together, as functions");
            }
            janet_def(env_, CallbackName(name_, key).c_str(), callback, "");
        }
        capabilities_.insert(group.capability);
    }
}

bool JanetTrackerProvider::Supports(editor::tracker::Capability capability) const {
    return capabilities_.contains(capability);
}

editor::tracker::CommandSpec JanetTrackerProvider::TransitionsArgv(const editor::tracker::Connection& connection,
                                                                   const std::string&                 key) const {
    return CommandSpecFrom(Call(CallbackName(name_, "transitions-argv"), {ConnectionStruct(connection), StringValue(key)}));
}

std::vector<editor::tracker::Choice> JanetTrackerProvider::ParseTransitions(const std::string& output) const {
    return ChoicesFrom(name_, "parse-transitions", Call(CallbackName(name_, "parse-transitions"), {StringValue(output)}));
}

editor::tracker::CommandSpec JanetTrackerProvider::TransitionArgv(const editor::tracker::Connection& connection, const std::string& key,
                                                                  const std::string& transitionId) const {
    return CommandSpecFrom(
        Call(CallbackName(name_, "transition-argv"), {ConnectionStruct(connection), StringValue(key), StringValue(transitionId)}));
}

editor::tracker::CommandSpec JanetTrackerProvider::AssigneesArgv(const editor::tracker::Connection& connection,
                                                                 const std::string&                 key) const {
    return CommandSpecFrom(Call(CallbackName(name_, "assignees-argv"), {ConnectionStruct(connection), StringValue(key)}));
}

std::vector<editor::tracker::Choice> JanetTrackerProvider::ParseAssignees(const std::string& output) const {
    return ChoicesFrom(name_, "parse-assignees", Call(CallbackName(name_, "parse-assignees"), {StringValue(output)}));
}

editor::tracker::CommandSpec JanetTrackerProvider::AssignArgv(const editor::tracker::Connection& connection, const std::string& key,
                                                              const std::string& userId) const {
    return CommandSpecFrom(Call(CallbackName(name_, "assign-argv"), {ConnectionStruct(connection), StringValue(key), StringValue(userId)}));
}

editor::tracker::CommandSpec JanetTrackerProvider::CommentArgv(const editor::tracker::Connection& connection, const std::string& key,
                                                               const std::string& body) const {
    return CommandSpecFrom(Call(CallbackName(name_, "comment-argv"), {ConnectionStruct(connection), StringValue(key), StringValue(body)}));
}

editor::tracker::CommandSpec JanetTrackerProvider::WorklogArgv(const editor::tracker::Connection& connection, const std::string& key,
                                                               const editor::tracker::Worklog& worklog) const {
    return CommandSpecFrom(Call(CallbackName(name_, "worklog-argv"),
                                {ConnectionStruct(connection), StringValue(key), janet_wrap_number(static_cast<double>(worklog.started)),
                                 janet_wrap_number(static_cast<double>(worklog.seconds))}));
}

editor::tracker::CommandSpec JanetTrackerProvider::ProjectKeysArgv(const editor::tracker::Connection& connection) const {
    return CommandSpecFrom(Call(CallbackName(name_, "project-keys-argv"), {ConnectionStruct(connection)}));
}

std::vector<std::string> JanetTrackerProvider::ParseProjectKeys(const std::string& output) const {
    const Janet  result = Call(CallbackName(name_, "parse-project-keys"), {StringValue(output)});
    const Janet* items  = nullptr;
    std::int32_t count  = 0;
    if (!janet_indexed_view(result, &items, &count)) {
        throw std::runtime_error("tracker provider \"" + name_ + "\": :parse-project-keys must return an array of strings");
    }
    std::vector<std::string> keys;
    for (std::int32_t i = 0; i < count; ++i) {
        if (janet_checktype(items[i], JANET_STRING)) {
            keys.push_back(FromJanet<std::string>(items[i]));
        }
    }
    return keys;
}

std::string JanetTrackerProvider::MineQuery(const editor::tracker::Connection& connection) const {
    const std::string name = CallbackName(name_, "mine-query");
    Janet             query;
    if (janet_resolve(env_, janet_csymbol(name.c_str()), &query) == JANET_BINDING_DEF && janet_checktype(query, JANET_STRING)) {
        return FromJanet<std::string>(query);
    }
    const Janet result = Call(name, {ConnectionStruct(connection)});
    if (!janet_checktype(result, JANET_STRING)) {
        throw std::runtime_error("tracker provider \"" + name_ + "\": :mine-query must return a query string");
    }
    return FromJanet<std::string>(result);
}

Janet JanetTrackerProvider::Call(const std::string& callback, std::initializer_list<Janet> args) const {
    std::string invokeExpr = "(" + callback;
    int         index      = 0;
    for (const Janet arg : args) {
        const std::string argName = "ned/tracker-call-arg" + std::to_string(index++);
        janet_def(env_, argName.c_str(), arg, "");
        invokeExpr += " " + argName;
    }
    invokeExpr += ")";

    Janet       out;
    std::string capturedError;
    if (DoStringCapturingStacktrace(env_, invokeExpr, "ned-tracker", &out, &capturedError) != 0) {
        throw std::runtime_error("tracker provider \"" + name_ + "\": " + capturedError);
    }
    return out;
}

editor::tracker::CommandSpec JanetTrackerProvider::ListArgv(const editor::tracker::Connection& connection,
                                                            const std::string&                 query) const {
    const Janet result = Call(CallbackName(name_, "list-argv"), {ConnectionStruct(connection), StringValue(query)});
    return CommandSpecFrom(result);
}

std::vector<editor::tracker::Issue> JanetTrackerProvider::ParseList(const std::string& output) const {
    const Janet  result = Call(CallbackName(name_, "parse-list"), {StringValue(output)});
    const Janet* items  = nullptr;
    std::int32_t count  = 0;
    if (!janet_indexed_view(result, &items, &count)) {
        throw std::runtime_error("tracker provider \"" + name_ + "\": :parse-list must return an array of issue tables");
    }
    std::vector<editor::tracker::Issue> issues;
    issues.reserve(static_cast<std::size_t>(count));
    for (std::int32_t i = 0; i < count; ++i) {
        if (IsDictionary(items[i])) {
            issues.push_back(IssueFields(items[i]));
        }
    }
    return issues;
}

std::optional<editor::tracker::CommandSpec> JanetTrackerProvider::ViewArgv(const editor::tracker::Connection& connection,
                                                                           const std::string&                 key) const {
    if (!hasView_) {
        return std::nullopt;
    }
    const Janet result = Call(CallbackName(name_, "view-argv"), {ConnectionStruct(connection), StringValue(key)});
    return CommandSpecFrom(result);
}

editor::tracker::IssueDetail JanetTrackerProvider::ParseView(const std::string& output) const {
    const Janet result = Call(CallbackName(name_, "parse-view"), {StringValue(output)});
    if (!IsDictionary(result)) {
        throw std::runtime_error("tracker provider \"" + name_ + "\": :parse-view must return an issue table");
    }
    editor::tracker::IssueDetail detail{.issue = IssueFields(result), .body = TextField(result, "body")};

    const Janet* comments = nullptr;
    std::int32_t count    = 0;
    if (janet_indexed_view(janet_get(result, janet_ckeywordv("comments")), &comments, &count)) {
        for (std::int32_t i = 0; i < count; ++i) {
            if (IsDictionary(comments[i])) {
                detail.comments.push_back(editor::tracker::Comment{.author  = TextField(comments[i], "author"),
                                                                   .created = TextField(comments[i], "created"),
                                                                   .body    = TextField(comments[i], "body")});
            }
        }
    }
    return detail;
}

std::vector<editor::tracker::Detected> JanetTrackerProvider::Detect(const std::vector<std::string>& remoteUrls) const {
    if (!hasDetect_) {
        return {};
    }
    JanetArray* urls = janet_array(static_cast<std::int32_t>(remoteUrls.size()));
    for (const std::string& url : remoteUrls) {
        janet_array_push(urls, StringValue(url));
    }
    const Janet  result = Call(CallbackName(name_, "detect"), {janet_wrap_array(urls)});
    const Janet* items  = nullptr;
    std::int32_t count  = 0;
    if (!janet_indexed_view(result, &items, &count)) {
        throw std::runtime_error("tracker provider \"" + name_ + "\": :detect must return an array of connection tables");
    }
    std::vector<editor::tracker::Detected> detected;
    for (std::int32_t i = 0; i < count; ++i) {
        if (!IsDictionary(items[i])) {
            continue;
        }
        editor::tracker::Detected found{.connection = {.name     = TextField(items[i], "name"),
                                                       .provider = name_,
                                                       .url      = TextField(items[i], "url"),
                                                       .email    = TextField(items[i], "email")}};
        const Janet*              panels     = nullptr;
        std::int32_t              panelCount = 0;
        if (janet_indexed_view(janet_get(items[i], janet_ckeywordv("panels")), &panels, &panelCount)) {
            for (std::int32_t j = 0; j < panelCount; ++j) {
                if (IsDictionary(panels[j])) {
                    found.panels.push_back(editor::tracker::Panel{.name       = TextField(panels[j], "name"),
                                                                  .connection = found.connection.name,
                                                                  .query      = TextField(panels[j], "query"),
                                                                  .glyph      = TextField(panels[j], "glyph")});
                }
            }
        }
        detected.push_back(std::move(found));
    }
    return detected;
}

} // namespace ned::janet
