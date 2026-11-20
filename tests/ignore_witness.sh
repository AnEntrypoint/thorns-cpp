#!/usr/bin/env bash
# Ignore witness: counts the source files the walker keeps for tests/fixtures/ignore (13 expected).
set -u
here="$(cd "$(dirname "$0")" && pwd)"
bin="${1:-$here/../build/thorns}"
out="$("$bin" "$here/fixtures/ignore")"
if grep -q "^13 files · " <<<"$out"; then echo "PASS ignore walk keeps 13 files"; echo "RESULT pass=1 fail=0"; else echo "FAIL ignore walk: $(sed -n 2p <<<"$out")"; echo "RESULT pass=0 fail=1"; exit 1; fi
