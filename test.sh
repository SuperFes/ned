#!/usr/bin/env bash
# Configures/builds ned_tests (if needed) and runs it. Any arguments are
# passed straight through to the Catch2 binary, e.g.:
#   ./test.sh "[Grammar]"
#   ./test.sh "Query::Captures*"
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

cmake -S . -B build
cmake --build build -j4 --target ned_tests
ctest --test-dir build -j8
exec ./build/Tests/ned_tests "$@"
