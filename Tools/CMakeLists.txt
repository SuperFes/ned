# Standalone diagnostics -- not part of ned/ned_lib, each answering a real
# terminal-capability question by hand (see each source file's own header
# comment). None depend on ned_lib; most link only notcurses-core.

add_executable(notcurses_alpha_probe NotcursesAlphaProbe.cpp)
target_link_libraries(notcurses_alpha_probe PRIVATE notcurses-core)

# See NotcursesPixelProbe.cpp's own header comment for what it's answering
# (real pixel-graphics support, for a possible minimap feature).
add_executable(notcurses_pixel_probe NotcursesPixelProbe.cpp)
target_link_libraries(notcurses_pixel_probe PRIVATE notcurses-core)

# See NotcursesMinimapProbe.cpp's own header comment: exercises the exact
# small-owned-plane blit path Minimap::EnsurePixelPlane() uses, at real
# minimap cell dimensions, next to Notcurses' own non-pixel blitters for a
# direct comparison.
add_executable(notcurses_minimap_probe NotcursesMinimapProbe.cpp)
target_link_libraries(notcurses_minimap_probe PRIVATE notcurses-core)

# See NotcursesGradientProbe.cpp's own header comment: a paged probe for
# translucency (background/foreground/overlapping, and what Notcurses' 2-bit
# cell alpha can and cannot express) next to gradients in every direction,
# opacity-only and colour+opacity both.
add_executable(notcurses_gradient_probe NotcursesGradientProbe.cpp)
target_link_libraries(notcurses_gradient_probe PRIVATE notcurses-core)

# Links against nothing at all -- see TerminalImageAlphaProbe.cpp's header
# comment: it speaks the iTerm2/kitty/sixel graphics protocols directly, to
# test per-pixel alpha handling that Notcurses itself cannot reach (3.0.17
# has no iTerm2 backend).
add_executable(terminal_image_alpha_probe TerminalImageAlphaProbe.cpp)

# See NotcursesLayerProbe.cpp's own header comment: whether layered ncplanes
# can produce a background that is translucent to the desktop, or only to
# Notcurses' own idea of a backdrop.
add_executable(notcurses_layer_probe NotcursesLayerProbe.cpp)
target_link_libraries(notcurses_layer_probe PRIVATE notcurses-core)
