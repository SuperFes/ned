//
// Turns file type icons off for one test's scope, for assertions on exact
// columns that predate a glyph before each name.
//

#ifndef NED_TESTS_FILEICONSOFF_H
#define NED_TESTS_FILEICONSOFF_H

#include "Editor/FileIcons.h"

struct FileIconsOff {
    FileIconsOff() {
        ned::editor::SetFileTypeIconsEnabled(false);
    }
    ~FileIconsOff() {
        ned::editor::SetFileTypeIconsEnabled(true);
    }
    FileIconsOff(const FileIconsOff&)            = delete;
    FileIconsOff& operator=(const FileIconsOff&) = delete;
};

#endif // NED_TESTS_FILEICONSOFF_H
