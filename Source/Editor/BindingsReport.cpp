#include "BindingsReport.h"

#include <algorithm>
#include <set>
#include <sstream>

#include "Fill.h"
#include "Key.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"

namespace ned::editor {

namespace {

    // Total width the report wraps to. Not FillColumn(): that is the user's
    // preference for prose they are writing, and this is a generated table
    // whose column layout only reads correctly at a fixed width.
    constexpr std::size_t kReportWidth = 78;
    constexpr std::size_t kRowIndent   = 2;

    std::vector<std::string> SplitWords(const std::string& text) {
        std::vector<std::string> words;
        std::istringstream       in(text);
        std::string              word;
        while (in >> word) {
            words.push_back(word);
        }
        return words;
    }

    void AppendDocstring(std::ostringstream& out, const std::string& docstring, std::size_t indent) {
        const std::vector<std::string> words = SplitWords(docstring);
        if (words.empty()) {
            return;
        }
        const std::size_t width = indent < kReportWidth ? kReportWidth - indent : 1;
        for (const std::string& line : WrapWords(words, width)) {
            out << std::string(indent, ' ') << line << "\n";
        }
    }

    std::string Plural(std::size_t count, const std::string& noun) {
        return std::to_string(count) + " " + noun + (count == 1 ? "" : "s");
    }

    struct Row {
        std::string key;
        std::string commandName;
        // An unmodified single printable character ("a", "$", SPC), the
        // only shape a range row can span.
        bool     plainCharacter = false;
        char32_t codepoint      = 0;
    };

    bool IsPlainCharacter(const std::vector<KeyChord>& sequence) {
        if (sequence.size() != 1) {
            return false;
        }
        const KeyChord& chord = sequence.front();
        return !chord.Control && !chord.Meta && chord.Special == SpecialKey::None && chord.Codepoint >= U' ';
    }

    // Ordered so the printable block is contiguous and can collapse: plain
    // characters first by codepoint, then everything else by its own
    // notation, which groups each prefix's children under it ("C-c ,"
    // through "C-c x t" land together).
    bool RowLess(const Row& left, const Row& right) {
        if (left.plainCharacter != right.plainCharacter) {
            return left.plainCharacter;
        }
        if (left.plainCharacter) {
            return left.codepoint < right.codepoint;
        }
        return left.key < right.key;
    }

    // Emacs' own "SPC .. ~  self-insert-command": every printable character
    // bound to one command is one row, not ninety-five. Generic rather than
    // self-insert-specific -- any run of adjacent plain characters sharing a
    // command collapses, and a run of two stays two rows, where the range
    // notation would cost more than it saves.
    std::vector<std::pair<std::string, std::string>> CollapseCharacterRuns(const std::vector<Row>& rows) {
        std::vector<std::pair<std::string, std::string>> collapsed;
        for (std::size_t start = 0; start < rows.size();) {
            std::size_t end = start + 1;
            while (rows[start].plainCharacter && end < rows.size() && rows[end].plainCharacter &&
                   rows[end].commandName == rows[start].commandName && rows[end].codepoint == rows[end - 1].codepoint + 1) {
                ++end;
            }
            if (end - start >= 3) {
                collapsed.emplace_back(rows[start].key + " .. " + rows[end - 1].key, rows[start].commandName);
            }
            else {
                for (std::size_t index = start; index < end; ++index) {
                    collapsed.emplace_back(rows[index].key, rows[index].commandName);
                }
            }
            start = end;
        }
        return collapsed;
    }

    // Wide enough for the longest chord in this section, so a section of
    // single chords doesn't inherit a gutter sized for "C-c C-M-r".
    std::size_t KeyColumnWidth(const std::vector<std::pair<std::string, std::string>>& rows) {
        std::size_t width = 0;
        for (const auto& [key, command] : rows) {
            width = std::max(width, key.size());
        }
        return width + 2;
    }

