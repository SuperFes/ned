//
// The Notcurses-backed replacement for ftxui::ScreenInteractive's own
// Loop()/Post() (FTXUI -> Notcurses migration, Phase 2). Deliberately kept
// decoupled from any concept of a Widget tree or Layout -- this owns
// Notcurses' own process lifecycle (init/teardown, raw input polling,
// terminal-signal suppression, cursor placement, resize detection, and
// thread-safe cross-thread posting) and nothing about *how* a frame gets
// composed. Source/UI/Layout.h and the composition root (Source/main.cpp),
// both Phase 3, are what turn "an Event arrived" / "repaint now" into an
// actual Widget tree walk; this file doesn't know Widget trees exist.
//

#ifndef NED_UI_EVENTLOOP_H
#define NED_UI_EVENTLOOP_H

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include "UI/Widget.h"

struct notcurses; // <notcurses/notcurses.h>
struct ncplane;

namespace ned::ui {

// Everything the loop needs from its caller to actually do anything --
// mirrors the shape of the handful of things main.cpp used to hand
// ScreenInteractive (a root Component via Loop(head), implicitly
// screen.Post() for background threads) plus the two things FTXUI used to
// do for us that Notcurses doesn't (recomputing layout on resize, deciding
// where the cursor goes), made explicit here since nothing else owns them
// now.
struct EventLoopCallbacks {
    // Called once at startup and again after every terminal resize, with
    // the new terminal Size -- the composition root's cue to recompute
    // every Widget's Box (Source/UI/Layout.h, Phase 3). Not called for any
    // other event; Paint doesn't need a resize to have happened to want to
    // repaint (e.g. the cursor blinking, a scratch auto-save completing).
    std::function<void(Size)> onResize;

    // Called for every non-resize input event (key or mouse), already
    // translated into our own Event -- the composition root's cue to route
    // it to the focused Widget (keyboard) or every Widget (mouse), the same
    // "every leaf gets every mouse event, hit-test yourself" contract
    // Widget::LocalMouseEvent already documents.
    std::function<void(const Event&)> onEvent;

    // Called once per loop iteration, after onResize/onEvent (if either
    // fired) and after any Post()ed work has run, whenever a repaint is due
    // -- expected to walk the Widget tree's Paint() calls into a shared
    // Screen and call Screen::Flush(loop.StdPlane()). Returns the absolute
    // (screen-space) cursor position to enable, or std::nullopt to hide it
    // -- typically FocusedWidget()'s own CursorPosition() translated by its
    // Box, but the loop itself doesn't know enough about Widget trees to
    // compute that translation on the caller's behalf.
    std::function<std::optional<Point>()> render;
};

// Owns exactly one Notcurses context for the process's lifetime -- like
// janet::Environment's own "construct at most one, ever" contract
// (Janet/Environment.h), though for an entirely different reason: this one
// is just RAII over a single global terminal resource, not a library with
// known init/deinit-cycle corruption bugs.
class EventLoop {
  public:
    EventLoop();
    ~EventLoop();

    EventLoop(const EventLoop&)            = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    [[nodiscard]] ncplane* StdPlane() const;
    [[nodiscard]] Size     TerminalSize() const;

    // Blocks until Exit() is called (from anywhere -- typically a Post()ed
    // callback reacting to CommandContext::quit, mirroring how the
    // FTXUI-era BufferView::OnKeyEvent used to call
    // ftxui::ScreenInteractive::Active()->Exit() directly). Drives
    // callbacks.onResize once immediately (the initial layout pass, no
    // resize needed to justify it) and callbacks.render once immediately
    // afterward, then services input/posted-work/resize events until told
    // to stop.
    void Run(const EventLoopCallbacks& callbacks);

    void Exit();

    // Thread-safe: queues fn to run on the loop's own thread at the next
    // opportunity, then wakes the loop if it's currently blocked waiting
    // for input -- the direct replacement for
    // ftxui::ScreenInteractive::Post, used the exact same way (BufferView's
    // scratch-auto-save background thread, LspManager's background
    // read-loop threads both need to marshal work back onto the thread
    // that owns every Widget's state, never touching it directly from a
    // second thread).
    void Post(std::function<void()> fn);

