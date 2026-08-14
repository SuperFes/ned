//
// A rebindable pointer to "the buffer currently shown/edited" -- the one
// piece of shared state BufferView and ModeLine both need to agree on so
// that switching buffers (find-file, switch-to-buffer) updates both at
// once, rather than each permanently binding its own text::Buffer&
// reference at construction the way they used to.
//

#ifndef NED_UI_ACTIVEBUFFER_H
#define NED_UI_ACTIVEBUFFER_H

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
        current_ = &buffer;
    }

  private:
    text::Buffer* current_;
};

} // namespace ned::ui

#endif // NED_UI_ACTIVEBUFFER_H
