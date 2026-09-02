//
// Inline diff preview (VCS side panel follow-up): a read-only, non-focusable
// widget showing one file's diff hunks with a click-to-stage/unstage
// affordance per hunk -- VcsPanel's own selection drives what's displayed
// (see VcsPanel::SetOnSelectionChanged), this widget never reads VcsRunner/
// VcsProvider itself. Docked as an OverlayHost bottom drawer in main.cpp,
// TerminalPanel/AcpPanel's own geometry -- the natural fit for a
// *contextual* preview (shown/hidden by VcsPanel's selection moving on/off
// a file row, no separate toggle keybinding), not a permanent
// WindowManager split.
//
// Deliberately non-focusable, mirroring ListPopup's own non-focusable mode
// (Widget's own default Focusable()==false, not overridden here): VcsPanel
// keeps keyboard focus for file navigation throughout, this widget is a
// pure renderer plus a mouse-only click target on each hunk's own
// [stage]/[unstage] affordance -- no second focus-holding widget to
// coordinate with VcsPanel's own keyboard handling.
//

#ifndef NED_UI_VCSDIFFPREVIEW_H
#define NED_UI_VCSDIFFPREVIEW_H

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "Editor/Vcs/DiffPatch.h"
#include "Theme.h"
#include "Widget.h"

namespace ned::ui {

struct VcsDiffPreviewModel {
    std::filesystem::path                    path;
    bool                                      staged; // which side's diff this is -- see SetModel's own doc comment
    std::vector<editor::vcs::DiffHunkText>    hunks;
};

class VcsDiffPreview : public Widget {
  public:
    explicit VcsDiffPreview(const Theme& theme);

    // std::nullopt hides the content (the caller still owns actually
    // showing/hiding the overlay via OverlayHost -- this only controls what
    // gets painted while it's visible). staged=false is the worktree diff
    // (DiffArgv) -- a hunk here can be *staged*; staged=true is the
    // index-vs-HEAD diff (StagedDiffArgv) -- a hunk here can be *unstaged*.
    // Matches VcsRunner::RequestFileDiffText's own staged parameter.
    void SetModel(std::optional<VcsDiffPreviewModel> model);

    // Fired when a hunk's own [stage]/[unstage] affordance is clicked --
    // newStart is the hunk's own new-side start line, ExtractHunkPatch's
    // own 1-indexed "targetLine" convention (VcsRunner::RequestHunkApply
    // re-derives everything else from path+targetLine+stage). stage is the
    // opposite of the currently-displayed model's own `staged` (staging a
    // hunk out of the worktree diff, or unstaging one out of the staged
    // diff). Unset (the default) is a safe no-op, matching every other
    // Set* hook in this codebase.
    void SetOnHunkStageToggle(std::function<void(const std::filesystem::path&, std::size_t newStart, bool stage)> handler);

    void Paint(Canvas c) override;
    bool OnEvent(const Event& event) override;

  private:
    const Theme&                        theme_;
    std::optional<VcsDiffPreviewModel>  model_;
    std::function<void(const std::filesystem::path&, std::size_t, bool)> onHunkStageToggle_;

    int scrollOffset_ = 0;

    struct Row {
        bool        isHeader;
        std::size_t hunkIndex; // valid when isHeader
        std::string text;      // header: the raw "@@ ... @@" line; body: one content line
    };
    [[nodiscard]] std::vector<Row> BuildRows() const;

    // Width of the "[stage] "/"[unstage] " affordance prefix a header row
    // starts with -- shared by Paint() (renders it) and OnEvent() (hit-
    // tests a click against it), always the same fixed value for a given
    // model_->staged so a click's exact column math never needs to inspect
    // the label text itself.
    [[nodiscard]] int AffordanceWidth() const;
};

} // namespace ned::ui

#endif // NED_UI_VCSDIFFPREVIEW_H
