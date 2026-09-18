#include "GrammarFile.h"

#include <cstddef>
#include <cstdlib>

#include "Editor/JanetData.h"

namespace ned::editor::grammar::compile {

using janetdata::Value;
using Rule = GrammarFile::Rule;

GrammarFileError::GrammarFileError(int line, const std::string& message) : std::runtime_error((line > 0 ? std::to_string(line) + ": " : std::string()) + message), line_(line) {
}

namespace {

    // --- grammar.json -----------------------------------------------------

    using Json = nlohmann::ordered_json;

    Rule Wrap(Rule::Kind kind, Rule child) {
        Rule rule;
        rule.kind = kind;
        rule.children.push_back(std::move(child));
        return rule;
    }

    Rule RuleFromJson(const Json& j) {
        const std::string type = j.at("type").get<std::string>();
        Rule              rule;
        if (type == "BLANK") {
            rule.kind = Rule::Kind::Blank;
        }
        else if (type == "SYMBOL") {
            rule.kind = Rule::Kind::Symbol;
            rule.text = j.at("name").get<std::string>();
        }
        else if (type == "STRING") {
            rule.kind = Rule::Kind::String;
            rule.text = j.at("value").get<std::string>();
        }
        else if (type == "PATTERN") {
            rule.kind = Rule::Kind::Pattern;
            rule.text = j.at("value").get<std::string>();
            if (j.contains("flags")) {
                rule.flags = j.at("flags").get<std::string>();
            }
        }
        else if (type == "SEQ" || type == "CHOICE") {
            rule.kind = type == "SEQ" ? Rule::Kind::Seq : Rule::Kind::Choice;
            for (const Json& member : j.at("members")) {
                rule.children.push_back(RuleFromJson(member));
            }
        }
        else if (type == "REPEAT" || type == "REPEAT1" || type == "TOKEN" || type == "IMMEDIATE_TOKEN") {
            rule = Wrap(type == "REPEAT"    ? Rule::Kind::Repeat
                        : type == "REPEAT1" ? Rule::Kind::Repeat1
                        : type == "TOKEN"   ? Rule::Kind::Token
                                            : Rule::Kind::TokenImmediate,
                        RuleFromJson(j.at("content")));
        }
        else if (type == "PREC" || type == "PREC_LEFT" || type == "PREC_RIGHT" || type == "PREC_DYNAMIC") {
            rule              = Wrap(type == "PREC"         ? Rule::Kind::Prec
                                     : type == "PREC_LEFT"  ? Rule::Kind::PrecLeft
                                     : type == "PREC_RIGHT" ? Rule::Kind::PrecRight
                                                            : Rule::Kind::PrecDynamic,
                                     RuleFromJson(j.at("content")));
            const Json& value = j.at("value");
            if (value.is_string()) {
                rule.precedenceName = value.get<std::string>();
            }
            else {
                rule.precedence = value.get<int>();
            }
        }
        else if (type == "ALIAS") {
            rule       = Wrap(Rule::Kind::Alias, RuleFromJson(j.at("content")));
            rule.text  = j.at("value").get<std::string>();
            rule.named = j.at("named").get<bool>();
        }
        else if (type == "FIELD") {
            rule      = Wrap(Rule::Kind::Field, RuleFromJson(j.at("content")));
            rule.text = j.at("name").get<std::string>();
        }
        else if (type == "RESERVED") {
            rule      = Wrap(Rule::Kind::Reserved, RuleFromJson(j.at("content")));
            rule.text = j.at("context_name").get<std::string>();
        }
        else {
            throw GrammarFileError(0, "grammar.json rule type not in the grammar.janet vocabulary: " + type);
        }
        return rule;
    }

    std::vector<Rule> RulesFromJson(const Json& array) {
        std::vector<Rule> out;
        for (const Json& item : array) {
            out.push_back(RuleFromJson(item));
        }
        return out;
    }

    std::vector<std::string> StringsFromJson(const Json& array) {
        std::vector<std::string> out;
        for (const Json& item : array) {
            out.push_back(item.get<std::string>());
        }
        return out;
    }

    // --- grammar.janet, reading ------------------------------------------

