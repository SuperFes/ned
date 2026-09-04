#include "MassifReportBuffer.h"

#include <vector>

#include "Sparkline.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"

namespace ned::editor {

namespace {

    std::string PadRight(std::string text, std::size_t width) {
        if (text.size() < width) {
            text.append(width - text.size(), ' ');
        }
        return text;
    }

    std::string PadLeft(std::string text, std::size_t width) {
        if (text.size() < width) {
            text.insert(0, width - text.size(), ' ');
        }
        return text;
    }

    std::string FormatSnapshotLine(const MassifSnapshot& snapshot) {
        std::string line = "[" + PadLeft(std::to_string(snapshot.index), 4) + "]";
        line += " time=" + PadRight(std::to_string(snapshot.time), 12);
        line += " heap=" + PadRight(std::to_string(snapshot.heapBytes) + " B", 14);
        line += " extra=" + PadRight(std::to_string(snapshot.heapExtraBytes) + " B", 12);
        line += " stacks=" + PadRight(std::to_string(snapshot.stacksBytes) + " B", 12);
        line += " total=" + std::to_string(snapshot.TotalBytes()) + " B";
        if (snapshot.isPeak) {
            line += "  [peak]";
        }
        else if (snapshot.isDetailed) {
            line += "  [detailed]";
        }
        return line;
    }

} // namespace

std::string MassifReportBufferName() {
    return "*massif report*";
}

text::Buffer& RebuildMassifReportBuffer(text::BufferList& bufferList, const MassifProfile& profile,
                                         std::string_view sourcePath) {
    text::Buffer* buffer = bufferList.Find(MassifReportBufferName());
    if (!buffer) {
        buffer = &bufferList.CreateBuffer(MassifReportBufferName());
        buffer->SetReadOnly(true); // before the first append -- AppendWhileReadOnly's precondition
    }

    buffer->SetReadOnly(false);
    buffer->BeginUndoGroup();
    if (buffer->Size() > 0) {
        buffer->DeleteRange(0, buffer->Size());
    }

    const auto appendLine = [&](const std::string& text) { buffer->InsertAtPoint(text + "\n"); };

    appendLine("Massif profile: " + std::string(sourcePath));
    if (!profile.cmd.empty()) {
        appendLine("Command: " + profile.cmd);
    }
    if (!profile.desc.empty()) {
        appendLine("Options: " + profile.desc);
    }
    if (!profile.timeUnit.empty()) {
        appendLine("Time unit: " + profile.timeUnit);
    }
    appendLine("Snapshots: " + std::to_string(profile.snapshots.size()));

    const MassifSnapshot* peak = nullptr;
    std::vector<double>   heapSeries;
    std::vector<double>   totalSeries;
    heapSeries.reserve(profile.snapshots.size());
    totalSeries.reserve(profile.snapshots.size());
    for (const MassifSnapshot& snapshot : profile.snapshots) {
        heapSeries.push_back(static_cast<double>(snapshot.heapBytes));
        totalSeries.push_back(static_cast<double>(snapshot.TotalBytes()));
        if (snapshot.isPeak) {
            peak = &snapshot;
        }
    }
    if (peak != nullptr) {
        appendLine("Peak: " + std::to_string(peak->TotalBytes()) + " B (snapshot " + std::to_string(peak->index) +
                    ", time " + std::to_string(peak->time) + ")");
    }
    appendLine("");

    appendLine("Heap usage (mem_heap_B) over time:");
    appendLine(BuildBlockSparkline(heapSeries));
    appendLine("");
    appendLine("Total usage (heap + extra + stacks) over time:");
    appendLine(BuildBlockSparkline(totalSeries));
    appendLine("");

    for (const MassifSnapshot& snapshot : profile.snapshots) {
        appendLine(FormatSnapshotLine(snapshot));
    }
    if (profile.snapshots.empty()) {
        appendLine("(no snapshots found)");
    }

    buffer->SetPoint(0);
    buffer->EndUndoGroup();
    buffer->SetReadOnly(true);
    return *buffer;
}

} // namespace ned::editor
