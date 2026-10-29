#!/usr/bin/env bash
# Builds a release package on the distribution it runs on: ned with patched
# Notcurses and Janet linked in (NED_BUNDLE_LIBS), its man pages, and the
# static agents (see CMake/Packaging.cmake).
#
#   Packaging/build-package.sh DEB|RPM|AppImage <agents dir> <output dir>
#
# The agents dir holds <arch>/ned-agent. The build goes to
# $NED_PACKAGE_BUILD_DIR, by default build-package/ in the source tree. An
# AppImage needs linuxdeploy and its appimage plugin, as $LINUXDEPLOY or on
# $PATH.
set -euo pipefail

usage() {
    echo "usage: $0 DEB|RPM|AppImage <agents dir> <output dir>" >&2
    exit 2
}
[ $# -eq 3 ] || usage
kind=$1
case $kind in DEB | RPM | AppImage) ;; *) usage ;; esac
agents=$(realpath "$2")
mkdir -p "$3"
out=$(realpath "$3")
src=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
build=${NED_PACKAGE_BUILD_DIR:-$src/build-package}

cmake -S "$src" --preset bundled -B "$build" \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DNED_BUILD_TESTS=OFF \
    -DNED_BUILD_MAN=ON \
    -DNED_PACKAGE_AGENTS="$agents"
cmake --build "$build" -j"$(nproc)"
version=$("$build/Source/ned" --version | awk '{print $2}')

case $kind in
DEB | RPM)
    (cd "$build" && cpack -G "$kind" -B "$out")
    ;;
AppImage)
    mkdir -p "$build/appimage"
    appdir=$(mktemp -d "$build/appimage/AppDir.XXXXXX")
    DESTDIR=$appdir cmake --install "$build"
    # The ned-* tools are symlinks onto ned; only real binaries need their
    # libraries gathered.
    executables=()
    for exe in "$appdir"/usr/bin/*; do
        [ -L "$exe" ] || executables+=(--executable "$exe")
    done
    # linuxdeploy is itself an AppImage; a container has no FUSE to mount it.
    # ARCH because the other architectures' agents make the AppDir's
    # ambiguous.
    (cd "$out" && APPIMAGE_EXTRACT_AND_RUN=1 ARCH=$(uname -m) LDAI_OUTPUT="ned-$version-$(uname -m).AppImage" \
        "${LINUXDEPLOY:-linuxdeploy}" --appdir "$appdir" "${executables[@]}" \
        --desktop-file "$appdir/usr/share/applications/ned.desktop" \
        --icon-file "$appdir/usr/share/icons/hicolor/scalable/apps/ned.svg" \
        --output appimage)
    ;;
esac
