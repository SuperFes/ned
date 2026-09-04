//
// Debugging wishlist follow-up (ROADMAP.md's Maybelist, the Valgrind entry):
// "Massif's heap-snapshot-over-time output is a genuine graphing use case
// too -- same sparkline/chart substrate as the array-value graph [...],
// fed by [massif's own snapshot format] instead of a live watch." Parses
// the raw `massif.out.<pid>` file Valgrind's massif tool writes directly
// (not `ms_print`'s rendered ASCII-art report) -- the format is a simple,
// stable, line-oriented `key=value` scheme (Valgrind's own documented
// format, unchanged across releases), so there's no need to shell out to
// `ms_print` and reparse its already-rendered text graph. Same hand-rolled,
// no-library, pinned-against-real-output precedent as
// ValgrindOutputParser.h/SanitizerOutputParser.h/TestOutputParser.h's
// "junit-xml" parser.
//
// A snapshot block looks like:
//
//   #-----------
//   snapshot=3
//   #-----------
//   time=20480
//   mem_heap_B=20000
//   mem_heap_extra_B=480
//   mem_stacks_B=0
//   heap_tree=detailed
//   n1: 20480 (heap allocation functions) malloc/new/new[], --alloc-fns, etc.
//    n1: 20480 0x1091C4: main (a.c:10)
//
// `heap_tree` is `empty` for most snapshots, `detailed` for one sampled
// every `--detailed-freq` snapshots (default 10), and `peak` for exactly
// one snapshot in the whole file -- the run's single global memory peak.
// The indented `nN: ...` allocation-tree lines that follow a non-`empty`
// `heap_tree` are deliberately not parsed here (a real call-tree browser is
// a separably-scoped, bigger feature -- see ROADMAP.md); this parser only
// extracts the per-snapshot scalar summary needed for a heap-usage-over-
// time chart.
//

#ifndef NED_EDITOR_MASSIFOUTPUTPARSER_H
#define NED_EDITOR_MASSIFOUTPUTPARSER_H

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace ned::editor {

struct MassifSnapshot {
    int         index          = -1; // Massif's own `snapshot=N` index, in file order.
    std::size_t time           = 0;  // Unit depends on the profile's timeUnit (bytes/ms/instructions).
    std::size_t heapBytes      = 0;  // mem_heap_B
    std::size_t heapExtraBytes = 0;  // mem_heap_extra_B -- allocator bookkeeping overhead
    std::size_t stacksBytes    = 0;  // mem_stacks_B -- 0 unless the profiled run used --stacks=yes
    bool        isDetailed     = false; // heap_tree was "detailed" or "peak" (a full tree follows in the file)
    bool        isPeak         = false; // heap_tree was "peak" -- the run's single global memory peak

    [[nodiscard]] std::size_t TotalBytes() const { return heapBytes + heapExtraBytes + stacksBytes; }

    [[nodiscard]] bool operator==(const MassifSnapshot&) const = default;
};

struct MassifProfile {
    std::string desc;     // Verbatim `desc:` header line (massif's own invocation options), empty if absent.
    std::string cmd;      // Verbatim `cmd:` header line (the profiled command), empty if absent.
    std::string timeUnit; // "B" (bytes allocated), "ms", or "i" (instructions) -- empty if absent.
    std::vector<MassifSnapshot> snapshots; // In file order, matching massif's own snapshot numbering.
};

// Pure, testable, never throws: scans a raw massif.out file's content for
// `desc:`/`cmd:`/`time_unit:` header lines and `snapshot=`/`time=`/
// `mem_heap_B=`/`mem_heap_extra_B=`/`mem_stacks_B=`/`heap_tree=` snapshot
// fields, returning one MassifSnapshot per snapshot block in file order.
// Output carrying no `snapshot=` line at all (plain text, an unrelated
// file) yields an empty snapshots vector -- "unrecognized input is not an
// error", the same contract every parser in this file's sibling headers
// follows.
[[nodiscard]] MassifProfile ParseMassifOutput(std::string_view output);

} // namespace ned::editor

#endif // NED_EDITOR_MASSIFOUTPUTPARSER_H
