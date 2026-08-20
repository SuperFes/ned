# Run as Notcurses' FetchContent PATCH_COMMAND (cwd = the fetched source tree).
#
# A legacy terminal (no kitty keyboard protocol, no modifyOtherKeys -- e.g.
# a default tmux) sends Ctrl+Space, and Ctrl+@, as the single NUL byte 0x00.
# Notcurses' own load_ncinput (src/lib/in.c) normalizes C0 control bytes
# 1..26 to Ctrl+letter but leaves 0 untouched, so the event is queued with
# id == 0 -- and notcurses_get's return-value contract uses 0 for "no
# input", making a NUL keypress *unrecoverable* by any caller (EventLoop's
# notcurses_get_nblock drain loop must treat 0 as queue-empty). The only
# place the byte can be saved is inside Notcurses itself, before it's
# queued: normalize it to id ' ' + NCKEY_MOD_CTRL, exactly the shape a
# kitty-protocol or modifyOtherKeys terminal already reports for Ctrl+Space,
# which UI/KeyTranslation.cpp's existing Ctrl branch already translates to
# the C-SPC chord. Verified against v3.0.14's in.c; the FATAL_ERROR below
# fails the configure loudly if an upstream bump ever drifts the anchor
# text, rather than silently shipping the swallowed-C-SPC bug back.
#
# Idempotent: FetchContent can re-run PATCH_COMMAND against an
# already-patched tree (e.g. after a declaration-args change triggers a
# re-populate that reuses the checkout).

set(in_c "src/lib/in.c")
file(READ "${in_c}" content)

if(content MATCHES "ned patch: NUL")
    return()
endif()

set(anchor "  // perform final normalizations
  if(ni->id == 0x7f || ni->id == 0x8){")

set(replacement "  // perform final normalizations
  if(ni->id == 0){ /* ned patch: NUL is a legacy terminal's Ctrl+Space/Ctrl+@ */
    ni->id = ' ';
    ni->modifiers |= NCKEY_MOD_CTRL;
  }else if(ni->id == 0x7f || ni->id == 0x8){")

string(REPLACE "${anchor}" "${replacement}" patched "${content}")

if(patched STREQUAL content)
    message(FATAL_ERROR "PatchNotcursesNulKey.cmake: anchor text not found in ${in_c} -- "
                        "upstream changed load_ncinput's final-normalization block; re-verify the "
                        "NUL (Ctrl+Space) handling and update this patch.")
endif()

file(WRITE "${in_c}" "${patched}")
