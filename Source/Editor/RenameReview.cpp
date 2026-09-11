#include "RenameReview.h"

#include <algorithm>
#include <cstdint>

namespace ned::editor::rename {

namespace {

    bool IsIdentifierByte(unsigned char byte, std::string_view name) {
        if ((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9') ||
            byte == '_' || byte >= 0x80) {
            return true;
        }
        // See the header: a character the searched-for name itself uses is
        // part of an identifier for the purposes of this scan, which is what
        // makes foo-bar and valid? whole-word-match in a Lisp without this
        // module carrying a per-language charset.
        return name.find(static_cast<char>(byte)) != std::string_view::npos;
    }

    // Every line's start offset, plus a sentinel past the end so a lookup
    // never needs a bounds special case.
    std::vector<std::size_t> LineStarts(std::string_view text) {
        std::vector<std::size_t> starts{0};
        for (std::size_t i = 0; i < text.size(); ++i) {
            if (text[i] == '\n') {
                starts.push_back(i + 1);
            }
        }
        return starts;
    }

    std::size_t LineForOffset(const std::vector<std::size_t>& lineStarts, std::size_t offset) {
        const auto it = std::upper_bound(lineStarts.begin(), lineStarts.end(), offset);
        return static_cast<std::size_t>(std::distance(lineStarts.begin(), it)) - 1;
    }

    std::size_t LineEndOffset(std::string_view text, const std::vector<std::size_t>& lineStarts, std::size_t line) {
        if (line + 1 < lineStarts.size()) {
            return lineStarts[line + 1] - 1; // before the '\n'
        }
        return text.size();
    }

    // The rows of one file, before the cross-file risk grouping.
    struct Row {
        std::size_t            startLine = 0; // 0-indexed
        std::size_t            endLine   = 0;
        std::vector<RenameHit> hits;
    };

    std::string ApplyHits(std::string_view body, std::size_t bodyStart, const std::vector<RenameHit>& hits,
                          const std::string& newName, bool includeRisky) {
        std::string out(body);
        for (auto it = hits.rbegin(); it != hits.rend(); ++it) {
            if (!includeRisky && it->kind != HitKind::Reference) {
                continue;
            }
            if (it->startByte < bodyStart || it->endByte > bodyStart + body.size() || it->endByte < it->startByte) {
                continue; // a hit outside the row it was grouped into: degrade, don't corrupt
            }
            out.replace(it->startByte - bodyStart, it->endByte - it->startByte,
                        it->replacement.empty() ? newName : it->replacement);
        }
        return out;
    }

} // namespace

HitKind KindForSyntaxClass(SyntaxClass syntaxClass) {
    switch (syntaxClass) {
        case SyntaxClass::Comment:
        case SyntaxClass::DocComment:
            return HitKind::Comment;
        case SyntaxClass::String:
        case SyntaxClass::StringEscape:
        // An #include "path" is a string as far as a rename is concerned:
        // rewriting it would break the include, not rename a symbol.
        case SyntaxClass::IncludePath:
            return HitKind::String;
        default:
            return HitKind::Reference;
    }
}

HitKind ClassifyHit(const std::vector<HighlightSpan>& spans, std::size_t startByte) {
    HitKind kind = HitKind::Reference;
    for (const HighlightSpan& span : spans) {
        if (span.startByte <= startByte && startByte < span.endByte) {
            kind = KindForSyntaxClass(span.syntaxClass); // later spans win (Mode.h)
        }
    }
    return kind;
}

namespace {

