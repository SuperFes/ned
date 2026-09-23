#include "ChangeSignature.h"

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

    std::string_view Name(std::string_view text, const SignatureMarker& marker) {
        return Slice(text, marker.nameStartByte, marker.nameEndByte);
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

    // Every OLD name that isn't unique is dropped from the lookup table
    // rather than trusted to pick the "right" occurrence -- a duplicate
    // name means the source this came from is already ambiguous, and this
    // module never guesses which one a new parameter of the same name
    // meant.
    std::unordered_map<std::string_view, std::size_t> oldIndexByName;
    std::unordered_map<std::string_view, bool>         oldNameIsAmbiguous;
    for (std::size_t i = 0; i < oldParams.size(); ++i) {
        if (oldParams[i].nameStartByte == oldParams[i].nameEndByte) {
            continue; // nameless (an abstract declarator) -- can never be Kept, simply dropped
        }
        const std::string_view name = Name(oldText, oldParams[i]);
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

    std::unordered_map<std::string_view, bool> newNameSeen;
    std::vector<ParamOrigin>                    origins;
    origins.reserve(newParams.size());
    for (const SignatureParameter& parameter : newParams) {
        if (parameter.nameStartByte == parameter.nameEndByte) {
            return {.declined = true, .declineReason = "new parameter list has an unnamed parameter"};
        }
        const std::string_view name = Name(newText, parameter);
        if (newNameSeen.contains(name)) {
            return {.declined = true, .declineReason = "new parameter list uses the name \"" + std::string(name) + "\" more than once"};
        }
        newNameSeen[name] = true;

        const auto found = oldIndexByName.find(name);
        if (found != oldIndexByName.end()) {
            origins.push_back(ParamOrigin{.kind = ParamOriginKind::Kept, .oldIndex = found->second});
            continue;
        }
        if (!parameter.hasDefaultValue) {
            return {.declined = true,
                   .declineReason = "new parameter \"" + std::string(name) + "\" needs a default value"};
        }
        origins.push_back(ParamOrigin{.kind                = ParamOriginKind::New,
                                      .newDefaultStartByte = parameter.defaultStartByte,
                                      .newDefaultEndByte   = parameter.defaultEndByte});
    }

    return {.declined = false, .origins = std::move(origins)};
}

ArgumentRewrite RewriteArgumentList(std::string_view callText, const std::vector<CallArgument>& oldArgs,
                                    std::string_view newDefaultText, const std::vector<ParamOrigin>& origins) {
    std::string result;
    for (const ParamOrigin& origin : origins) {
        std::string_view piece;
        if (origin.kind == ParamOriginKind::Kept) {
            if (origin.oldIndex >= oldArgs.size()) {
                return {.declined = true,
                       .declineReason = "call site supplies fewer arguments than the old signature has parameters"};
            }
            const CallArgument& argument = oldArgs[origin.oldIndex];
            piece                        = callText.substr(argument.startByte, argument.endByte - argument.startByte);
        }
        else {
            piece = newDefaultText.substr(origin.newDefaultStartByte, origin.newDefaultEndByte - origin.newDefaultStartByte);
        }
        if (!result.empty()) {
            result += ", ";
        }
        result += piece;
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
            if (signature.nameStartByte == signature.nameEndByte || Name(*text, signature) != name) {
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
