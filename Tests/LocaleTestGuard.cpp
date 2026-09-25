// Gives the whole ned_tests binary a UTF-8 LC_CTYPE before any TEST_CASE
// runs, the way the editor has one once main() calls setlocale: text width
// (Text/DisplayWidth.h) is measured by Notcurses, which can only measure
// non-ASCII text under a UTF-8 locale. LC_CTYPE only, so number formatting
// stays in the "C" locale every other test assumes.

#include <clocale>

namespace {

struct Utf8CtypeForTests {
    Utf8CtypeForTests() {
        if (std::setlocale(LC_CTYPE, "C.UTF-8") == nullptr) {
            std::setlocale(LC_CTYPE, "en_US.UTF-8");
        }
    }
};

const Utf8CtypeForTests kUtf8CtypeForTests;

} // namespace
