#include "ChangeSignature.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace ned::editor::changesig {

namespace {

    std::string_view Slice(std::string_view text, std::size_t start, std::size_t end) {
        return text.substr(start, end - start);
    }

    std::string_view Name(std::string_view text, const SignatureParameter& parameter) {
        return Slice(text, parameter.nameStartByte, parameter.nameEndByte);
    }

    std::string_view CallName(std::string_view text, const SignatureMarker& marker) {
        return Slice(text, marker.callNameStartByte, marker.callNameEndByte);
    }

    std::string_view Callee(std::string_view text, const CallMarker& call) {
        return Slice(text, call.calleeStartByte, call.calleeEndByte);
    }

    bool AnyVariadic(const std::vector<SignatureParameter>& params) {
        for (const SignatureParameter& parameter : params) {
            if (parameter.isVariadic) {
                return true;
            }
        }
        return false;
    }

} // namespace

MappingResult BuildPositionMapping(std::string_view oldText, const std::vector<SignatureParameter>& oldParams,
                                   std::string_view newText, const std::vector<SignatureParameter>& newParams) {
    if (AnyVariadic(oldParams) || AnyVariadic(newParams)) {
        return {.declined = true, .declineReason = "a variadic parameter can't be reordered, dropped or defaulted"};
    }

    // Positional parameters are matched by place; keyword-only ones by name
    // alone, so they are set aside first.
    std::vector<SignatureParameter> oldPositional;
    std::vector<std::string_view>   oldKeywords;
    for (const SignatureParameter& parameter : oldParams) {
        if (parameter.isKeyword) {
            oldKeywords.push_back(Name(oldText, parameter));
        }
        else {
            oldPositional.push_back(parameter);
        }
    }

    // Receivers lead the old list, and the new one keeps them first under
    // the same names. Whether a parameter is a receiver is the old
    // signature's to say: the new list was parsed out of context (a Python
    // `self` in a module-level wrapper is just a parameter).
    std::vector<ParameterReceiver> receivers;
    while (receivers.size() < oldPositional.size() && oldPositional[receivers.size()].receiver != ParameterReceiver::None) {
        receivers.push_back(oldPositional[receivers.size()].receiver);
    }
    const auto receiverMoved = [&] {
        for (std::size_t i = receivers.size(); i < oldPositional.size(); ++i) {
            if (oldPositional[i].receiver != ParameterReceiver::None) {
                return true; // a receiver after an ordinary parameter -- nothing to anchor on
            }
        }
        if (newParams.size() < receivers.size()) {
            return true;
        }
        for (std::size_t i = 0; i < receivers.size(); ++i) {
            if (newParams[i].isKeyword || Name(oldText, oldPositional[i]) != Name(newText, newParams[i])) {
                return true;
            }
        }
        return false;
    };
    if (receiverMoved()) {
        return {.declined = true, .declineReason = "a method's receiver has to stay first and unchanged"};
    }

    // Every OLD name that isn't unique is dropped from the lookup table
    // rather than trusted to pick the "right" occurrence -- a duplicate
    // name means the source this came from is already ambiguous, and this
    // module never guesses which one a new parameter of the same name
    // meant.
    std::unordered_map<std::string_view, std::size_t> oldIndexByName;
    std::unordered_map<std::string_view, bool>         oldNameIsAmbiguous;
    for (std::size_t i = 0; i < oldPositional.size(); ++i) {
        if (oldPositional[i].nameStartByte == oldPositional[i].nameEndByte) {
            continue; // nameless (an abstract declarator) -- can never be Kept, simply dropped
        }
        const std::string_view name = Name(oldText, oldPositional[i]);
        if (oldIndexByName.contains(name)) {
            oldNameIsAmbiguous[name] = true;
            continue;
        }
        oldIndexByName[name] = i;
    }
    for (const auto& [name, ambiguous] : oldNameIsAmbiguous) {
        if (ambiguous) {
            oldIndexByName.erase(name);
        }
    }
    const auto isOldKeyword = [&](std::string_view name) {
        return std::find(oldKeywords.begin(), oldKeywords.end(), name) != oldKeywords.end();
    };

    MappingResult                              result;
    std::unordered_map<std::string_view, bool> newNameSeen;
    result.origins.reserve(newParams.size());
    for (const SignatureParameter& parameter : newParams) {
        if (parameter.nameStartByte == parameter.nameEndByte) {
            return {.declined = true, .declineReason = "new parameter list has an unnamed parameter"};
        }
        const std::string_view name = Name(newText, parameter);
        if (newNameSeen.contains(name)) {
            return {.declined = true, .declineReason = "new parameter list uses the name \"" + std::string(name) + "\" more than once"};
        }
        newNameSeen[name] = true;

        if (parameter.isKeyword != isOldKeyword(name) && (isOldKeyword(name) || oldIndexByName.contains(name))) {
            return {.declined      = true,
                    .declineReason = "\"" + std::string(name) + "\" can't move between positional and keyword-only"};
        }
        if (parameter.isKeyword) {
            if (!isOldKeyword(name) && !parameter.hasDefaultValue) {
                return {.declined      = true,
                        .declineReason = "new parameter \"" + std::string(name) + "\" needs a default value"};
            }
            if (isOldKeyword(name)) {
                result.keptKeywords.emplace_back(name);
            }
            continue;
        }

        const auto found = oldIndexByName.find(name);
        if (found != oldIndexByName.end()) {
            result.origins.push_back(ParamOrigin{.kind = ParamOriginKind::Kept, .oldIndex = found->second});
            continue;
        }
        if (!parameter.hasDefaultValue) {
            return {.declined = true,
                   .declineReason = "new parameter \"" + std::string(name) + "\" needs a default value"};
        }
        result.origins.push_back(ParamOrigin{.kind                = ParamOriginKind::New,
                                             .newDefaultStartByte = parameter.defaultStartByte,
                                             .newDefaultEndByte   = parameter.defaultEndByte,
                                             .newLabelStartByte   = parameter.labelStartByte,
                                             .newLabelEndByte     = parameter.labelEndByte});
    }
    for (const std::string_view name : oldKeywords) {
        if (!newNameSeen.contains(name)) {
            result.droppedKeywords.emplace_back(name);
        }
    }
    result.oldArity  = oldPositional.size();
    result.receivers = std::move(receivers);
    return result;
}

