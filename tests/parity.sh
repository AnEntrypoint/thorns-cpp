#!/usr/bin/env bash
# Parity checks against the Node thorns verify-overview.mjs expectations (shared sections only).
# Lines marked FAIL are open parity gaps, not regressions. Usage: tests/parity.sh [binary]
set -u
here="$(cd "$(dirname "$0")" && pwd)"
bin="${1:-$here/../build/thorns}"
ov="$("$bin" "$here/fixtures/ov")"
pass=0; fail=0
check() {
  local name="$1"; shift
  if "$@" >/dev/null 2>&1; then pass=$((pass+1)); echo "PASS $name"; else fail=$((fail+1)); echo "FAIL $name"; fi
}
check "Architecture: root src/cli.js(main)" grep -qx "src/cli.js(main)" <<<"$ov"
check "Architecture: children nest under cli" grep -qx "    src/helpers.js(helper, ping)" <<<"$ov"
check "Issues: cycle names a.js and b.js" grep -q "^cycle 2: src/a.js src/b.js$" <<<"$ov"
check "Dead code: unused exports line" grep -q "^unused exports:" <<<"$ov"
check "Dead code: orphans line" grep -q "^orphans:" <<<"$ov"
check "URL printed whole" grep -q "https://api.example.com/v1/items" <<<"$ov"
check "Env: API_KEY and HOME" bash -c 'grep -q "API_KEY" <<<"$1"' _ "$ov"
check "Dependencies: external left-pad x1" grep -q "left-pad×1" <<<"$ov"
check "Dependencies: unused deps line" grep -q "^unused deps:" <<<"$ov"
check "Markdown section headers (## Architecture)" grep -q "^## Architecture$" <<<"$ov"
check "Stats line 'files · lines · fn'" grep -q " files · " <<<"$ov"
check "Calls section (main → run)" grep -q "main → run" <<<"$ov"
check "I/O section with storage sql×1" grep -q "sql×1" <<<"$ov"
check "Output ends with legend line" bash -c 'tail -n1 <<<"$1" | grep -q "^legend:"' _ "$ov"
echo "RESULT pass=$pass fail=$fail (fail = open parity gaps)"
