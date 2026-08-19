//
// Hand-rolled box layout (FTXUI -> Notcurses migration, Phase 3) --
// replaces ftxui::Container::Horizontal/Vertical plus the size()/flex()/
// Maybe() decorators main.cpp's and WindowManager's own composition used to
// layer on top of each child individually. Notcurses has no layout system
// of its own at all (see Widget.h's own header comment), so this project
// owns the box math now instead of getting it for free from the TUI
// library, the same "own it directly" shape this migration already took
// for the event loop (EventLoop.h) and terminal-cell painting (Widget.h's
// own Screen/Canvas).
//

#ifndef NED_UI_LAYOUT_H
#define NED_UI_LAYOUT_H

#include <functional>
#include <vector>

#include "Widget.h"

namespace ned::ui {

enum class Axis { Horizontal,
                  Vertical };

// How much space one child reserves along a Container's main axis --
// mirrors size(WIDTH/HEIGHT, EQUAL, n) (Fixed), flex() (Flex), and the
// per-frame ElementDecorator lambda main.cpp's own ProjectSidebar sizing
// used (DynamicFixed) -- all three real call sites this codebase's
// composition roots actually needed, nothing more speculative.
struct SizeSpec {
    enum class Kind { Fixed,
                      DynamicFixed,
                      Flex } kind   = Kind::Flex;
    int                  fixedValue = 0; // Kind::Fixed
    std::function<int()> dynamicSize;    // Kind::DynamicFixed -- read fresh every Layout() call
    int                  flexWeight = 1; // Kind::Flex -- proportional share of whatever's left

    [[nodiscard]] static SizeSpec Fixed(int n) {
        return SizeSpec{.kind = Kind::Fixed, .fixedValue = n};
    }
    [[nodiscard]] static SizeSpec DynamicFixed(std::function<int()> f) {
        return SizeSpec{.kind = Kind::DynamicFixed, .dynamicSize = std::move(f)};
    }
    [[nodiscard]] static SizeSpec Flex(int weight = 1) {
        return SizeSpec{.kind = Kind::Flex, .flexWeight = weight};
    }
};

// A container Widget: lays out child widgets along one axis, recomputing
// every child's Box fresh on every Paint() call -- mirrors FTXUI's own
// "rebuild the whole Element tree, including every size()/flex()
// decorator, fresh every single frame" contract (confirmed during the
// original TermOx -> FTXUI migration, see ROADMAP.md), just implemented
// directly here instead of implicitly via a parallel Element tree. A child
// widget with `widget->active == false` (Widget.h's own field, was FTXUI's
// Maybe(component, bool*) target) is skipped entirely -- zero space
// reserved for it, and neither Paint nor OnEvent ever reaches it while
// inactive -- which is what makes a separate Maybe-equivalent type
// unnecessary here: every real use of Maybe in this codebase (just
// ProjectSidebar) already toggles its own `active` field directly.
//
// OnEvent forwards to every child unconditionally (the same "every leaf
// gets every event, hit-test yourself" contract Widget.h's own header
// comment documents) -- a Container is not itself a hit-testing dispatcher,
// it only decides *whether* a child exists in the tree this frame, exactly
// mirroring what Maybe's own active-flag check already did and what a
// FTXUI Container's own OnEvent already did (broadcast to every child,
// first non-consuming return continuing to the next -- confirmed by
// reading ContainerBase::OnEvent during the original migration).
class Container : public Widget {
  public:
    struct Child {
        Widget*  widget;
        SizeSpec size;
    };

    Container(Axis axis, std::vector<Child> children) : axis_(axis), children_(std::move(children)) {
    }

    // Replaces the full child list -- used by WindowManager's own
    // RebuildComponentTree after a split/close changes the pane tree's
    // shape. Boxes are recomputed from scratch on the very next Paint()
    // call, same as any other frame.
    void SetChildren(std::vector<Child> children) {
        children_ = std::move(children);
    }

    void Paint(Canvas c) override;
    bool OnEvent(const Event& event) override;

  private:
    // Assigns every active child's Box_ from this Container's own current
    // Box_(), splitting the main axis into each Fixed/DynamicFixed child's
    // exact size plus whatever's left divided proportionally among Flex
    // children by weight -- shared by Paint (needs boxes before painting)
    // and OnEvent (relies on whatever the most recent Paint already
    // assigned, per Widget.h's own "boxes are current as of the last
    // Paint" contract; this is only called from Paint, never from
    // OnEvent, to avoid recomputing layout mid-event-dispatch for no
    // reason).
    void LayoutChildren();

    Axis               axis_;
    std::vector<Child> children_;
};

} // namespace ned::ui

#endif // NED_UI_LAYOUT_H
