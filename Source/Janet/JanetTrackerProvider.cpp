#include "JanetTrackerProvider.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <stdexcept>

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

    Janet ConnectionStruct(const editor::tracker::Connection& connection) {
        JanetKV* fields = janet_struct_begin(4);
        janet_struct_put(fields, janet_ckeywordv("name"), StringValue(connection.name));
        janet_struct_put(fields, janet_ckeywordv("provider"), StringValue(connection.provider));
        janet_struct_put(fields, janet_ckeywordv("url"), StringValue(connection.url));
        janet_struct_put(fields, janet_ckeywordv("email"), StringValue(connection.email));
        return janet_wrap_struct(janet_struct_end(fields));
    }

} // namespace

JanetTrackerProvider::JanetTrackerProvider(JanetTable* env, std::string name, Janet callbacks) : env_(env), name_(std::move(name)) {
    if (!janet_checktype(callbacks, JANET_TABLE) && !janet_checktype(callbacks, JANET_STRUCT)) {
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
        if (!janet_checktype(callback, JANET_FUNCTION) && !janet_checktype(callback, JANET_CFUNCTION)) {
            throw std::runtime_error("ned: tracker provider \"" + name_ + "\" needs a :" + key + " function");
        }
        janet_def(env_, CallbackName(name_, key).c_str(), callback, "");
    }
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
    return editor::tracker::CommandSpec{FromJanet<std::vector<std::string>>(result)};
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
        if (!janet_checktype(items[i], JANET_TABLE) && !janet_checktype(items[i], JANET_STRUCT)) {
            continue;
        }
        issues.push_back(editor::tracker::Issue{
            .key      = TextField(items[i], "key"),
            .title    = TextField(items[i], "title"),
            .status   = TextField(items[i], "status"),
            .assignee = TextField(items[i], "assignee"),
            .labels   = LabelsField(items[i]),
            .url      = TextField(items[i], "url"),
            .updated  = TextField(items[i], "updated"),
        });
    }
    return issues;
}

} // namespace ned::janet