    // The whole-word scan both public entry points below share. A hit is
    // kept when it is on an identifier boundary, is not already covered by
    // the caller's own edit set, and -- unless referencesToo -- is not a
    // plain code reference.
    std::vector<RenameHit> ScanWholeWord(std::string_view text, const std::vector<HighlightSpan>& spans,
                                         std::string_view name, const std::vector<RenameHit>& covered,
                                         bool referencesToo) {
        std::vector<RenameHit> found;
        if (name.empty() || text.empty()) {
            return found;
        }

        std::size_t coveredIndex = 0;
        std::size_t at           = text.find(name, 0);
        while (at != std::string_view::npos) {
            const std::size_t end = at + name.size();

            const bool leftBoundary = at == 0 || !IsIdentifierByte(static_cast<unsigned char>(text[at - 1]), name);
            const bool rightBoundary =
                end >= text.size() || !IsIdentifierByte(static_cast<unsigned char>(text[end]), name);

            if (leftBoundary && rightBoundary) {
                // covered is sorted and this scan runs left to right, so the
                // cursor only ever moves forward.
                while (coveredIndex < covered.size() && covered[coveredIndex].endByte <= at) {
                    ++coveredIndex;
                }
                const bool alreadyCovered = coveredIndex < covered.size() &&
                                            covered[coveredIndex].startByte < end && at < covered[coveredIndex].endByte;
                if (!alreadyCovered) {
                    const HitKind kind = ClassifyHit(spans, at);
                    if (referencesToo || kind != HitKind::Reference) {
                        found.push_back(RenameHit{at, end, kind});
                    }
                }
            }
            at = text.find(name, at + 1);
        }
        return found;
    }

} // namespace

std::vector<RenameHit> FindWholeWordOccurrences(std::string_view text, const std::vector<HighlightSpan>& spans,
                                                std::string_view name) {
    return ScanWholeWord(text, spans, name, {}, /*referencesToo=*/true);
}

std::vector<RenameHit> FindExtraCandidates(std::string_view text, const std::vector<HighlightSpan>& spans,
                                           std::string_view name, const std::vector<RenameHit>& covered) {
    return ScanWholeWord(text, spans, name, covered, /*referencesToo=*/false);
}

std::vector<ReviewExcerpt> BuildReviewExcerpts(const std::vector<FileRenameHits>& files, const std::string& newName) {
    std::vector<ReviewExcerpt> excerpts;
    std::vector<std::uint8_t>  ranks; // parallel to excerpts, only used for the grouping sort

    for (const FileRenameHits& file : files) {
        if (file.hits.empty()) {
            continue;
        }
        const std::string_view         text       = file.text;
        const std::vector<std::size_t> lineStarts = LineStarts(text);

        std::vector<Row> rows;
        for (const RenameHit& hit : file.hits) {
            if (hit.startByte > text.size() || hit.endByte > text.size() || hit.endByte < hit.startByte) {
                continue; // a stale offset never becomes an edit
            }
            const std::size_t startLine = LineForOffset(lineStarts, hit.startByte);
            const std::size_t endLine   = LineForOffset(lineStarts, hit.endByte == hit.startByte ? hit.endByte : hit.endByte - 1);
            if (!rows.empty() && startLine <= rows.back().endLine) {
                rows.back().endLine = std::max(rows.back().endLine, endLine);
                rows.back().hits.push_back(hit);
                continue;
            }
            rows.push_back(Row{startLine, endLine, {hit}});
        }

        const std::string displayPath = file.displayPath.empty() ? file.file.string() : file.displayPath;
        for (const Row& row : rows) {
            const std::size_t bodyStart = lineStarts[row.startLine];
            const std::size_t bodyEnd   = LineEndOffset(text, lineStarts, row.endLine);
            const std::string body(text.substr(bodyStart, bodyEnd - bodyStart));

            bool hasReference = false;
            bool hasComment   = false;
            bool hasString    = false;
            for (const RenameHit& hit : row.hits) {
                hasReference = hasReference || hit.kind == HitKind::Reference;
                hasComment   = hasComment || hit.kind == HitKind::Comment;
                hasString    = hasString || hit.kind == HitKind::String;
            }

            std::string riskTag;
            if (hasComment && hasString) {
                riskTag = "comment/string";
            }
            else if (hasComment) {
                riskTag = "comment";
            }
            else if (hasString) {
                riskTag = "string";
            }
            std::string tag;
            if (!riskTag.empty()) {
                tag = hasReference ? "  [+" + riskTag + "]" : "  [" + riskTag + "]";
            }

            ReviewExcerpt excerpt;
            excerpt.source.sourcePath      = file.file;
            excerpt.source.sourceStartLine = row.startLine + 1;
            excerpt.source.sourceEndLine   = row.endLine + 1;
            excerpt.source.headerText =
                "▸ " + displayPath + ":" + std::to_string(row.startLine + 1) + tag;
            excerpt.source.bodyText    = body;
            excerpt.source.editable    = true;
            excerpt.referencesOnlyBody = hasReference ? ApplyHits(body, bodyStart, row.hits, newName, /*includeRisky=*/false) : body;
            excerpt.allHitsBody        = (hasComment || hasString) ? ApplyHits(body, bodyStart, row.hits, newName, /*includeRisky=*/true)
                                                                   : excerpt.referencesOnlyBody;
            excerpt.hasReference       = hasReference;
            excerpt.hasRisky           = hasComment || hasString;

            excerpts.push_back(std::move(excerpt));
            ranks.push_back(hasReference ? 0 : (hasComment ? 1 : 2));
        }
    }

    // Stable, so each rank group keeps the caller's file order and ascending
    // lines within a file.
    std::vector<std::size_t> order(excerpts.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        order[i] = i;
    }
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return ranks[a] < ranks[b]; });

    std::vector<ReviewExcerpt> sorted;
    sorted.reserve(excerpts.size());
    for (const std::size_t index : order) {
        sorted.push_back(std::move(excerpts[index]));
    }
    return sorted;
}

} // namespace ned::editor::rename