  private:
    // Runs every currently-queued Post()ed callback and returns whether any
    // ran at all (the caller's cue that a repaint is worth doing).
    bool DrainPosted_();
    void Wake_();

    // sidebar/drag-resize-not-working follow-up: a real, confirmed-via-
    // reading-Notcurses'-own-SGR-decoder bug (src/lib/in.c's mouse_click) --
    // an SGR (1006) mouse report for "button N still held, cursor moved"
    // carries no distinct evtype of its own; Notcurses collapses it into
    // exactly the same ncinput{.id = NCKEY_BUTTON<N>, .evtype = NCTYPE_PRESS}
    // shape a genuinely fresh click produces (confirmed by a real
    // NED_DEBUG_MOUSE capture: an entire drag decoded as a long run of
    // `press` events, button held constant, never a single `move`, so
    // Widget::Event::mouse() -- which only maps NCTYPE_UNKNOWN to
    // Motion::Moved -- never produced one either). Every consumer that
    // specifically waits for Motion::Moved during a drag (ProjectSidebar's
    // resize, BufferView's click-and-drag text selection, ScrollBar/
    // Minimap's own drag handling) was silently inert as a result. Fixed
    // once, centrally, here rather than patched into every consumer:
    // heldMouseButtonId_ tracks whichever NCKEY_BUTTON* id most recently
    // pressed without a matching release yet; Run()'s read loop reclassifies
    // a same-button PRESS/REPEAT arriving while that id is still held into
    // NCTYPE_UNKNOWN before constructing the Event, which Event::mouse()
    // then decodes as an ordinary Motion::Moved -- the semantics every
    // consumer already expected, restored at the one place that actually
    // has the sequential state needed to tell "held-and-moved" apart from
    // "freshly pressed."
    std::optional<std::uint32_t> heldMouseButtonId_;

    notcurses* nc_      = nullptr;
    bool       running_ = false;

    int wakeReadFd_  = -1;
    int wakeWriteFd_ = -1;

    std::mutex                         postedMutex_;
    std::vector<std::function<void()>> posted_;
};

// A one-shot "call this once, after this much wall-clock time, on the loop
// thread" primitive -- the direct replacement for the FTXUI-era
// ftxui::animation::RequestAnimationFrame()/Widget::OnAnimation pattern
// (BufferView's completion-debounce and status-message-idle-timeout
// deadlines both used it). FTXUI's mechanism was tied to the render loop
// itself (it only ever got a chance to check "has the deadline passed yet"
// on a real repaint, so it had to keep requesting another frame purely to
// keep polling); Notcurses' own EventLoop has no equivalent per-frame tick
// at all (see EventLoop's own header comment), so this goes back to the
// pre-FTXUI shape instead: a real background std::jthread that sleeps
// exactly the requested delay and Post()s the callback back onto the loop
// thread when it elapses, the same "own background thread, marshal back via
// Post" idiom BufferView's scratch-auto-save timer and
// ScrollArrowButton's press-and-hold repeat both already use. Re-arming
// (calling Arm again before the previous deadline fired) cancels the
// pending one outright -- there is only ever at most one pending fire per
// DeadlineTimer instance, matching how completionDebounceDeadline_/
// statusMessageChangedAt_ were each a single optional<time_point>, never a
// queue.
class DeadlineTimer {
  public:
    ~DeadlineTimer() {
        Cancel();
    }

    void Arm(EventLoop& loop, std::chrono::milliseconds delay, std::function<void()> onFire) {
        Cancel();
        thread_ = std::jthread([&loop, delay, onFire = std::move(onFire)](const std::stop_token& stopToken) {
            std::mutex                   mutex;
            std::unique_lock<std::mutex> lock(mutex);
            std::condition_variable_any  cv;
            // Returns false on a real timeout (predicate never became true)
            // -- exactly the "nobody cancelled me, the delay really
            // elapsed" case that should fire; true means stop_requested()
            // became true first (Cancel() or destruction), which must not
            // fire at all.
            if (!cv.wait_for(lock, stopToken, delay, [&stopToken] { return stopToken.stop_requested(); })) {
                loop.Post(onFire);
            }
        });
    }

    void Cancel() {
        if (thread_.joinable()) {
            thread_.request_stop();
        }
    }

  private:
    std::jthread thread_;
};

} // namespace ned::ui

#endif // NED_UI_EVENTLOOP_H
