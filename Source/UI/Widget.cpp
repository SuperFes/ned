#include "Widget.h"

namespace ned::ui {

namespace {

    // Bridges Widget's Paint()/MinimumSize()/SetSize_ into the shape FTXUI's
    // own layout/render pipeline expects (ComputeRequirement/SetBox/Render
    // on a Node) -- see Widget.h's own comment for why this holds a Widget&
    // rather than being a base class itself.
    class PaintNode : public ftxui::Node {
      public:
        explicit PaintNode(Widget& owner) : owner_(owner) {}

        void ComputeRequirement() override {
            const Size minimum = owner_.MinimumSize();
            requirement_.min_x = minimum.width;
            requirement_.min_y = minimum.height;
        }

        void SetBox(ftxui::Box box) override {
            Node::SetBox(box);
            owner_.SetBox_(box);
        }

        void Render(ftxui::Screen& screen) override {
            owner_.Paint(Canvas(screen, box_));
        }

      private:
        Widget& owner_;
    };

} // namespace

ftxui::Element Widget::OnRender() {
    return std::make_shared<PaintNode>(*this);
}

} // namespace ned::ui
