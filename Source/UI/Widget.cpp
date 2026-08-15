#include "Widget.h"

namespace ned::ui {

namespace {

    // Bridges Widget's Paint()/MinimumSize()/SetSize_ into the shape FTXUI's
    // own layout/render pipeline expects (ComputeRequirement/SetBox/Render
    // on a Node) -- see Widget.h's own comment for why this holds a Widget&
    // rather than being a base class itself.
    class PaintNode : public ftxui::Node {
      public:
        explicit PaintNode(Widget& owner) : owner_(owner) {
        }

        void ComputeRequirement() override {
            const Size minimum = owner_.MinimumSize();
            requirement_.min_x = minimum.width;
            requirement_.min_y = minimum.height;

            // Cursor *presence* (enabled/node/cursor_shape) has to be
            // reported here, in ComputeRequirement, not in SetBox -- even
            // though the real screen position can only be computed in
            // SetBox (see that override's own comment). A parent container
            // (hbox/vbox) aggregates focus from its children by reading
            // child->requirement() *during its own ComputeRequirement call*
            // (confirmed against hbox.cpp/vbox.cpp's real
            // `requirement_.focused.Prefer(child->requirement().focused)`),
            // which for a simple, non-multi-iteration tree like ours (the
            // only kind that exists here -- nothing overrides Check() to
            // request a second layout pass) happens in the SAME single
            // ComputeRequirement sweep across the whole tree, strictly
            // before ANY node's SetBox runs at all. Setting focused fields
            // only in SetBox (a real bug introduced and then caught within
            // this same session, verified with a standalone repro against a
            // widget nested inside an hbox/vbox exactly like BufferView
            // really is) made every parent's aggregation see "not focused"
            // permanently -- the cursor never propagated up to the root at
            // all, landing FTXUI's own Render() in its no-cursor fallback
            // (bottom-right corner, Hidden shape) instead of anywhere near
            // the real target. `.node`'s VALUE (a stable pointer to
            // cursorAnchor_) is all a parent's aggregation ever actually
            // reads/copies at this stage -- the box inside the pointed-to
            // Node isn't dereferenced until ftxui::Render()'s own final
            // step, which only runs after the whole tree's SetBox pass has
            // completed, which is what makes finishing the box's real value
            // in SetBox (below) still correct.
            if (const std::optional<Point> cursor = owner_.CursorPosition()) {
                requirement_.focused.enabled      = true;
                requirement_.focused.cursor_shape = owner_.CursorShape();
                requirement_.focused.node         = &cursorAnchor_;
            }
            else {
                requirement_.focused.enabled = false;
            }
        }

        void SetBox(ftxui::Box box) override {
            Node::SetBox(box);
            owner_.SetBox_(box);

            // Finishes what ComputeRequirement started: the real absolute
            // cursor box needs box_ as this frame's real assignment, which
            // only exists from here on (Widget::OnRender() constructs a
            // brand-new PaintNode every single frame -- FTXUI rebuilds its
            // whole Element tree from scratch each frame, confirmed by
            // reading App::Internal::Draw -- so box_ is default-constructed
            // {0,0,0,0} for the whole ComputeRequirement pass, not "the
            // previous frame's box" the way an earlier version of this
            // comment assumed; a real bug, reported live as "cursor renders
            // a line too high with the sidebar collapsed, and far to the
            // left with it open" and confirmed by a standalone repro before
            // being fixed).
            //
            // ftxui::Render (dom/node.cpp) places the terminal cursor by
            // dereferencing requirement().focused.node->box_ directly --
            // NOT requirement().focused.box (that field is used elsewhere,
            // e.g. frame.cpp's scroll-into-view calculation) -- and only
            // after the whole tree's SetBox pass has already finished, so
            // finalizing cursorAnchor_'s box_ here, this late, is still
            // correct. Every built-in FTXUI component that reports focus
            // (Focus, Frame) gets away with never doing this two-step split
            // at all: they wrap a tightly-bound child element whose own
            // box_, once the normal recursive SetBox pass reaches it,
            // already *is* the exact target cell. PaintNode has no such
            // child -- it's one monolithic Node spanning the whole widget,
            // with the cursor at some dynamic point inside it -- so
            // cursorAnchor_ is a plain, otherwise-unused Node that exists
            // solely to hold that exact absolute box, driven manually
            // (bypassing FTXUI's normal layout traversal entirely -- it
            // isn't a child of this node, so nothing else ever calls
            // SetBox/ComputeRequirement/Render on it).
            if (const std::optional<Point> cursor = owner_.CursorPosition()) {
                cursorAnchor_.SetBox(ftxui::Box{
                    .x_min = box_.x_min + cursor->x,
                    .x_max = box_.x_min + cursor->x,
                    .y_min = box_.y_min + cursor->y,
                    .y_max = box_.y_min + cursor->y,
                });
            }
        }

        void Render(ftxui::Screen& screen) override {
            owner_.Paint(Canvas(screen, box_));
        }

      private:
        Widget&     owner_;
        ftxui::Node cursorAnchor_; // see ComputeRequirement's own comment
    };

} // namespace

ftxui::Element Widget::OnRender() {
    return std::make_shared<PaintNode>(*this);
}

} // namespace ned::ui