    [[noreturn]] void Fail(const Value& at, const std::string& message) {
        throw GrammarFileError(at.line, message);
    }

    std::string Describe(const Value& value) {
        switch (value.kind) {
            case Value::Kind::Struct:
                return "a struct";
            case Value::Kind::Tuple:
                return "a tuple";
            case Value::Kind::String:
                return "the string " + janetdata::QuoteJanetString(value.text);
            case Value::Kind::Keyword:
                return ":" + value.text;
            case Value::Kind::Symbol:
                return "the symbol " + value.text;
            case Value::Kind::Bool:
                return value.boolean ? "true" : "false";
            case Value::Kind::Nil:
                return "nil";
        }
        return "a value";
    }

    // A rule name: a bare symbol, or (:ref "name") for one Janet reads as
    // data. true/false/nil arrive here as Bool/Nil, which is exactly the
    // mistake the :ref form exists to avoid.
    std::string ReadName(const Value& value) {
        if (value.IsSymbol()) {
            return value.text;
        }
        if (value.IsTuple() && value.items.size() == 2 && value.items[0].IsKeyword() && value.items[0].text == "ref" &&
            value.items[1].IsString()) {
            return value.items[1].text;
        }
        if (value.IsBool() || value.kind == Value::Kind::Nil) {
            Fail(value, "a rule named " + Describe(value) + " must be written (:ref \"" + Describe(value) + "\")");
        }
        Fail(value, "expected a rule name, got " + Describe(value));
    }

    Rule ReadRule(const Value& value);

    void ExpectArity(const Value& form, std::size_t arity) {
        if (form.items.size() != arity + 1) {
            Fail(form, "(:" + form.items[0].text + " ...) takes " + std::to_string(arity) + " argument" + (arity == 1 ? "" : "s") +
                           ", got " + std::to_string(form.items.size() - 1));
        }
    }

    Rule ReadForm(const Value& form) {
        if (form.items.empty() || !form.items[0].IsKeyword()) {
            Fail(form, "a rule form starts with a keyword, (:seq ...), (:choice ...) and so on");
        }
        const std::string& head = form.items[0].text;
        Rule               rule;
        if (head == "ref") {
            rule.kind = Rule::Kind::Symbol;
            rule.text = ReadName(form);
            return rule;
        }
        if (head == "seq" || head == "choice") {
            rule.kind = head == "seq" ? Rule::Kind::Seq : Rule::Kind::Choice;
            for (std::size_t i = 1; i < form.items.size(); ++i) {
                rule.children.push_back(ReadRule(form.items[i]));
            }
            return rule;
        }
        if (head == "repeat" || head == "repeat1" || head == "token" || head == "token-immediate") {
            ExpectArity(form, 1);
            return Wrap(head == "repeat"    ? Rule::Kind::Repeat
                        : head == "repeat1" ? Rule::Kind::Repeat1
                        : head == "token"   ? Rule::Kind::Token
                                            : Rule::Kind::TokenImmediate,
                        ReadRule(form.items[1]));
        }
        if (head == "prec" || head == "prec-left" || head == "prec-right" || head == "prec-dynamic") {
            ExpectArity(form, 2);
            rule = Wrap(head == "prec"         ? Rule::Kind::Prec
                        : head == "prec-left"  ? Rule::Kind::PrecLeft
                        : head == "prec-right" ? Rule::Kind::PrecRight
                                               : Rule::Kind::PrecDynamic,
                        ReadRule(form.items[2]));
            // The data reader hands a number over as a bare symbol.
            const Value& value = form.items[1];
            if (value.IsString()) {
                rule.precedenceName = value.text;
                return rule;
            }
            if (value.IsSymbol()) {
                char*      end        = nullptr;
                const long precedence = std::strtol(value.text.c_str(), &end, 10);
                if (end != value.text.c_str() && *end == '\0') {
                    rule.precedence = static_cast<int>(precedence);
                    return rule;
                }
            }
            Fail(value, "a precedence is an integer or a \"name\" from :precedences, got " + Describe(value));
        }
        if (head == "alias") {
            ExpectArity(form, 2);
            rule               = Wrap(Rule::Kind::Alias, ReadRule(form.items[1]));
            const Value& alias = form.items[2];
            if (alias.IsString()) {
                rule.text  = alias.text;
                rule.named = false;
            }
            else {
                rule.text  = ReadName(alias);
                rule.named = true;
            }
            return rule;
        }
        if (head == "field") {
            ExpectArity(form, 2);
            if (!form.items[1].IsKeyword()) {
                Fail(form.items[1], "a field name is a :keyword, got " + Describe(form.items[1]));
            }
            rule      = Wrap(Rule::Kind::Field, ReadRule(form.items[2]));
            rule.text = form.items[1].text;
            return rule;
        }
        if (head == "reserved") {
            ExpectArity(form, 2);
            if (!form.items[1].IsKeyword()) {
                Fail(form.items[1], "a reserved-word context is a :keyword, got " + Describe(form.items[1]));
            }
            rule      = Wrap(Rule::Kind::Reserved, ReadRule(form.items[2]));
            rule.text = form.items[1].text;
            return rule;
        }
        if (head == "pattern") {
            if (form.items.size() != 2 && form.items.size() != 3) {
                Fail(form, "(:pattern \"regex\") or (:pattern \"regex\" \"flags\")");
            }
            for (std::size_t i = 1; i < form.items.size(); ++i) {
                if (!form.items[i].IsString()) {
                    Fail(form.items[i], "a pattern and its flags are strings, got " + Describe(form.items[i]));
                }
            }
            rule.kind = Rule::Kind::Pattern;
            rule.text = form.items[1].text;
            if (form.items.size() == 3) {
                rule.flags = form.items[2].text;
            }
            return rule;
        }
        Fail(form, "unknown rule form (:" + head + " ...)");
    }

