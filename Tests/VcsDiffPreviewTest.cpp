#include <catch2/catch_test_macros.hpp>

#include "TestEvents.h"
#include "UI/VcsDiffPreview.h"

using ned::editor::vcs::DiffHunkText;

namespace {

std::string RowText(ned::ui::Screen& screen, int row, int width) {
    std::string out;
    for (int col = 0; col < width; ++col) {
        out += screen.PixelAt(col, row).character;
    }
    return out;
}

void PlacePreview(ned::ui::VcsDiffPreview& preview, int width, int height) {
    preview.SetBox_(ned::ui::Box{.x_min = 0, .x_max = width - 1, .y_min = 0, .y_max = height - 1});
}

ned::ui::Event MousePress(int x, int y) {
    return ned::ui::test::Mouse(x, y, ned::ui::MouseEvent::Button::Left, ned::ui::MouseEvent::Motion::Pressed);
}

// -U0 output (VcsProvider::DiffArgv/StagedDiffArgv's own convention) --
// body text is just the +/- lines, no context.
std::vector<DiffHunkText> TwoHunks() {
    return {
        DiffHunkText{"file.txt", "@@ -2 +2 @@", "-line2\n+CHANGED2", 2, 1, 2, 1},
        DiffHunkText{"file.txt", "@@ -5 +5 @@", "-line5\n+CHANGED5", 5, 1, 5, 1},
    };
}

} // namespace

TEST_CASE("VcsDiffPreview renders nothing but the title with no model", "[VcsDiffPreview]") {
    ned::ui::Theme        theme = ned::ui::DarkTheme();
    ned::ui::VcsDiffPreview preview(theme);
    PlacePreview(preview, 40, 10);

    ned::ui::Screen screen = ned::ui::Screen(40, 10);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 9});
    preview.Paint(canvas);

    REQUIRE(RowText(screen, 0, 40).find("Diff preview") != std::string::npos);
    REQUIRE(RowText(screen, 1, 40).find("@@") == std::string::npos);
}

TEST_CASE("VcsDiffPreview renders hunk headers with a [stage] affordance and +/- body lines", "[VcsDiffPreview]") {
    ned::ui::Theme        theme = ned::ui::DarkTheme();
    ned::ui::VcsDiffPreview preview(theme);
    PlacePreview(preview, 40, 10);
    preview.SetModel(ned::ui::VcsDiffPreviewModel{"/tmp/file.txt", /*staged=*/false, TwoHunks()});

    ned::ui::Screen screen = ned::ui::Screen(40, 10);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 9});
    preview.Paint(canvas);

    REQUIRE(RowText(screen, 0, 40).find("file.txt (unstaged)") != std::string::npos);
    REQUIRE(RowText(screen, 1, 40).find("[stage] @@ -2 +2 @@") != std::string::npos);
    REQUIRE(RowText(screen, 3, 40).find("+CHANGED2") != std::string::npos);
}

TEST_CASE("Clicking a staged model's hunk shows [unstage] and requests stage=false", "[VcsDiffPreview]") {
    ned::ui::Theme        theme = ned::ui::DarkTheme();
    ned::ui::VcsDiffPreview preview(theme);
    PlacePreview(preview, 40, 10);
    preview.SetModel(ned::ui::VcsDiffPreviewModel{"/tmp/file.txt", /*staged=*/true, TwoHunks()});

    ned::ui::Screen screen = ned::ui::Screen(40, 10);
    ned::ui::Canvas canvas(screen, ned::ui::Box{.x_min = 0, .x_max = 39, .y_min = 0, .y_max = 9});
    preview.Paint(canvas);
    REQUIRE(RowText(screen, 1, 40).find("[unstage] @@ -2 +2 @@") != std::string::npos);

    std::filesystem::path calledPath;
    std::size_t           calledNewStart = 0;
    bool                  calledStage    = true;
    preview.SetOnHunkStageToggle([&](const std::filesystem::path& path, std::size_t newStart, bool stage) {
        calledPath     = path;
        calledNewStart = newStart;
        calledStage    = stage;
    });

    // Row 1 (0-indexed, the first hunk header), column within the
    // "[unstage] " affordance prefix.
    preview.OnEvent(MousePress(2, 1));
    REQUIRE(calledPath == "/tmp/file.txt");
    REQUIRE(calledNewStart == 2);
    REQUIRE_FALSE(calledStage); // staged view's hunk toggles to unstage
}

TEST_CASE("Clicking a body line or past the affordance label does not fire the callback", "[VcsDiffPreview]") {
    ned::ui::Theme        theme = ned::ui::DarkTheme();
    ned::ui::VcsDiffPreview preview(theme);
    PlacePreview(preview, 40, 10);
    preview.SetModel(ned::ui::VcsDiffPreviewModel{"/tmp/file.txt", /*staged=*/false, TwoHunks()});

    bool fired = false;
    preview.SetOnHunkStageToggle([&](const std::filesystem::path&, std::size_t, bool) { fired = true; });

    preview.OnEvent(MousePress(2, 2)); // a body line ("-line2"), not a header
    REQUIRE_FALSE(fired);

    preview.OnEvent(MousePress(20, 1)); // header row, but past the "[stage] " label itself
    REQUIRE_FALSE(fired);
}
