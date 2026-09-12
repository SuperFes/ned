#!/usr/bin/env bash
set -euo pipefail

total() {
    local sum=0
    for value in "$@"; do
        sum=$(( sum + value ))
    done
    echo "$sum"
}

if [ "$#" -gt 0 ]; then
    total "$@"
fi
