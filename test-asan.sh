#!/usr/bin/env bash
# Same as test.sh, but under -DNED_ENABLE_SANITIZERS=ON (ASan/UBSan) in its
# own build directory (cmake-build-asan/, matching the cmake-build-*/
# pattern .gitignore already excludes) so it never clobbers the normal
# build/ tree. The whole suite is expected to stay clean under this --
# treat any sanitizer finding as a real bug, not noise (see CLAUDE.md).
# Any arguments are passed straight through to the Catch2 binary.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

cmake -S . -B cmake-build-asan -DNED_ENABLE_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build cmake-build-asan -j8 --target ned_tests
exec ./cmake-build-asan/ned_tests "$@"
