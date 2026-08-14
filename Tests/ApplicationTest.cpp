#include <catch2/catch_test_macros.hpp>

#include "Application.h"

TEST_CASE("Application title can be set and retrieved", "[Application]") {
    Ned::Application::SetTitle("Ned Test Title");

    REQUIRE(Ned::Application::GetTitle() == "Ned Test Title");
}