    Rule ReadRule(const Value& value) {
        Rule rule;
        switch (value.kind) {
            case Value::Kind::Symbol:
                rule.kind = Rule::Kind::Symbol;
                rule.text = value.text;
                return rule;
            case Value::Kind::String:
                rule.kind = Rule::Kind::String;
                rule.text = value.text;
                return rule;
            case Value::Kind::Keyword:
                if (value.text == "blank") {
                    return rule;
                }
                Fail(value, "unknown rule :" + value.text + " (only :blank stands alone)");
            case Value::Kind::Tuple:
                return ReadForm(value);
            case Value::Kind::Bool:
            case Value::Kind::Nil:
                Fail(value, "a rule named " + Describe(value) + " must be written (:ref \"" + Describe(value) + "\")");
            case Value::Kind::Struct:
                break;
        }
        Fail(value, "expected a rule, got " + Describe(value));
    }

    const Value& ExpectTuple(const Value& value, const char* what) {
        if (!value.IsTuple()) {
            Fail(value, std::string(what) + " is a [list], got " + Describe(value));
        }
        return value;
    }

    std::vector<Rule> ReadRules(const Value& list, const char* what) {
        std::vector<Rule> out;
        for (const Value& item : ExpectTuple(list, what).items) {
            out.push_back(ReadRule(item));
        }
        return out;
    }

    std::vector<std::string> ReadNames(const Value& list, const char* what) {
        std::vector<std::string> out;
        for (const Value& item : ExpectTuple(list, what).items) {
            out.push_back(ReadName(item));
        }
        return out;
    }

    // --- grammar.janet, writing ------------------------------------------

    constexpr std::size_t kWidth = 100;

    std::string Pad(std::size_t indent) {
        return std::string(indent, ' ');
    }

    bool NeedsRef(std::string_view name) {
        if (name.empty() || name == "true" || name == "false" || name == "nil") {
            return true;
        }
        const char first = name[0];
        if ((first >= '0' && first <= '9') || first == '-' || first == '+' || first == ':' || first == '#') {
            return true;
        }
        for (const char c : name) {
            if (c == ' ' || c == '(' || c == ')' || c == '[' || c == ']' || c == '{' || c == '}' || c == '"' || c == ':') {
                return true;
            }
        }
        return false;
    }

