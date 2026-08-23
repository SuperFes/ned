//
// Generic, mode-agnostic "link at point" detection and follow-on-activate --
// links follow-up. Deliberately its own small file rather than folded into
// Org.h: this namespace never needs to know Org exists at all, the same
// shared-engine/format-adapter split Source/Editor/Table.h established for
// Org/Markdown tables. Org's own [[target][description]] bracket syntax is a
// separate, Org-specific model (see Org.h's own Link/ParseLinks/LinkAtPoint) --
// BufferView::OpenLinkAtPoint is what ties the two together, trying Org's
// bracket links first in an org-mode buffer and falling back to this file's
// bare-URL/file-path detection everywhere else (and for an Org link whose own
// target turns out to be a URL or file path once its brackets are stripped).
//

#ifndef NED_EDITOR_LINK_H
#define NED_EDITOR_LINK_H

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ned::editor::link {

enum class LinkKind { Url,
                      File };

struct DetectedLink {
    LinkKind    kind;
    std::string target;
    std::size_t startByte;
    std::size_t endByte; // exclusive
};

// Scans only the line containing point (bufferText may be the whole buffer --
// this never looks outside that one line, matching Org.h's own line-oriented
// parsers). Three candidate shapes, tried in order:
//   1. A bare "https?://..." URL, trimmed of one trailing punctuation
//      character (one of ")]}>.,;:!?'\"") so a URL at the end of a sentence
//      doesn't swallow its own period -- a deliberate simplification against
//      a real balanced-paren linkifier, not attempted here.
//   2. The whitespace-delimited token under point, classified as a File
//      candidate only if it looks path-shaped -- contains '/', or has a
//      '.'-plus-suffix that looks like a file extension -- or is itself
//      already quote-/angle-bracket-delimited (see step 3's own stripping
//      logic, applied here too when point sits precisely inside the
//      delimiters). A bare word (no slash, no extension-shaped dot, no
//      delimiters) is never treated as a File candidate at all: without
//      this, a project with a real file literally named "TODO" at its root
//      would make the plain word "TODO" resolve and open unexpectedly. An
//      exact match here always wins over step 3's broader guess below.
//   3. Fallback, tried only when step 2 found nothing right under the
//      cursor: a quoted ("..."/'...') target anywhere within point's own
//      statement -- bounded by the nearest ';'/',' on either side, or the
//      whole line if there's neither -- regardless of whether point lands
//      on the target's own bytes. Point anywhere on `#include "foo.h"` or
//      `require("foo")` opens the same file landing exactly on "foo.h"
//      would: the quotes are already an unambiguous "this is a path"
//      signal, so there's no reason to also demand point sit precisely
//      inside them. An angle-bracketed (<...>) target gets the same
//      statement-wide treatment, but only when the line (leading
//      whitespace trimmed) starts with "#include" -- that's the one
//      construct in any C-family language where bare angle brackets denote
//      a file path; without this guard, point anywhere on an ordinary
//      template line (`std::vector<int> x;`) would misfire on "<int>".
//      Multiple candidates in the same statement resolve to whichever is
//      closest to point.
// Existence on disk is deliberately NOT checked here -- see ResolveFileLink
// below -- so this stays a pure, testable function over plain text, the
// same "UI-agnostic, string_view in" shape every other Source/Editor/ parser
// already uses. nullopt if point isn't on any of the three shapes.
[[nodiscard]] std::optional<DetectedLink> DetectLinkAtPoint(std::string_view bufferText, std::size_t point);

// Classifies an already-isolated target string -- e.g. Org's own extracted
// link target, with no surrounding bracket syntax or line context -- as a
// bare URL (Url if it starts with "http://"/"https://") or otherwise a File.
// Unlike DetectLinkAtPoint, this never scans surrounding text; used when a
// target string is already known in isolation rather than found by scanning
// (see BufferView::OpenLinkAtPoint's own Org-bracket-link handling).
[[nodiscard]] LinkKind ClassifyTarget(std::string_view target);

// Resolves target to a real, existing file: tried as an absolute path, then
// relative to baseDirectory (typically the active buffer's own containing
// directory), then relative to editor::ProjectRoot(), then relative to each of
// includePaths in order (ProjectSettings.h's includePaths -- a non-project-relative
// #include/import target, e.g. an angle-form C/C++ #include or a vendored
// dependency, needs a directory list to search since it isn't found under
// baseDirectory/ProjectRoot at all). nullopt if none exist on disk -- unlike
// find-file, a link to a path that doesn't exist is a dead link to be reported as
// such, never silently turned into a new empty buffer/file.
[[nodiscard]] std::optional<std::filesystem::path> ResolveFileLink(
    const std::string& target, const std::filesystem::path& baseDirectory,
    const std::vector<std::filesystem::path>& includePaths = {});

// Process-wide, mutex-guarded static state (mirrors TabWidth.h's exact
// pattern) -- unlike FormatOnSave.h's FormatCommand, which defaults unset
// because there's no universally-safe default formatter, this defaults to
// "xdg-open": opening a URL is safe/non-destructive, xdg-open is the de
// facto standard opener on every XDG-compliant Linux desktop (matching this
// project's already-Linux-only stance elsewhere via its XDG conventions),
// and the ROADMAP's own original wishlist entry for this feature explicitly
// named "xdg-open-style" as the intended behavior. Still fully overridable
// from Janet (ned/set-url-open-command) for e.g. WSL (wslview) or a
// non-default browser.
void                                     SetUrlOpenCommand(std::optional<std::string> command);
[[nodiscard]] std::optional<std::string> UrlOpenCommand();

// Launches UrlOpenCommand() with url as its own argv element via fork+execlp
// -- deliberately NOT std::system/a shell string: url comes from buffer
// *content*, which can be an untrusted file (a cloned repo, a downloaded
// note), so splicing it into a shell command the way RunFormatCommand does
// for its own *user-configured* command (FormatOnSave.cpp's own comment:
// that's safe only because the string there is the user's own configured
// command, not arbitrary file content) would be a real command-injection
// hole here. execlp receives url as its own argument, never parsed by a
// shell, so no quoting/escaping logic is needed or trusted. Fire-and-forget:
// the child is reaped by a detached background thread's own waitpid call,
// not a process-wide SIGCHLD/SIG_IGN (which would silently break
// RunFormatCommand's own std::system-based child-reaping elsewhere in this
// same process -- POSIX wait()/waitpid() return -1/ECHILD once SIGCHLD is
// globally ignored). Returns false without launching anything if
// UrlOpenCommand() is unset/empty or fork() itself fails; a launched-but-
// failing opener (bad command, browser crashes) isn't distinguishable from
// success -- this never waits to find out.
bool OpenUrl(const std::string& url);

} // namespace ned::editor::link

#endif // NED_EDITOR_LINK_H
