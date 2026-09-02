#include "FillColumn.h"

#include <algorithm>
#include <mutex>

namespace ned::editor {

namespace {

    std::mutex& ColumnMutex() {
        static std::mutex mutex;
        return mutex;
    }

    int& ColumnStorage() {
        static int column = 70;
        return column;
    }

} // namespace

void SetFillColumn(int columns) {
    const std::lock_guard<std::mutex> lock(ColumnMutex());
    ColumnStorage() = std::max(1, columns); // non-positive would collapse WrapWords to one-word-per-line nonsensically
}

int FillColumn() {
    const std::lock_guard<std::mutex> lock(ColumnMutex());
    return ColumnStorage();
}

} // namespace ned::editor