ArgumentRewrite RewriteArgumentList(std::string_view callText, const std::vector<CallArgument>& oldArgs,
                                    std::string_view newDefaultText, const MappingResult& mapping, CallReceiver receiver) {
    // Positional arguments are realigned; a named one for a keyword
    // parameter follows them as written, or goes with its parameter.
    std::vector<CallArgument>     positional;
    std::vector<std::string_view> named;
    for (const CallArgument& argument : oldArgs) {
        if (argument.positional) {
            positional.push_back(argument);
            continue;
        }
        if (argument.nameStartByte == argument.nameEndByte) {
            return {.declined = true, .declineReason = "call site spreads its arguments"};
        }
        const std::string_view name = callText.substr(argument.nameStartByte, argument.nameEndByte - argument.nameStartByte);
        const auto             in   = [name](const std::vector<std::string>& names) {
            return std::find(names.begin(), names.end(), name) != names.end();
        };
        if (in(mapping.droppedKeywords)) {
            continue;
        }
        if (!in(mapping.keptKeywords)) {
            return {.declined = true, .declineReason = "call site passes an argument by name"};
        }
        named.push_back(callText.substr(argument.startByte, argument.endByte - argument.startByte));
    }

    // The leading receivers this call's object supplies.
    std::size_t implicit = 0;
    for (const ParameterReceiver parameter : mapping.receivers) {
        const bool supplied = parameter == ParameterReceiver::Always ||
                              (parameter == ParameterReceiver::Instance && receiver == CallReceiver::Instance) ||
                              (parameter == ParameterReceiver::Any && receiver != CallReceiver::None);
        if (!supplied) {
            break;
        }
        ++implicit;
    }
    if (positional.size() > mapping.oldArity - implicit) {
        return {.declined = true, .declineReason = "call site supplies more arguments than the old signature has parameters"};
    }
    std::string result;
    const auto  append = [&result](std::string_view piece) {
        if (!result.empty()) {
            result += ", ";
        }
        result += piece;
    };
    for (std::size_t i = 0; i < mapping.origins.size(); ++i) {
        const ParamOrigin& origin = mapping.origins[i];
        if (i < implicit) {
            continue; // a receiver the object supplies, kept in place
        }
        if (origin.kind == ParamOriginKind::Kept) {
            if (origin.oldIndex - implicit >= positional.size()) {
                return {.declined = true,
                       .declineReason = "call site supplies fewer arguments than the old signature has parameters"};
            }
            const CallArgument& argument = positional[origin.oldIndex - implicit];
            append(callText.substr(argument.startByte, argument.endByte - argument.startByte));
        }
        else {
            const std::string_view value =
                newDefaultText.substr(origin.newDefaultStartByte, origin.newDefaultEndByte - origin.newDefaultStartByte);
            if (origin.newLabelEndByte > origin.newLabelStartByte) {
                append(std::string(newDefaultText.substr(origin.newLabelStartByte, origin.newLabelEndByte - origin.newLabelStartByte)) +
                       ": " + std::string(value));
            }
            else {
                append(value);
            }
        }
    }
    for (const std::string_view piece : named) {
        append(piece);
    }
    return {.declined = false, .argumentListText = std::move(result)};
}

DiscoveryResult DiscoverSignatureAndCallSites(std::string_view name, std::size_t targetArity,
                                              const std::vector<std::filesystem::path>& candidates,
                                              const SourceLookup& readText, const FileScanner& scanner) {
    DiscoveryResult result;
    for (const std::filesystem::path& candidate : candidates) {
        const std::optional<std::string> text = readText(candidate);
        if (!text) {
            ++result.filesSkipped;
            continue;
        }
        const FileScanResult scanned = scanner(candidate, *text);

        for (const SignatureMarker& signature : scanned.signatures) {
            if (signature.callNameStartByte == signature.callNameEndByte || CallName(*text, signature) != name) {
                continue;
            }
            if (signature.parameters.size() != targetArity) {
                ++result.arityMismatches;
                continue;
            }
            result.signatureSites.push_back(SignatureSite{.file = candidate, .text = *text, .signature = signature});
        }

        for (const CallMarker& call : scanned.calls) {
            if (Callee(*text, call) != name) {
                continue;
            }
            result.callSites.push_back(CallSite{.file = candidate, .text = *text, .call = call});
        }
    }
    return result;
}

std::string CandidatePattern(std::string_view name) {
    if (name.empty()) {
        return {};
    }
    std::string pattern = "\\b";
    for (const char c : name) {
        // Escaped by hand rather than via RE2::QuoteMeta, the same call
        // ImportFixup.cpp::CandidatePattern makes and for the same reason:
        // this stays free of a dependency it otherwise has no use for.
        if (std::isalnum(static_cast<unsigned char>(c)) == 0 && c != '_') {
            pattern += '\\';
        }
        pattern += c;
    }
    pattern += "\\b";
    return pattern;
}

} // namespace ned::editor::changesig
