//
// Vim-flavored register storage: yank/delete text keyed by a character, with vim's own
// routing rules (unnamed "", named a-z/A-Z-append, the numbered "1-"9 delete ring, "0
// (last yank), "_ (blackhole), "+/"* (system clipboard)). A new type, not a reuse of
// Editor/Register.h's RegisterTable (Emacs point-registers don't apply here, and vim's
// ring-rotation/append semantics are a different shape than that single-slot-per-name
// store) or Text/KillRing.h (no ring-rotation-on-every-kill or register-name concept
// there at all). Pieces mirror RegisterTable/KillRing's own "one piece per line/cursor"
// convention -- a Char entry always has exactly one piece (possibly empty), a Line entry
// one piece per line (each without its own trailing newline), a Block entry one piece
// per visual-block row.
//

#ifndef NED_EDITOR_VIM_VIMREGISTERS_H
#define NED_EDITOR_VIM_VIMREGISTERS_H

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace ned::editor::vim {

enum class RegisterKind { Char,
                          Line,
                          Block };

struct RegisterEntry {
    std::vector<std::string> pieces;
    RegisterKind             kind = RegisterKind::Char;

    // Char: pieces[0]. Line: every piece newline-joined, with a trailing newline. Block:
    // every piece newline-joined (informational only -- paste's own per-row column
    // placement is VimEngine's concern, not this type's).
    [[nodiscard]] std::string Joined() const;
};

class VimRegisters {
  public:
    // name == 0 means "no register was explicitly named" -- Store then applies vim's
    // own unnamed-write routing: isDelete false (a yank) writes "0; isDelete true and
    // entry spans a single Char piece with no embedded newline (a "small delete") writes
    // "-; any other delete shifts the numbered ring ("1 -> "2 -> ... -> "9, dropping the
    // oldest) and writes the new text into "1. Every write (named or unnamed) also
    // mirrors into the unnamed register "", except "_ (blackhole, discarded entirely)
    // and an explicit uppercase name (which appends to its lowercase register instead of
    // overwriting, then mirrors the *appended* result).
    //
    // name == '+' or '*' bridges Editor/Clipboard.h's system clipboard instead of this
    // object's own storage -- CopyToSystemClipboard(entry.Joined()) -- and still mirrors
    // into unnamed (matching real vim, which updates "" on any explicit yank/delete
    // regardless of target register).
    void Store(char32_t name, RegisterEntry entry, bool isDelete);

    // name == 0 reads unnamed. '+'/'*' read PasteFromSystemClipboard() live (not cached)
    // -- std::nullopt if the clipboard bridge has nothing/is disabled. std::nullopt for
    // any other unset register.
    [[nodiscard]] std::optional<RegisterEntry> Get(char32_t name) const;

    // vim-macro-register follow-up: a direct, unrouted write -- unlike Store, doesn't
    // mirror into the unnamed register or apply uppercase-append semantics. Real vim's
    // own macro recording (`q{register}...q`) writes its named register exactly this
    // way, distinct from a yank/delete; VimEngine::StopMacroRecording is the sole
    // caller. name must already be a plain lowercase a-z (an uppercase *recording*
    // name's append semantics are handled by VimEngine itself, seeding
    // macroRecordingBuffer_ from the existing register before recording starts, not by
    // this method).
    void SetRaw(char32_t name, RegisterEntry entry);

  private:
    void RouteNamed(char32_t name, RegisterEntry entry);
    void RouteUnnamed(RegisterEntry entry, bool isDelete);
    void SetUnnamed(const RegisterEntry& entry);

    std::map<char32_t, RegisterEntry> registers_; // keys: lowercase a-z, '0'-'9', '-', '"' (unnamed)
};

} // namespace ned::editor::vim

#endif // NED_EDITOR_VIM_VIMREGISTERS_H
