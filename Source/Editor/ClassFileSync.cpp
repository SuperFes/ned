#include "ClassFileSync.h"

#include <algorithm>
#include <cctype>

namespace ned::editor::classfile {

namespace {

    // A marker that can enclose a candidate. Namespace is the sole exception
    // -- see TopLevelTypes' own doc comment for why both of its spellings
    // (wrapping and non-wrapping) need it skipped.
    bool CanEnclose(SymbolKind kind) {
        return kind != SymbolKind::Namespace;
    }

    bool StrictlyContains(const SymbolMarker& outer, const SymbolMarker& inner) {
        if (outer.startByte == inner.startByte && outer.endByte == inner.endByte) {
            return false; // an exact duplicate range is not its own container
        }
        return outer.startByte <= inner.startByte && inner.endByte <= outer.endByte;
    }

} // namespace

FileNameParts SplitFileName(std::string_view filename) {
    // From index 1, never 0: a dotfile's leading dot belongs to its name.
    const std::size_t dot = filename.empty() ? std::string_view::npos : filename.find('.', 1);
    if (dot == std::string_view::npos) {
        return FileNameParts{std::string(filename), std::string()};
    }
    return FileNameParts{std::string(filename.substr(0, dot)), std::string(filename.substr(dot))};
}

std::string FileNameForType(std::string_view filename, std::string_view typeName) {
    if (typeName.empty() || typeName.find('/') != std::string_view::npos ||
        typeName.find('\\') != std::string_view::npos || typeName == "." || typeName == "..") {
        return std::string();
    }
    return std::string(typeName) + SplitFileName(filename).suffix;
}

std::vector<TypeMatch> TopLevelTypes(std::span<const SymbolMarker> markers) {
    std::vector<TypeMatch> result;
    for (std::size_t i = 0; i < markers.size(); ++i) {
        if (markers[i].kind != SymbolKind::TypeLike || markers[i].name.empty()) {
            continue;
        }
        bool enclosed = false;
        for (std::size_t j = 0; j < markers.size() && !enclosed; ++j) {
            if (j == i || !CanEnclose(markers[j].kind)) {
                continue;
            }
            enclosed = StrictlyContains(markers[j], markers[i]);
        }
        if (!enclosed) {
            result.push_back(
                TypeMatch{markers[i].name, markers[i].startByte, markers[i].endByte, markers[i].nameStartByte});
        }
    }
    // Mode::symbolKind's own contract is start-byte order, but this is the
    // one place where "outermost wins" is a promise rather than a side
    // effect, so it is stated rather than inherited.
    std::stable_sort(result.begin(), result.end(),
                     [](const TypeMatch& a, const TypeMatch& b) { return a.startByte < b.startByte; });
    return result;
}

Resolution SelectType(std::span<const SymbolMarker> markers, Strictness strictness) {
    const std::vector<TypeMatch> candidates = TopLevelTypes(markers);

    Resolution resolution;
    resolution.candidateCount = candidates.size();
    if (candidates.empty()) {
        resolution.decline = Decline::NoTypes;
        return resolution;
    }
    if (candidates.size() > 1 && strictness == Strictness::Strict) {
        resolution.decline = Decline::SeveralTypes;
        return resolution;
    }
    resolution.type    = candidates.front();
    resolution.decline = Decline::None;
    return resolution;
}

bool FileRenameFollowsSymbolRename(const Resolution& resolution, std::string_view filename,
                                   std::string_view renamedFrom, std::string_view renamedTo) {
    if (!resolution.type || resolution.candidateCount != 1) {
        return false;
    }
    if (renamedFrom.empty() || renamedTo.empty() || renamedFrom == renamedTo) {
        return false;
    }
    // The type just renamed has to BE the file's one type -- a rename of some
    // unrelated symbol in the same file must not drag the filename along.
    if (resolution.type->name != renamedTo) {
        return false;
    }
    // ...and the file has to have been named after it beforehand. This is the
    // evidence-not-convention rule: without it, a file deliberately named
    // something other than its type would be pestered on every rename.
    return SplitFileName(filename).stem == renamedFrom;
}

bool TypeRenameFollowsFileMove(const Resolution& resolution, std::string_view previousFilename,
                               std::string_view newFilename) {
    if (!resolution.type || resolution.candidateCount != 1) {
        return false;
    }
    const FileNameParts previous = SplitFileName(previousFilename);
    const FileNameParts current  = SplitFileName(newFilename);
    if (previous.stem == current.stem || current.stem.empty()) {
        return false; // a pure directory move renames nothing
    }
    if (resolution.type->name != previous.stem) {
        return false; // the file was not named after this type to begin with
    }
    // A move to a name no language would accept as a type ("my-component.ts",
    // "02_migration.php") is a real and ordinary thing to do; it just isn't a
    // type rename, so it is declined rather than proposed.
    return LooksLikeTypeName(current.stem);
}

bool LooksLikeTypeName(std::string_view text) {
    if (text.empty()) {
        return false;
    }
    const auto isNameByte = [](unsigned char c) {
        return std::isalnum(c) != 0 || c == '_' || c >= 0x80;
    };
    if (std::isdigit(static_cast<unsigned char>(text.front())) != 0) {
        return false;
    }
    return std::all_of(text.begin(), text.end(),
                       [&](char c) { return isNameByte(static_cast<unsigned char>(c)); });
}

} // namespace ned::editor::classfile
