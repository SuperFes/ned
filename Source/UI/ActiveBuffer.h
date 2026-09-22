//
// A rebindable pointer to "the buffer currently shown/edited" -- the one
// piece of shared state BufferView and ModeLine both need to agree on so
// that switching buffers (find-file, switch-to-buffer) updates both at
// once, rather than each permanently binding its own text::Buffer&
// reference at construction the way they used to.
//

#ifndef NED_UI_ACTIVEBUFFER_H
#define NED_UI_ACTIVEBUFFER_H

#include <functional>
#include <utility>

#include "Text/Buffer.h"

namespace ned::ui {

class ActiveBuffer {
  public:
    explicit ActiveBuffer(text::Buffer& initial) : current_(&initial) {
    }

    [[nodiscard]] text::Buffer& Get() const {
        return *current_;
    }
    void Set(text::Buffer& buffer) {
        const bool changed = (current_ != &buffer);
        if (changed) {
            // The outgoing buffer stops being on screen here, which is what
            // makes its unseen-content frontier mean "since you last left".
            // Done here rather than on the next Paint's own switch detection
            // because this is the last point the outgoing buffer is
            // guaranteed to still be alive (a close switches away first,
            // then destroys).
            current_->CommitSeenContent();
        }
        current_ = &buffer;
        if (changed && onChange_) {
            onChange_(buffer);
        }
    }

    // MRU-close follow-up: fires with the newly current buffer whenever Set
    // actually changes it (never for a same-buffer Set). Unset by default
    // (every pre-existing construction site and test), same no-op-by-
    // absence convention as TabBar::SetOnCloseRequest. WindowManager's Pane
    // wires this to text::BufferList::TouchBuffer, making Set the one choke
    // point that keeps the MRU order current across every switch path.
    void SetOnChange(std::function<void(text::Buffer&)> hook) {
        onChange_ = std::move(hook);
    }

  private:
    text::Buffer*                      current_;
    std::function<void(text::Buffer&)> onChange_;
};

} // namespace ned::ui

#endif // NED_UI_ACTIVEBUFFER_H
