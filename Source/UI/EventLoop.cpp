#include "EventLoop.h"

#include <stdexcept>

#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

#include <notcurses/notcurses.h>

#include "UI/KeyTranslation.h"

namespace ned::ui {

EventLoop::EventLoop() {
    // NCOPTION_NO_QUIT_SIGHANDLERS: Notcurses otherwise installs its own
    // SIGINT/SIGILL/SIGSEGV/SIGABRT/SIGTERM handlers that call
    // notcurses_stop() and exit -- the same "library unilaterally decides
    // when we're done" trap ftxui::ScreenInteractive::ForceHandleCtrlC's
    // default (true) turned out to be (see the FTXUI-era comment this
    // replaced in main.cpp, and CLAUDE.md's own account of the C-c
    // C-p-exits-the-whole-process bug that finding prevented from
    // recurring here). notcurses_linesigs_disable (called right below,
    // once nc_ exists) is the deeper fix specifically for Ctrl-C/Ctrl-Z/
    // Ctrl-\\ -- it stops the terminal's own line discipline from raising
    // SIGINT/SIGTSTP/SIGQUIT at all, so those keys arrive as plain input
    // bytes through notcurses_get like everything else, the same
    // "our own key bindings always win" intent ForceHandleCtrlC(false) had,
    // just resolved one layer lower (no signal is ever raised in the first
    // place, rather than raised-then-suppressed). NO_QUIT_SIGHANDLERS is
    // kept anyway as a defensive second layer for the signals
    // linesigs_disable doesn't cover (SIGSEGV et al. from a real crash
    // shouldn't silently vanish into Notcurses' own handler either, since
    // this project has its own top-level exception handling to prefer).
    // NCOPTION_SUPPRESS_BANNERS matches how Phase 0's own smoke test was
    // run -- no reason for Notcurses' own startup/perf banner to appear on
    // top of this editor's UI.
    notcurses_options opts{};
    opts.flags = NCOPTION_NO_QUIT_SIGHANDLERS | NCOPTION_SUPPRESS_BANNERS;

    nc_ = notcurses_core_init(&opts, nullptr);
    if (nc_ == nullptr) {
        throw std::runtime_error("EventLoop: notcurses_core_init failed");
    }

    notcurses_linesigs_disable(nc_);
    notcurses_mice_enable(nc_, NCMICE_ALL_EVENTS);

    // C-x-C-s-passes-scroll-lock-to-the-terminal follow-up: Notcurses' own
    // raw-mode setup (termdesc.c) only clears ICRNL, not IXON/IXOFF -- the
    // termios flags controlling XON/XOFF software flow control -- so C-s/
    // C-q still reach the terminal's line discipline as start/stop-output
    // signals instead of arriving as plain input bytes, freezing the
    // terminal (Scroll Lock-style) the instant a C-s-prefixed binding (e.g.
    // C-x C-s, save-buffer) is pressed, the same class of bug
    // notcurses_linesigs_disable above already fixed for C-c/C-z/C-\.
    // Cleared here by hand on STDIN_FILENO, the same fd notcurses_get reads
    // from -- best-effort, matching linesigs_disable's own return-value
    // handling (both are silently no-ops on a non-terminal stdin, which
    // never happens in real usage but shouldn't crash a test harness either).
    termios rawTermios{};
    if (tcgetattr(STDIN_FILENO, &rawTermios) == 0) {
        rawTermios.c_iflag &= ~static_cast<tcflag_t>(IXON | IXOFF);
        tcsetattr(STDIN_FILENO, TCSANOW, &rawTermios);
    }

    // A pipe purely for Post() to wake a blocked poll() from another thread
    // -- notcurses_inputready_fd (below, in Run) gives us a real pollable
    // fd for terminal input, but there's no equivalent "wake me up, a
    // background thread posted work" fd Notcurses itself provides, so this
    // is the standard self-pipe trick to build one.
    int fds[2];
    if (pipe(fds) != 0) {
        notcurses_stop(nc_);
        throw std::runtime_error("EventLoop: pipe() failed");
    }
    wakeReadFd_  = fds[0];
    wakeWriteFd_ = fds[1];
    fcntl(wakeReadFd_, F_SETFL, fcntl(wakeReadFd_, F_GETFL) | O_NONBLOCK);
    fcntl(wakeWriteFd_, F_SETFL, fcntl(wakeWriteFd_, F_GETFL) | O_NONBLOCK);
}

EventLoop::~EventLoop() {
    if (wakeReadFd_ >= 0)
        close(wakeReadFd_);
    if (wakeWriteFd_ >= 0)
        close(wakeWriteFd_);
    if (nc_ != nullptr) {
        notcurses_stop(nc_);
    }
}

ncplane* EventLoop::StdPlane() const {
    return notcurses_stdplane(nc_);
}

Size EventLoop::TerminalSize() const {
    unsigned y = 0, x = 0;
    notcurses_stddim_yx(nc_, &y, &x);
    return Size{static_cast<int>(x), static_cast<int>(y)};
}

void EventLoop::Wake_() {
    const char byte = 0;
    // Best-effort: if the pipe is momentarily full the reader will still
    // wake for the bytes already queued, and Post()'s own posted_ vector
    // (not the pipe) is what actually carries the work -- the pipe is only
    // ever a wakeup signal, never the payload.
    [[maybe_unused]] const ssize_t written = write(wakeWriteFd_, &byte, 1);
}

void EventLoop::Post(std::function<void()> fn) {
    {
        const std::lock_guard<std::mutex> lock(postedMutex_);
        posted_.push_back(std::move(fn));
    }
    Wake_();
}

bool EventLoop::DrainPosted_() {
    std::vector<std::function<void()>> work;
    {
        const std::lock_guard<std::mutex> lock(postedMutex_);
        work.swap(posted_);
    }
    for (auto& fn : work) {
        fn();
    }
    return !work.empty();
}

void EventLoop::Exit() {
    running_ = false;
    Wake_();
}

void EventLoop::Run(const EventLoopCallbacks& callbacks) {
    running_ = true;

    Size lastSize = TerminalSize();
    if (callbacks.onResize) {
        callbacks.onResize(lastSize);
    }
    auto repaint = [&] {
        std::optional<Point> cursor = callbacks.render ? callbacks.render() : std::nullopt;
        if (cursor) {
            notcurses_cursor_enable(nc_, cursor->y, cursor->x);
        }
        else {
            notcurses_cursor_disable(nc_);
        }
        notcurses_render(nc_);
    };
    repaint();

    const int inputFd = notcurses_inputready_fd(nc_);

    while (running_) {
        struct pollfd fds[2];
        fds[0].fd     = inputFd;
        fds[0].events = POLLIN;
        fds[1].fd     = wakeReadFd_;
        fds[1].events = POLLIN;

        const int pollResult = poll(fds, 2, -1);
        if (pollResult < 0) {
            continue; // EINTR (e.g. SIGWINCH) -- fall through to the drain loop below, which is a safe no-op if nothing's ready yet
        }

        // Drain the wake pipe unconditionally (cheap, and avoids a
        // build-up of unread bytes across many Post() calls between wakes)
        // before running whatever was actually posted.
        char discard[64];
        while (read(wakeReadFd_, discard, sizeof(discard)) > 0) {
        }
        // Posted work (scratch auto-save, LSP background-thread results,
        // ...) may itself have mutated state a repaint should reflect --
        // e.g. new diagnostics arriving -- so running any of it at all
        // earns this iteration a repaint, the same way FTXUI's own Post()
        // always triggered its next frame regardless of what the posted
        // callback actually did.
        bool needsRepaint = DrainPosted_();

        ncinput  input{};
        uint32_t id = 0;
        while ((id = notcurses_get_nblock(nc_, &input)) != 0) {
            if (id == static_cast<uint32_t>(-1)) {
                break; // real error -- stop draining this round, try again next wakeup
            }
            if (id == NCKEY_RESIZE) {
                const Size newSize = TerminalSize();
                if ((newSize.width != lastSize.width || newSize.height != lastSize.height) && callbacks.onResize) {
                    lastSize = newSize;
                    callbacks.onResize(newSize);
                }
                needsRepaint = true;
                continue;
            }
            if (id == NCKEY_SIGNAL || id == NCKEY_EOF) {
                continue;
            }
            // See heldMouseButtonId_'s own doc comment (EventLoop.h) for the
            // real, confirmed root cause this reclassification fixes: a
            // held-button drag-motion sample and a genuinely fresh press
            // are otherwise indistinguishable once Notcurses hands them
            // over, both id == NCKEY_BUTTON<N> / evtype == NCTYPE_PRESS.
            if (nckey_mouse_p(input.id)) {
                if (input.evtype == NCTYPE_RELEASE) {
                    heldMouseButtonId_.reset();
                }
                else if (input.evtype == NCTYPE_PRESS || input.evtype == NCTYPE_REPEAT) {
                    if (heldMouseButtonId_ == input.id) {
                        input.evtype = NCTYPE_UNKNOWN; // Event::mouse() decodes this as Motion::Moved
                    }
                    else {
                        heldMouseButtonId_ = input.id;
                    }
                }
            }
            if (callbacks.onEvent) {
                callbacks.onEvent(Event(input));
            }
            needsRepaint = true;
        }

        if (needsRepaint && running_) {
            repaint();
        }
    }
}

} // namespace ned::ui