    std::string Name(std::string_view name) {
        return NeedsRef(name) ? "(:ref " + janetdata::QuoteJanetString(name) + ")" : std::string(name);
    }

    std::string Quote(std::string_view text) {
        return janetdata::QuoteJanetString(text);
    }

    const char* Head(Rule::Kind kind) {
        switch (kind) {
            case Rule::Kind::Seq:
                return ":seq";
            case Rule::Kind::Choice:
                return ":choice";
            case Rule::Kind::Repeat:
                return ":repeat";
            case Rule::Kind::Repeat1:
                return ":repeat1";
            case Rule::Kind::Prec:
                return ":prec";
            case Rule::Kind::PrecLeft:
                return ":prec-left";
            case Rule::Kind::PrecRight:
                return ":prec-right";
            case Rule::Kind::PrecDynamic:
                return ":prec-dynamic";
            case Rule::Kind::Token:
                return ":token";
            case Rule::Kind::TokenImmediate:
                return ":token-immediate";
            case Rule::Kind::Alias:
                return ":alias";
            case Rule::Kind::Field:
                return ":field";
            case Rule::Kind::Reserved:
                return ":reserved";
            case Rule::Kind::Blank:
            case Rule::Kind::Symbol:
            case Rule::Kind::String:
            case Rule::Kind::Pattern:
                break;
        }
        return "";
    }

    // The arguments that precede a wrapper's child, e.g. `1` in (:prec 1 x).
    std::string LeadingArguments(const Rule& rule) {
        switch (rule.kind) {
            case Rule::Kind::Prec:
            case Rule::Kind::PrecLeft:
            case Rule::Kind::PrecRight:
            case Rule::Kind::PrecDynamic:
                return rule.precedenceName.empty() ? std::to_string(rule.precedence) : Quote(rule.precedenceName);
            case Rule::Kind::Field:
            case Rule::Kind::Reserved:
                return ":" + rule.text;
            default:
                return "";
        }
    }

    std::string Compact(const Rule& rule);

    std::string Atom(const Rule& rule) {
        switch (rule.kind) {
            case Rule::Kind::Blank:
                return ":blank";
            case Rule::Kind::Symbol:
                return Name(rule.text);
            case Rule::Kind::String:
                return Quote(rule.text);
            case Rule::Kind::Pattern:
                return "(:pattern " + Quote(rule.text) + (rule.flags.empty() ? "" : " " + Quote(rule.flags)) + ")";
            default:
                return "";
        }
    }

    // Atomic by kind, not by child count: an empty (:seq) or (:choice) is
    // a form with no members, not an atom.
    bool IsAtom(const Rule& rule) {
        switch (rule.kind) {
            case Rule::Kind::Blank:
            case Rule::Kind::Symbol:
            case Rule::Kind::String:
            case Rule::Kind::Pattern:
                return true;
            default:
                return false;
        }
    }

    std::string Compact(const Rule& rule) {
        if (IsAtom(rule)) {
            return Atom(rule);
        }
        std::string out = "(";
        out += Head(rule.kind);
        const std::string leading = LeadingArguments(rule);
        if (!leading.empty()) {
            out += " " + leading;
        }
        for (const Rule& child : rule.children) {
            out += " " + Compact(child);
        }
        if (rule.kind == Rule::Kind::Alias) {
            out += " " + (rule.named ? Name(rule.text) : Quote(rule.text));
        }
        out += ")";
        return out;
    }

    // One line when it fits in the remaining width, else the head on the
    // first line and each child on its own indented line.
    std::string Render(const Rule& rule, std::size_t indent) {
        const std::string compact = Compact(rule);
        if (IsAtom(rule) || indent + compact.size() <= kWidth) {
            return compact;
        }
        std::string out = "(";
        out += Head(rule.kind);
        const std::string leading = LeadingArguments(rule);
        if (!leading.empty()) {
            out += " " + leading;
        }
        const std::size_t childIndent = indent + 1;
        for (const Rule& child : rule.children) {
            out += "\n" + Pad(childIndent) + Render(child, childIndent);
        }
        if (rule.kind == Rule::Kind::Alias) {
            out += "\n" + Pad(childIndent) + (rule.named ? Name(rule.text) : Quote(rule.text));
        }
        out += ")";
        return out;
    }

