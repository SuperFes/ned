//
// Unordered containers keyed by Buffer address whose entries cannot outlive
// what they were stored for: a buffer drops its entries when it is destroyed
// or assigned over, so a buffer later allocated at the same address never
// inherits one. Shaped like the std container they wrap, for the members
// that are here.
//
// Not thread-safe; the owner's own discipline applies. A buffer's entries
// are dropped on the thread that destroys or assigns it, under `guard` when
// one is given.
//

#ifndef NED_TEXT_PER_BUFFER_MAP_H
#define NED_TEXT_PER_BUFFER_MAP_H

#include <mutex>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "Text/Buffer.h"
#include "Text/BufferEntryOwners.h"

namespace ned::text {

template <typename Container>
class BufferKeyed final : public BufferEntryOwner {
    static constexpr bool kIsMap = requires { typename Container::mapped_type; };

  public:
    using key_type       = typename Container::key_type;
    using value_type     = typename Container::value_type;
    using size_type      = typename Container::size_type;
    using iterator       = typename Container::iterator;
    using const_iterator = typename Container::const_iterator;

    static_assert(std::is_same_v<key_type, Buffer*> || std::is_same_v<key_type, const Buffer*>);

    explicit BufferKeyed(std::mutex* guard = nullptr) : guard_(guard) {
    }

    BufferKeyed(const BufferKeyed&)            = delete;
    BufferKeyed& operator=(const BufferKeyed&) = delete;

    ~BufferKeyed() {
        clear();
    }

    [[nodiscard]] iterator begin() {
        return entries_.begin();
    }
    [[nodiscard]] iterator end() {
        return entries_.end();
    }
    [[nodiscard]] const_iterator begin() const {
        return entries_.begin();
    }
    [[nodiscard]] const_iterator end() const {
        return entries_.end();
    }

    [[nodiscard]] bool empty() const {
        return entries_.empty();
    }
    [[nodiscard]] size_type size() const {
        return entries_.size();
    }

    [[nodiscard]] iterator find(key_type key) {
        return entries_.find(key);
    }
    [[nodiscard]] const_iterator find(key_type key) const {
        return entries_.find(key);
    }
    [[nodiscard]] bool contains(key_type key) const {
        return entries_.contains(key);
    }

    std::pair<iterator, bool> insert(value_type value) {
        return Attached(entries_.insert(std::move(value)));
    }

    template <typename... Args>
        requires kIsMap
    std::pair<iterator, bool> try_emplace(key_type key, Args&&... args) {
        return Attached(entries_.try_emplace(key, std::forward<Args>(args)...));
    }

    template <typename Value>
        requires kIsMap
    std::pair<iterator, bool> insert_or_assign(key_type key, Value&& value) {
        return Attached(entries_.insert_or_assign(key, std::forward<Value>(value)));
    }

    auto& operator[](key_type key)
        requires kIsMap
    {
        return try_emplace(key).first->second;
    }

    auto& at(key_type key)
        requires kIsMap
    {
        return entries_.at(key);
    }
    const auto& at(key_type key) const
        requires kIsMap
    {
        return entries_.at(key);
    }

    size_type erase(key_type key) {
        if (entries_.erase(key) == 0) {
            return 0;
        }
        key->DetachEntryOwner(*this);
        return 1;
    }

    iterator erase(const_iterator position) {
        KeyOf(*position)->DetachEntryOwner(*this);
        return entries_.erase(position);
    }

    void clear() {
        for (const value_type& value : entries_) {
            KeyOf(value)->DetachEntryOwner(*this);
        }
        entries_.clear();
    }

    void ForgetBuffer(const Buffer* buffer) override {
        // Only a lookup key; nothing is reached through it.
        const auto key = const_cast<key_type>(buffer);
        if (guard_ != nullptr) {
            const std::lock_guard lock(*guard_);
            entries_.erase(key);
            return;
        }
        entries_.erase(key);
    }

  private:
    static key_type KeyOf(const value_type& value) {
        if constexpr (kIsMap) {
            return value.first;
        }
        else {
            return value;
        }
    }

    std::pair<iterator, bool> Attached(std::pair<iterator, bool> result) {
        if (result.second) {
            KeyOf(*result.first)->AttachEntryOwner(*this);
        }
        return result;
    }

    Container   entries_;
    std::mutex* guard_;
};

template <typename T, typename Key = Buffer*>
using PerBufferMap = BufferKeyed<std::unordered_map<Key, T>>;

template <typename Key = Buffer*>
using PerBufferSet = BufferKeyed<std::unordered_set<Key>>;

} // namespace ned::text

#endif // NED_TEXT_PER_BUFFER_MAP_H
