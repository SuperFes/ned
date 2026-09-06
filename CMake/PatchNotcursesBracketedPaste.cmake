# Run as Notcurses' FetchContent PATCH_COMMAND (cwd = the fetched source tree).
#
# paste-perf-and-drag-drop follow-up. Notcurses has no bracketed-paste support at
# all (confirmed against v3.0.17: nothing recognizes \x1b[200~/\x1b[201~, the
# terminal's own paste-start/paste-end markers). Without it, a raw terminal paste
# arrives as one ordinary keystroke event per character, and each one re-triggers
# this editor's full per-command repaint/reparse chain -- O(n) reparses for an
# n-character paste. This patch teaches Notcurses' own input automaton to
# recognize the two marker sequences as fixed literal escapes (the same
# registration path prep_xtmodkeys/prep_windows_special_keys already use for
# similarly-shaped literal CSI-tilde sequences -- no numeric parsing needed, since
# \x1b[200~/\x1b[201~ carry no parameters) and surface them as two new NCKEY_*
# sentinels. All buffering of the pasted text between the two markers happens in
# ned's own EventLoop::Run(), not here -- Notcurses only needs to hand back two
# more special keys, exactly like every existing one.
#
# NCKEY_PASTE_BEGIN/END are placed at preterunicode(220)/(221) -- in the real gap
# between NCKEY_BUTTON11 (211) and NCKEY_SIGNAL (400), and (deliberately) still
# <= NCKEY_EOF (500), so nckey_synthesized_p() keeps classifying them as
# synthesized like every other special key, same as everything else in this range.
#
# Verified against v3.0.17's nckeys.h/in.c; each FATAL_ERROR below fails the
# configure loudly if an upstream bump ever drifts its own anchor text, rather
# than silently shipping a no-op patch.
#
# Idempotent: FetchContent can re-run PATCH_COMMAND against an already-patched
# tree (e.g. after a declaration-args change triggers a re-populate that reuses
# the checkout). The two halves below are independently marker-gated so a partial
# re-apply (e.g. only one of the two files somehow reverted) still self-heals.

set(nckeys_h "include/notcurses/nckeys.h")
file(READ "${nckeys_h}" nckeys_content)

if(NOT nckeys_content MATCHES "ned patch: paste keys")
    set(nckeys_anchor "#define NCKEY_BUTTON11  preterunicode(211)

// we received SIGCONT
#define NCKEY_SIGNAL    preterunicode(400)")

    set(nckeys_replacement "#define NCKEY_BUTTON11  preterunicode(211)

// ned patch: paste keys -- bracketed-paste start/end markers, registered as
// fixed literal escapes in in.c's prep_bracketed_paste_keys. Placed here,
// under NCKEY_EOF below, so nckey_synthesized_p() still classifies them as
// synthesized like every other special key.
#define NCKEY_PASTE_BEGIN preterunicode(220)
#define NCKEY_PASTE_END   preterunicode(221)

// we received SIGCONT
#define NCKEY_SIGNAL    preterunicode(400)")

    string(REPLACE "${nckeys_anchor}" "${nckeys_replacement}" nckeys_patched "${nckeys_content}")

    if(nckeys_patched STREQUAL nckeys_content)
        message(FATAL_ERROR "PatchNotcursesBracketedPaste.cmake: anchor text not found in ${nckeys_h} -- "
                            "upstream changed the NCKEY_BUTTON11/NCKEY_SIGNAL block; re-verify placement "
                            "and update this patch.")
    endif()

    file(WRITE "${nckeys_h}" "${nckeys_patched}")
endif()

set(in_c "src/lib/in.c")
file(READ "${in_c}" in_c_content)

if(NOT in_c_content MATCHES "ned patch: paste")
    set(in_c_anchor "static int
prep_all_keys(inputctx* ictx){
  if(prep_windows_special_keys(ictx)){
    return -1;
  }
  if(prep_kitty_special_keys(ictx)){
    return -1;
  }
  if(prep_xtmodkeys(ictx)){
    return -1;
  }
  return 0;
}")

    set(in_c_replacement "// ned patch: paste -- \\x1b[200~/\\x1b[201~ are fixed literal escapes (SGR
// bracketed-paste markers), the same shape prep_xtmodkeys/
// prep_windows_special_keys already register via inputctx_add_input_escape;
// no numeric CSI-tilde parsing involved.
static int
prep_bracketed_paste_keys(inputctx* ictx){
  static const struct {
    const char* esc;
    uint32_t key;
    unsigned modifiers;
  } keys[] = {
    { .esc = \"\\x1b[200~\", .key = NCKEY_PASTE_BEGIN, },
    { .esc = \"\\x1b[201~\", .key = NCKEY_PASTE_END, },
    { .esc = NULL, .key = 0, },
  }, *k;
  for(k = keys ; k->esc ; ++k){
    if(inputctx_add_input_escape(&ictx->amata, k->esc, k->key, k->modifiers)){
      return -1;
    }
    logdebug(\"added %s %u\", k->esc, k->key);
  }
  loginfo(\"added bracketed paste keys\");
  return 0;
}

static int
prep_all_keys(inputctx* ictx){
  if(prep_windows_special_keys(ictx)){
    return -1;
  }
  if(prep_kitty_special_keys(ictx)){
    return -1;
  }
  if(prep_xtmodkeys(ictx)){
    return -1;
  }
  if(prep_bracketed_paste_keys(ictx)){
    return -1;
  }
  return 0;
}")

    string(REPLACE "${in_c_anchor}" "${in_c_replacement}" in_c_patched "${in_c_content}")

    if(in_c_patched STREQUAL in_c_content)
        message(FATAL_ERROR "PatchNotcursesBracketedPaste.cmake: anchor text not found in ${in_c} -- "
                            "upstream changed prep_all_keys's body; re-verify the prep_*_keys chain and "
                            "update this patch.")
    endif()

    file(WRITE "${in_c}" "${in_c_patched}")
endif()