    // A top-level list: one line when it fits, else one item per line.
    std::string RenderList(const std::vector<std::string>& items, std::size_t indent) {
        std::string compact = "[";
        for (std::size_t i = 0; i < items.size(); ++i) {
            compact += (i == 0 ? "" : " ") + items[i];
        }
        compact += "]";
        if (indent + compact.size() <= kWidth) {
            return compact;
        }
        std::string out = "[";
        for (std::size_t i = 0; i < items.size(); ++i) {
            out += (i == 0 ? "" : "\n" + Pad(indent + 1)) + items[i];
        }
        return out + "]";
    }

} // namespace

GrammarFile ParseGrammarJson(const Json& grammar) {
    GrammarFile out;
    out.name = grammar.at("name").get<std::string>();
    if (grammar.contains("word") && grammar.at("word").is_string()) {
        out.word = grammar.at("word").get<std::string>();
    }
    if (grammar.contains("inherits") && grammar.at("inherits").is_string()) {
        out.inherits = grammar.at("inherits").get<std::string>();
    }
    for (const auto& [name, rule] : grammar.at("rules").items()) {
        out.rules.emplace_back(name, RuleFromJson(rule));
    }
    if (grammar.contains("extras")) {
        out.extras = RulesFromJson(grammar.at("extras"));
    }
    if (grammar.contains("externals")) {
        out.externals = RulesFromJson(grammar.at("externals"));
    }
    if (grammar.contains("conflicts")) {
        for (const Json& conflict : grammar.at("conflicts")) {
            out.conflicts.push_back(StringsFromJson(conflict));
        }
    }
    if (grammar.contains("precedences")) {
        for (const Json& precedence : grammar.at("precedences")) {
            out.precedences.push_back(RulesFromJson(precedence));
        }
    }
    if (grammar.contains("inline")) {
        out.inlineRules = StringsFromJson(grammar.at("inline"));
    }
    if (grammar.contains("supertypes")) {
        out.supertypes = StringsFromJson(grammar.at("supertypes"));
    }
    if (grammar.contains("reserved")) {
        for (const auto& [context, words] : grammar.at("reserved").items()) {
            out.reserved.emplace_back(context, RulesFromJson(words));
        }
    }
    return out;
}

GrammarFile ParseGrammarJanet(std::string_view source) {
    Value top;
    try {
        top = janetdata::ParseJanetData(source);
    }
    catch (const janetdata::JanetDataError& error) {
        throw GrammarFileError(error.Line(), error.what());
    }
    if (!top.IsStruct()) {
        Fail(top, "a grammar file is one struct {:name ... :rules {...}}");
    }

    GrammarFile out;
    const auto  string = [&](const char* key, std::string& into) {
        if (const Value* value = top.Get(key)) {
            if (!value->IsString()) {
                Fail(*value, std::string(":") + key + " is a string, got " + Describe(*value));
            }
            into = value->text;
        }
    };
    string("name", out.name);
    if (out.name.empty()) {
        Fail(top, ":name is required");
    }
    string("inherits", out.inherits);
    if (const Value* word = top.Get("word")) {
        out.word = ReadName(*word);
    }

    const Value* rules = top.Get("rules");
    if (rules == nullptr || !rules->IsStruct()) {
        Fail(top, ":rules is required and is a struct of name -> rule");
    }
    for (std::size_t i = 0; i + 1 < rules->pairs.size(); i += 2) {
        out.rules.emplace_back(ReadName(rules->pairs[i]), ReadRule(rules->pairs[i + 1]));
    }
    if (out.rules.empty()) {
        Fail(*rules, ":rules has no rules -- the first rule is the start rule");
    }

    if (const Value* extras = top.Get("extras")) {
        out.extras = ReadRules(*extras, ":extras");
    }
    if (const Value* externals = top.Get("externals")) {
        out.externals = ReadRules(*externals, ":externals");
    }
    if (const Value* conflicts = top.Get("conflicts")) {
        for (const Value& conflict : ExpectTuple(*conflicts, ":conflicts").items) {
            out.conflicts.push_back(ReadNames(conflict, "a conflict"));
        }
    }
    if (const Value* precedences = top.Get("precedences")) {
        for (const Value& precedence : ExpectTuple(*precedences, ":precedences").items) {
            std::vector<Rule> items;
            for (const Value& item : ExpectTuple(precedence, "a precedence list").items) {
                Rule rule = ReadRule(item);
                if (rule.kind != Rule::Kind::Symbol && rule.kind != Rule::Kind::String) {
                    Fail(item, "a precedence list holds rule names and \"precedence names\"");
                }
                items.push_back(std::move(rule));
            }
            out.precedences.push_back(std::move(items));
        }
    }
    if (const Value* inlineRules = top.Get("inline")) {
        out.inlineRules = ReadNames(*inlineRules, ":inline");
    }
    if (const Value* supertypes = top.Get("supertypes")) {
        out.supertypes = ReadNames(*supertypes, ":supertypes");
    }
    if (const Value* reserved = top.Get("reserved")) {
        if (!reserved->IsStruct()) {
            Fail(*reserved, ":reserved is a struct of :context -> [words]");
        }
        for (std::size_t i = 0; i + 1 < reserved->pairs.size(); i += 2) {
            const Value& context = reserved->pairs[i];
            if (!context.IsKeyword()) {
                Fail(context, "a reserved-word context is a :keyword, got " + Describe(context));
            }
            out.reserved.emplace_back(context.text, ReadRules(reserved->pairs[i + 1], "a reserved-word list"));
        }
    }
    return out;
}

std::string ToGrammarJanet(const GrammarFile& grammar) {
    std::string out = "# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;\n"
                      "# the form vocabulary is documented there. Rule order is meaning: the first\n"
                      "# rule is the start rule.\n";
    out += "{:name " + Quote(grammar.name) + "\n";
    if (!grammar.word.empty()) {
        out += " :word " + Name(grammar.word) + "\n";
    }
    if (!grammar.inherits.empty()) {
        out += " :inherits " + Quote(grammar.inherits) + "\n";
    }

    const auto renderRules = [](const std::vector<Rule>& rules, std::size_t indent) {
        std::vector<std::string> items;
        for (const Rule& rule : rules) {
            items.push_back(Render(rule, indent + 1));
        }
        return RenderList(items, indent);
    };
    const auto renderNames = [](const std::vector<std::string>& names, std::size_t indent) {
        std::vector<std::string> items;
        for (const std::string& name : names) {
            items.push_back(Name(name));
        }
        return RenderList(items, indent);
    };

    out += " :extras " + renderRules(grammar.extras, 9) + "\n";
    {
        std::vector<std::string> conflicts;
        for (const std::vector<std::string>& conflict : grammar.conflicts) {
            conflicts.push_back(renderNames(conflict, 13));
        }
        out += " :conflicts " + RenderList(conflicts, 12) + "\n";
    }
    {
        std::vector<std::string> precedences;
        for (const std::vector<Rule>& precedence : grammar.precedences) {
            precedences.push_back(renderRules(precedence, 15));
        }
        out += " :precedences " + RenderList(precedences, 14) + "\n";
    }
    out += " :externals " + renderRules(grammar.externals, 12) + "\n";
    out += " :inline " + renderNames(grammar.inlineRules, 9) + "\n";
    out += " :supertypes " + renderNames(grammar.supertypes, 13) + "\n";
    if (!grammar.reserved.empty()) {
        out += " :reserved\n {";
        bool first = true;
        for (const auto& [context, words] : grammar.reserved) {
            out += (first ? "" : "\n  ") + (":" + context) + " " + renderRules(words, 3 + context.size() + 2);
            first = false;
        }
        out += "}\n";
    }

    out += " :rules\n {";
    bool first = true;
    for (const auto& [name, rule] : grammar.rules) {
        const std::string rendered = Name(name);
        out += (first ? "" : "\n  ") + rendered + " " + Render(rule, 2 + rendered.size() + 1);
        first = false;
    }
    out += "}}\n";
    return out;
}

} // namespace ned::editor::grammar::compile
