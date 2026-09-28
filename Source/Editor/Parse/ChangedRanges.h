#pragma once

#include <vector>

#include "Editor/Parse/Tree.h"

// The structural diff between two generations of a tree -- the port of
// tree-sitter's get_changed_ranges.c. A range is reported wherever the
// nesting of visible nodes (their types, extents, or the external-scanner
// state they were lexed under) may differ between the two trees; text that
// changed inside an otherwise identical token is not a structural change and
// is not reported. Ranges are sorted, non-overlapping, and in the NEW tree's
// coordinates.
//
// `oldEdited` must be the previous generation with the edit(s) already
// applied (GreenTree::WithEdit) -- the hasChanges marks and shifted layout
// that leaves behind are what the walk trusts to skip unchanged subtrees --
// and `newTree` the tree parsed from the edited text.

namespace ned::editor::parse {

struct ChangedRange {
    Length start;
    Length end;
};

std::vector<ChangedRange> ChangedRanges(const GreenTree& oldEdited, const GreenTree& newTree);

} // namespace ned::editor::parse
