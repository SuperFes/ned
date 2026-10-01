//
// Callbacks that outlive their owner do nothing instead of touching it.
//
// An object that hands a [this] callback to something it does not own (a
// language server reply, a version control job, a timer posted to the event
// loop) has no way to take that callback back when it is destroyed. Binding
// the callback through the object's LifetimeGuard makes it a no-op once the
// guard is gone:
//
//     lspManager_->RequestHover(buffer, offset, lifetime_.Bind([this](auto reply) { ... }));
//
// Bind and Token are main-thread calls on the owner. A LifetimeToken is a
// plain copyable value, so one taken on the main thread can be carried to
// another thread and checked or bound there.
//

#ifndef NED_EDITOR_LIFETIME_H
#define NED_EDITOR_LIFETIME_H

#include <memory>
#include <utility>

namespace ned::editor {

class LifetimeToken {
  public:
    [[nodiscard]] bool Alive() const {
        return !alive_.expired();
    }

    // fn, wrapped to return without calling it once the guard is gone.
    template <typename Fn>
    [[nodiscard]] auto Bind(Fn fn) const {
        return [alive = alive_, fn = std::move(fn)](auto&&... args) mutable {
            if (!alive.expired()) {
                fn(std::forward<decltype(args)>(args)...);
            }
        };
    }

  private:
    friend class LifetimeGuard;

    explicit LifetimeToken(std::weak_ptr<const int> alive) : alive_(std::move(alive)) {
    }

    std::weak_ptr<const int> alive_;
};

class LifetimeGuard {
  public:
    LifetimeGuard()                                = default;
    LifetimeGuard(const LifetimeGuard&)            = delete;
    LifetimeGuard& operator=(const LifetimeGuard&) = delete;

    [[nodiscard]] LifetimeToken Token() const {
        return LifetimeToken(alive_);
    }

    template <typename Fn>
    [[nodiscard]] auto Bind(Fn fn) const {
        return Token().Bind(std::move(fn));
    }

    // Kills every token handed out so far without destroying the guard.
    void Revoke() {
        alive_ = std::make_shared<const int>(0);
    }

  private:
    std::shared_ptr<const int> alive_ = std::make_shared<const int>(0);
};

} // namespace ned::editor

#endif // NED_EDITOR_LIFETIME_H
