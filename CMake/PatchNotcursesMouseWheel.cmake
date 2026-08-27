# Run as Notcurses' FetchContent PATCH_COMMAND (cwd = the fetched source tree).
#
# horizontal-wheel-scroll follow-up. SGR (1006) mouse reporting encodes a
# button as a numeric code Cb: buttons 1-3 (mods < 64) use Cb % 4 == 3 as a
# real sentinel meaning "no button pressed" (used for a bare-motion report
# and for a release, per xterm's own ctlseqs convention). Notcurses' own
# mouse_click() (src/lib/in.c) applies that same "% 4 == 3 means no
# button" check unconditionally, before ever looking at which device group
# Cb falls into -- so Cb=67 (the wheel group's own tilt-right/"wheel
# right" code, 64=wheel-up/65=wheel-down/66=wheel-left/67=wheel-right) gets
# caught by the sentinel branch and reported as NCKEY_MOTION+release
# instead of the NCKEY_BUTTON7 notcurses already defines and would
# otherwise correctly decode via its own "mods >= 64 && mods < 128 ->
# NCKEY_BUTTON4 + (mods % 4)" branch just below. Verified live against a
# real Wayland/foot terminal's SGR mouse report: Cb=66 (wheel-left)
# decodes fine, Cb=67 (wheel-right) does not, confirmed via a raw escape
# sequence and NED_DEBUG_MOUSE. The same bug also swallows Cb=131 (the
# 128-191 "extra buttons" group's own %4==3 slot, NCKEY_BUTTON11) for the
# identical reason, fixed as a side effect of the same condition change.
#
# Fix: only treat mods%4==3 as the "no button" sentinel for the base
# button group (mods < 64) it actually means that in -- every higher
# device group decodes its own Cb%4==3 slot as a real, distinct button via
# the existing group-math branch below, unchanged.
#
# Verified against v3.0.17's in.c; the FATAL_ERROR below fails the
# configure loudly if an upstream bump ever drifts the anchor text, rather
# than silently shipping the swallowed-wheel-right bug back.
#
# Idempotent: FetchContent can re-run PATCH_COMMAND against an
# already-patched tree (e.g. after a declaration-args change triggers a
# re-populate that reuses the checkout).

set(in_c "src/lib/in.c")
file(READ "${in_c}" content)

if(content MATCHES "ned patch: wheel")
    return()
endif()

set(anchor "  if(mods % 4 == 3){
    tni.id = NCKEY_MOTION;
    tni.evtype = NCTYPE_RELEASE;
  }else{")

set(replacement "  if(mods < 64 && mods % 4 == 3){ /* ned patch: wheel -- this sentinel only applies to the base button group */
    tni.id = NCKEY_MOTION;
    tni.evtype = NCTYPE_RELEASE;
  }else{")

string(REPLACE "${anchor}" "${replacement}" patched "${content}")

if(patched STREQUAL content)
    message(FATAL_ERROR "PatchNotcursesMouseWheel.cmake: anchor text not found in ${in_c} -- "
                        "upstream changed mouse_click()'s button-decode block; re-verify the "
                        "wheel-right (Cb=67) handling and update this patch.")
endif()

file(WRITE "${in_c}" "${patched}")