    void AppendSection(std::ostringstream& out, const std::string& heading, std::size_t bindingCount,
                       const std::vector<std::pair<std::string, std::string>>& rows, const CommandRegistry& registry) {
        if (rows.empty()) {
            return;
        }
        out << "\n"
            << heading << " -- " << Plural(bindingCount, "binding") << "\n\n";
        const std::size_t keyWidth = KeyColumnWidth(rows);
        for (const auto& [key, command] : rows) {
            out << std::string(kRowIndent, ' ') << key << std::string(keyWidth - key.size(), ' ') << command << "\n";
            const Command* registered = registry.Find(command);
            AppendDocstring(out, registered ? registered->Docstring() : "(no such command -- bound to a name nothing registers)",
                            kRowIndent + keyWidth);
        }
    }

} // namespace

std::string BindingsBufferName() {
    return "*bindings*";
}

std::string RenderBindingsReport(const KeymapStack& keymaps, const CommandRegistry& registry, const std::string& majorModeName) {
    const std::vector<Keymap::Binding> reachable = keymaps.AllBindings();

    std::ostringstream out;
    out << "Key bindings -- " << Plural(reachable.size(), "reachable binding") << ", major mode: "
        << (majorModeName.empty() ? "none" : majorModeName) << "\n";

    std::set<std::string> boundCommands;
    for (std::size_t layer = 0; layer < keymaps.LayerCount(); ++layer) {
        std::vector<Row> rows;
        for (const Keymap::Binding& binding : reachable) {
            if (binding.layer == layer) {
                rows.push_back(Row{.key            = FormatKeySequence(binding.sequence),
                                   .commandName    = binding.commandName,
                                   .plainCharacter = IsPlainCharacter(binding.sequence),
                                   .codepoint      = binding.sequence.empty() ? 0 : binding.sequence.front().Codepoint});
                boundCommands.insert(binding.commandName);
            }
        }
        std::sort(rows.begin(), rows.end(), RowLess);
        // The heading counts real bindings, not rows -- a collapsed range
        // is one row standing for many.
        AppendSection(out, keymaps.LayerName(layer), rows.size(), CollapseCharacterRuns(rows), registry);
    }

    const std::vector<KeymapStack::ShadowedBinding> shadowed = keymaps.ShadowedBindings();
    if (!shadowed.empty()) {
        out << "\nShadowed -- " << Plural(shadowed.size(), "binding") << " that can never fire\n\n";
        std::vector<std::pair<std::string, std::string>> rows;
        for (const KeymapStack::ShadowedBinding& entry : shadowed) {
            rows.emplace_back(FormatKeySequence(entry.binding.sequence),
                              entry.binding.commandName + " -- " + keymaps.LayerName(entry.binding.layer) + " layer, but " +
                                  entry.shadowedBy + " fires instead");
        }
        std::sort(rows.begin(), rows.end());
        const std::size_t keyWidth = KeyColumnWidth(rows);
        for (const auto& [key, detail] : rows) {
            out << std::string(kRowIndent, ' ') << key << std::string(keyWidth - key.size(), ' ') << detail << "\n";
        }
    }

    std::vector<std::string> unbound;
    for (const std::string& name : registry.Names()) {
        if (!boundCommands.contains(name)) {
            unbound.push_back(name);
        }
    }
    if (!unbound.empty()) {
        out << "\nUnbound -- " << Plural(unbound.size(), "command") << " reachable only from M-x\n\n";
        for (const std::string& name : unbound) {
            out << std::string(kRowIndent, ' ') << name << "\n";
        }
    }

    return out.str();
}

text::Buffer& RebuildBindingsBuffer(text::BufferList& bufferList, const KeymapStack& keymaps, const CommandRegistry& registry,
                                    const std::string& majorModeName) {
    text::Buffer* buffer = bufferList.Find(BindingsBufferName());
    if (!buffer) {
        buffer = &bufferList.CreateBuffer(BindingsBufferName());
        buffer->SetReadOnly(true); // before the first append -- AppendWhileReadOnly's precondition
    }

    buffer->SetReadOnly(false);
    buffer->BeginUndoGroup();
    if (buffer->Size() > 0) {
        buffer->DeleteRange(0, buffer->Size());
    }
    buffer->InsertAtPoint(RenderBindingsReport(keymaps, registry, majorModeName));
    buffer->SetPoint(0);
    buffer->EndUndoGroup();
    buffer->SetReadOnly(true);
    return *buffer;
}

} // namespace ned::editor
