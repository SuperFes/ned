#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <new>

#include "Editor/Parse/RawArray.h"
#include "Editor/Parse/ScannerSupport.h"

// An allocation this size can never succeed, so these exercise the failure
// path for real. ASan reports such a request itself instead of returning
// null, so the checks only run without it.
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define NED_PARSE_ALLOC_UNDER_ASAN 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__)
#define NED_PARSE_ALLOC_UNDER_ASAN 1
#endif

namespace {

using Huge = std::array<char, std::size_t{1} << 20>;

} // namespace

TEST_CASE("A parser array that cannot grow throws instead of losing its contents", "[Parse][MemorySafety]") {
#ifndef NED_PARSE_ALLOC_UNDER_ASAN
    ned::editor::parse::RawArray<Huge> array;
    array.Reserve(1);
    Huge* const before = array.contents;

    REQUIRE_THROWS_AS(array.Reserve(UINT32_MAX), std::bad_alloc);
    REQUIRE(array.contents == before);
    REQUIRE(array.capacity == 1);
    array.Delete();
#endif
}

TEST_CASE("A scanner allocation that cannot be satisfied throws", "[Parse][MemorySafety]") {
#ifndef NED_PARSE_ALLOC_UNDER_ASAN
    using namespace ned::editor::parse::scanner;
    // Stored through a volatile so the compiler cannot drop an allocation
    // whose result is never used, assuming it succeeded.
    void* volatile escaped = nullptr;
    REQUIRE_THROWS_AS(escaped = scanner_malloc(SIZE_MAX / 2), std::bad_alloc);
    REQUIRE_THROWS_AS(escaped = scanner_calloc(SIZE_MAX / 2, 4), std::bad_alloc);
    REQUIRE(escaped == nullptr);
#endif
}
