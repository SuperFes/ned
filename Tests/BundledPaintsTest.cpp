#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include "Editor/ThemeSetting.h"
#include "Janet/EditorBindings.h"
#include "Janet/Environment.h"
#include "Janet/PluginLoader.h"
#include "JanetTestSupport.h"
#include "UI/PaintParse.h"
#include "UI/Theme.h"
#include "UI/ThemePaints.h"

using ned::ui::DarkTheme;
using ned::ui::LightTheme;
using ned::ui::PaintKind;
using ned::ui::ParsePaint;
using ned::ui::Theme;

namespace {

// Both the deferred override stores and the parsed registries are
// process-wide (ThemeSetting.h / ThemePaints.h), so this cleans up after
// itself the way every other registry test here does.
struct BundledPaintGuard {
    BundledPaintGuard() {
        ned::editor::ClearNamedPaintOverrides();
        ned::editor::ClearSurfacePaintOverrides();
        ned::ui::ClearNamedPaints();
        ned::ui::ClearSurfaceOverrides();
    }
    ~BundledPaintGuard() {
        ned::editor::ClearNamedPaintOverrides();
        ned::editor::ClearSurfacePaintOverrides();
        ned::ui::ClearNamedPaints();
        ned::ui::ClearSurfaceOverrides();
    }
};

std::vector<std::pair<std::string, std::string>> LoadBundledPaints() {
    ned::janet::Environment& env = ned_tests::TestEnvironment();
    ned::janet::InstallEditorBindings(env);
    ned::janet::LoadBundledPlugins(env);
    return ned::editor::NamedPaintOverrides();
}

} // namespace

TEST_CASE("Every bundled preset parses against a real theme", "[BundledPaints]") {
    const BundledPaintGuard guard;
    const auto              presets = LoadBundledPaints();

    REQUIRE_FALSE(presets.empty());

    // Registered in order, so a preset referencing an earlier one resolves
    // exactly the way startup will apply them.
    for (const Theme& theme : {DarkTheme(), LightTheme()}) {
        ned::ui::ClearNamedPaints();
        for (const auto& [name, spec] : presets) {
            INFO(name << " = " << spec);
            std::string error;
            const auto  paint = ParsePaint(spec, ned::ui::PaintContextFor(theme), &error);
            INFO(error);
            REQUIRE(paint.has_value());
            ned::ui::RegisterNamedPaint(name, *paint);
        }
    }
}

TEST_CASE("The bundled set covers the documented presets", "[BundledPaints]") {
    const BundledPaintGuard guard;
    const auto              presets = LoadBundledPaints();

    std::vector<std::string> names;
    names.reserve(presets.size());
    for (const auto& [name, spec] : presets) {
        names.push_back(name);
    }
    auto has = [&names](const char* wanted) {
        return std::find(names.begin(), names.end(), wanted) != names.end();
    };

    for (const char* palette : {"lift", "sink", "glass", "edge", "scrim", "focus", "brand", "rule"}) {
        INFO(palette);
        REQUIRE(has(palette));
    }
    for (const char* signature : {"spectrum", "aurora", "sunset", "ember", "ice", "vapor", "ink"}) {
        INFO(signature);
        REQUIRE(has(signature));
    }
    for (const char* pattern : {"grain", "scanlines", "checker", "hatch", "graph", "stipple"}) {
        INFO(pattern);
        REQUIRE(has(pattern));
    }
    for (const char* fade : {"vanish", "ghost"}) {
        INFO(fade);
        REQUIRE(has(fade));
    }
}

TEST_CASE("The Janet array sugar flattens to the one-line form C++ parses", "[BundledPaints]") {
    const BundledPaintGuard  guard;
    ned::janet::Environment& env = ned_tests::TestEnvironment();
    ned::janet::InstallEditorBindings(env);
    ned::janet::LoadBundledPlugins(env);
    ned::editor::ClearNamedPaintOverrides();
    ned::editor::ClearSurfacePaintOverrides();

    env.DoString(R"((ned/gradient "test-flatten" [:y "$bg" 3 "$bg+8"]))");
    const auto paints = ned::editor::NamedPaintOverrides();
    REQUIRE(paints.size() == 1);
    REQUIRE(paints[0].second == "y $bg 3 $bg+8"); // keyword colon stripped, number rendered

    SECTION("a plain string passes through untouched") {
        env.DoString(R"((ned/gradient "test-string" "x #000000 #ffffff"))");
        REQUIRE(ned::editor::NamedPaintOverrides().back().second == "x #000000 #ffffff");
    }

    SECTION("ned/surface sets several parts in one call") {
        env.DoString(R"((ned/surface "popup" :fill [:y "$bg/78" 3 "$bg/52"] :border "$brand"))");
        const auto surfaces = ned::editor::SurfacePaintOverrides();
        REQUIRE(surfaces.size() == 2);
        REQUIRE(surfaces[0].part == "fill");
        REQUIRE(surfaces[0].spec == "y $bg/78 3 $bg/52");
        REQUIRE(surfaces[1].part == "border");
        REQUIRE(surfaces[1].spec == "$brand");
    }
}

TEST_CASE("A preset built on another preset resolves through the registry", "[BundledPaints]") {
    const BundledPaintGuard guard;
    const Theme             theme = DarkTheme();
    for (const auto& [name, spec] : LoadBundledPaints()) {
        if (const auto paint = ParsePaint(spec, ned::ui::PaintContextFor(theme))) {
            ned::ui::RegisterNamedPaint(name, *paint);
        }
    }

    // "$brand" is a two-stop gradient; extending it adds a third stop
    // rather than nesting a paint inside a paint.
    const auto extended = ParsePaint("y $brand #000000", ned::ui::PaintContextFor(theme));
    REQUIRE(extended.has_value());
    REQUIRE(extended->kind == PaintKind::Gradient);
    REQUIRE(extended->stops.size() == 3);
}
