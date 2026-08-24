#include "TestResult.h"

#include <string_view>

namespace ned::editor::testrun {

namespace {

    // "test_param[2]" -> "test_param": a parameterized instance aggregates
    // onto its one discovered definition.
    std::string_view StripParamSuffix(std::string_view name) {
        if (name.ends_with(']')) {
            const std::size_t open = name.rfind('[');
            if (open != std::string_view::npos && open > 0) {
                return name.substr(0, open);
            }
        }
        return name;
    }

    std::string_view TrailingSegment(std::string_view name) {
        const std::size_t lastSep    = name.find_last_of("./#");
        std::size_t       start      = lastSep == std::string_view::npos ? 0 : lastSep + 1;
        const std::size_t lastColons = name.rfind("::");
        if (lastColons != std::string_view::npos && lastColons + 2 > start) {
            start = lastColons + 2;
        }
        return name.substr(start);
    }

} // namespace

bool MatchesTestName(std::string_view markerName, std::string_view resultName) {
    if (markerName.empty() || resultName.empty()) {
        return false;
    }
    if (markerName == resultName) {
        return true;
    }
    const std::string_view stripped = StripParamSuffix(resultName);
    if (markerName == stripped) {
        return true;
    }
    // "TestSub/child_fail" aggregates onto a discovered "TestSub" (go
    // subtests; any framework reporting children under a parent's name).
    if (stripped.size() > markerName.size() && stripped.starts_with(markerName) && stripped[markerName.size()] == '/') {
        return true;
    }
    // "Tests\FooTest::testBar" / "module.Class::test_x" / "Suite.Name"
    // against a bare "testBar"/"test_x"/"Name" marker -- the framework
    // qualifies, the discovered definition doesn't.
    return TrailingSegment(stripped) == markerName;
}

} // namespace ned::editor::testrun
