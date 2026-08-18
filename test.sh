#!/usr/bin/env bash
# Configures/builds ned_tests (if needed) and runs it. Any arguments are
# passed straight through to the Catch2 binary, e.g.:
#   ./test.sh "[TreeSitter]"
#   ./test.sh "Query::Captures*"
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

cmake -S . -B build
cmake --build build -j8 --target ned_tests
exec ./build/ned_tests "$@"
