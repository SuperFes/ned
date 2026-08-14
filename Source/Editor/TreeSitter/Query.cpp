#include "Query.h"

#include <stdexcept>
#include <utility>

namespace ned::editor::treesitter {

namespace {

    std::string_view QueryErrorName(TSQueryError error) {
        switch (error) {
            case TSQueryErrorNone:
                return "none";
            case TSQueryErrorSyntax:
                return "syntax";
            case TSQueryErrorNodeType:
                return "unknown node type";
            case TSQueryErrorField:
                return "unknown field name";
            case TSQueryErrorCapture:
                return "unknown capture name";
            case TSQueryErrorStructure:
                return "impossible pattern structure";
            case TSQueryErrorLanguage:
                return "language version mismatch";
        }
        return "unknown";
    }

} // namespace

Query::Query(const Language& language, std::string_view source) {
    uint32_t     errorOffset = 0;
    TSQueryError errorType   = TSQueryErrorNone;

    query_ = ts_query_new(language.Raw(), source.data(), static_cast<uint32_t>(source.size()), &errorOffset,
                          &errorType);

    if (query_ == nullptr) {
        throw std::runtime_error("ned: tree-sitter query error (" + std::string(QueryErrorName(errorType)) +
                                 ") at byte offset " + std::to_string(errorOffset));
    }
}

Query::~Query() {
    ts_query_delete(query_); // ts_query_delete(nullptr) is a documented no-op
}

Query::Query(Query&& other) noexcept : query_(std::exchange(other.query_, nullptr)) {
}

Query& Query::operator=(Query&& other) noexcept {
    if (this != &other) {
        ts_query_delete(query_);
        query_ = std::exchange(other.query_, nullptr);
    }
    return *this;
}

std::vector<QueryCapture> Query::Captures(const Node& root) const {
    std::vector<QueryCapture> captures;

    TSQueryCursor* cursor = ts_query_cursor_new();
    ts_query_cursor_exec(cursor, query_, root.Raw());

    TSQueryMatch match;
    uint32_t     captureIndex = 0;
    while (ts_query_cursor_next_capture(cursor, &match, &captureIndex)) {
        const TSQueryCapture& capture = match.captures[captureIndex];

        uint32_t          nameLength = 0;
        const char* const name       = ts_query_capture_name_for_id(query_, capture.index, &nameLength);

        captures.push_back(QueryCapture{
            .name      = std::string(name, nameLength),
            .startByte = ts_node_start_byte(capture.node),
            .endByte   = ts_node_end_byte(capture.node),
        });
    }

    ts_query_cursor_delete(cursor);
    return captures;
}

} // namespace ned::editor::treesitter
