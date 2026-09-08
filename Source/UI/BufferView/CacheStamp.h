//
// What a derived-data cache was computed from, so it can tell whether it is
// still good without re-deriving anything -- see Docs/BufferViewDecomposition.md.
//
// Every per-line cache BufferView keeps (folds, diagnostics, symbols, tests,
// coverage, blame, links, row counts, ...) answers the same question the same
// way: "was this computed from the buffer I am about to read, at the generation
// counters it is at now?" Each one used to spell that out as its own loose group
// of members -- a buffer pointer, one to three generation stamps, and for the
// windowed derivations a byte range -- re-compared by hand in its own Ensure*
// method. That is one idea written out fourteen times, and the two traps in it
// (a windowed cache that forgets the window is part of its key, and a cache
// whose stamps get marked current under the wrong mode) had to be got right
// fourteen times over.
//
// A stamp is compared against a freshly built one rather than against a list of
// values, so the key is written once per cache instead of once per comparison:
// the check and the store cannot drift apart.
//
//     const CacheStamp stamp = CacheStamp::For(&buffer, {buffer.ContentGeneration(),
//                                                        buffer.FoldGeneration()});
//     if (foldCacheStamp_.Matches(stamp)) {
//         return;                       // still good -- whatever is memoized stands
//     }
//     ... recompute ...
//     foldCacheStamp_ = stamp;          // now good for exactly this key
//
// The stamp deliberately does not own the derived value. The payloads differ in
// type and several caches produce more than one of them, so they stay as their
// own members under their own names; this type is only ever the validity half.
//

#ifndef NED_UI_BUFFERVIEW_CACHESTAMP_H
#define NED_UI_BUFFERVIEW_CACHESTAMP_H

#include <algorithm>
#include <array>
#include <cstddef>
#include <initializer_list>

namespace ned::text {
class Buffer;
} // namespace ned::text

namespace ned::ui::bufferview {

class CacheStamp {
  public:
    // Six is comfortably above the widest key in use (a windowed cache keyed on
    // two generation counters needs four). Building one with more is a
    // programming error, and is truncated rather than silently ignored -- see
    // For() below.
    static constexpr std::size_t kMaxValues = 6;

    // A default-constructed stamp holds no buffer, which is what "never
    // computed" means; it matches nothing, including another empty stamp.
    CacheStamp() = default;

    [[nodiscard]] static CacheStamp For(const text::Buffer* buffer, std::initializer_list<std::size_t> values) {
        CacheStamp stamp;
        stamp.buffer_ = buffer;
        stamp.count_  = std::min(values.size(), kMaxValues);
        std::copy_n(values.begin(), stamp.count_, stamp.values_.begin());
        return stamp;
    }

    // True only when both stamps name the same buffer and agree on every value.
    // An empty stamp on either side never matches: a cache that was never
    // computed, or was explicitly invalidated, has to be rebuilt.
    [[nodiscard]] bool Matches(const CacheStamp& other) const {
        if (buffer_ == nullptr || other.buffer_ == nullptr) {
            return false;
        }
        return buffer_ == other.buffer_ && count_ == other.count_ &&
               std::equal(values_.begin(), values_.begin() + static_cast<std::ptrdiff_t>(count_), other.values_.begin());
    }

    // Forget what this cache was computed for, so the next Matches() fails and
    // the payload is rebuilt. The payload itself is left alone -- callers that
    // want it cleared clear it themselves, since several keep a previous result
    // visible until the replacement is ready.
    void Invalidate() {
        *this = CacheStamp{};
    }

    // Whether this stamp is for that buffer, for the "drop the caches belonging
    // to a buffer that is going away" sweep. An empty stamp is for no buffer.
    [[nodiscard]] bool IsFor(const text::Buffer* buffer) const {
        return buffer_ != nullptr && buffer_ == buffer;
    }

  private:
    const text::Buffer*                 buffer_ = nullptr;
    std::array<std::size_t, kMaxValues> values_{};
    std::size_t                         count_ = 0;
};

} // namespace ned::ui::bufferview

#endif // NED_UI_BUFFERVIEW_CACHESTAMP_H
