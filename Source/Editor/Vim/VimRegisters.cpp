#include "VimRegisters.h"

#include "Editor/Clipboard.h"

namespace ned::editor::vim {

namespace {

    constexpr char32_t kUnnamed = U'"';

    bool IsSmallDelete(const RegisterEntry& entry) {
        return entry.kind == RegisterKind::Char && entry.pieces.size() == 1;
    }

} // namespace

std::string RegisterEntry::Joined() const {
    if (kind == RegisterKind::Char) {
        return pieces.empty() ? std::string() : pieces.front();
    }
    std::string joined;
    for (const std::string& piece : pieces) {
        joined += piece;
        joined += '\n';
    }
    return joined;
}

void VimRegisters::SetUnnamed(const RegisterEntry& entry) {
    registers_[kUnnamed] = entry;
}

void VimRegisters::RouteNamed(char32_t name, RegisterEntry entry) {
    const bool     isUppercase = name >= U'A' && name <= U'Z';
    const char32_t lower       = isUppercase ? name - U'A' + U'a' : name;

    if (isUppercase) {
        auto it = registers_.find(lower);
        if (it != registers_.end() && it->second.kind == entry.kind) {
            // Appending to a Char register concatenates onto its single piece; Line/Block
            // registers just grow the piece list (each piece is already a whole row).
            if (entry.kind == RegisterKind::Char) {
                it->second.pieces.front() += entry.pieces.empty() ? std::string() : entry.pieces.front();
            }
            else {
                it->second.pieces.insert(it->second.pieces.end(), entry.pieces.begin(), entry.pieces.end());
            }
            SetUnnamed(it->second);
            return;
        }
        // No existing same-kind register to append onto -- behaves like a plain write.
        registers_[lower] = entry;
        SetUnnamed(entry);
        return;
    }

    registers_[lower] = entry;
    SetUnnamed(entry);
}

void VimRegisters::RouteUnnamed(RegisterEntry entry, bool isDelete) {
    if (!isDelete) {
        registers_[U'0'] = entry;
        SetUnnamed(entry);
        return;
    }
    if (IsSmallDelete(entry)) {
        registers_[U'-'] = entry;
        SetUnnamed(entry);
        return;
    }
    // Shift the numbered delete ring: "1 -> "2 -> ... -> "9 (oldest dropped), new
    // content into "1.
    for (char32_t digit = U'9'; digit > U'1'; --digit) {
        auto it = registers_.find(digit - 1);
        if (it != registers_.end()) {
            registers_[digit] = it->second;
        }
    }
    registers_[U'1'] = entry;
    SetUnnamed(entry);
}

void VimRegisters::Store(char32_t name, RegisterEntry entry, bool isDelete) {
    if (name == U'_') {
        return; // blackhole: discarded entirely, unnamed untouched
    }
    if (name == U'+' || name == U'*') {
        CopyToSystemClipboard(entry.Joined());
        SetUnnamed(entry);
        return;
    }
    if (name == 0) {
        RouteUnnamed(std::move(entry), isDelete);
        return;
    }
    RouteNamed(name, std::move(entry));
}

void VimRegisters::SetRaw(char32_t name, RegisterEntry entry) {
    registers_[name] = std::move(entry);
}

std::optional<RegisterEntry> VimRegisters::Get(char32_t name) const {
    if (name == U'+' || name == U'*') {
        const std::optional<std::string> clip = PasteFromSystemClipboard();
        if (!clip) {
            return std::nullopt;
        }
        return RegisterEntry{{*clip}, RegisterKind::Char};
    }
    const char32_t key = name == 0 ? kUnnamed : (name >= U'A' && name <= U'Z' ? name - U'A' + U'a' : name);
    const auto     it  = registers_.find(key);
    if (it == registers_.end()) {
        return std::nullopt;
    }
    return it->second;
}

} // namespace ned::editor::vim
