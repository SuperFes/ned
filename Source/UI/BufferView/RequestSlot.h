//
// One in-flight asynchronous request, and whether a reply that just arrived is
// still the one being waited for -- see Docs/BufferViewDecomposition.md.
//
// Every request BufferView sends to a language server, debug adapter or version
// control provider comes back on a callback, by which time the reason for
// sending it may be gone: point moved, the buffer was switched, the user typed
// another character and a newer request went out. Each of those sites solved
// that the same way -- a counter bumped when a request goes out, captured into
// the callback, and compared on the way back in -- and each spelled it out as a
// bare std::size_t member with the comparison written by hand.
//
// Writing it by hand is what makes it easy to get subtly wrong: comparing the
// wrong counter, forgetting the comparison altogether, or bumping to cancel in
// one place and not another. The type makes each of those a compile error or a
// named call instead of a convention.
//
//     const std::size_t token = hoverRequest_.Begin();      // supersedes any in flight
//     lspManager_->RequestHover(..., [this, token](auto reply) {
//         if (hoverRequest_.IsStale(token)) {
//             return;                                       // a newer one went out
//         }
//         ...
//     });
//
// Cancel() abandons whatever is in flight without issuing anything, so a reply
// still on its way is dropped when it lands.
//
// This is deliberately only the staleness half, not a request handle: it holds
// no callback, no payload and no timer, and knows nothing about what was asked.
// Those differ per feature and stay with the feature.
//

#ifndef NED_UI_BUFFERVIEW_REQUESTSLOT_H
#define NED_UI_BUFFERVIEW_REQUESTSLOT_H

#include <cstddef>

namespace ned::ui::bufferview {

class RequestSlot {
  public:
    // What Begin() hands out for a callback to capture and check itself
    // against. Only meaningful to the slot that issued it.
    using Token = std::size_t;

    // Issue a request, superseding any already in flight: their tokens go stale
    // immediately, so whichever reply lands first, only the newest is used.
    [[nodiscard]] Token Begin() {
        return ++generation_;
    }

    // The token of the request currently in flight, without issuing a new one --
    // for a follow-up that must ride along with a request already sent rather
    // than superseding it.
    [[nodiscard]] Token Current() const {
        return generation_;
    }

    // Whether this token has been superseded or cancelled. A reply whose token
    // is stale must be dropped, not applied.
    [[nodiscard]] bool IsStale(Token token) const {
        return token != generation_;
    }

    // Abandon whatever is in flight without issuing a replacement.
    void Cancel() {
        ++generation_;
    }

  private:
    // Starts at 1 rather than 0 so that no token this slot ever hands out --
    // from Begin() or Current() -- can collide with a default-initialised
    // Token{}. A stray zero therefore always reads as stale instead of
    // accidentally matching a slot that has not issued anything yet.
    Token generation_ = 1;
};

} // namespace ned::ui::bufferview

#endif // NED_UI_BUFFERVIEW_REQUESTSLOT_H
