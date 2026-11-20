#!/usr/bin/env bash
# Witness checks for thorns-cpp. Usage: tests/witness.sh [path-to-thorns-binary]
set -u
here="$(cd "$(dirname "$0")" && pwd)"
bin="${1:-$here/../build/thorns}"
fx="$here/fixtures"
pass=0; fail=0
check() {
  local name="$1"; shift
  if "$@" >/dev/null 2>&1; then pass=$((pass+1)); echo "PASS $name"; else fail=$((fail+1)); echo "FAIL $name"; fi
}
ov="$("$bin" "$fx/ov")"
py="$("$bin" "$fx/py")"
js="$("$bin" "$fx/js")"
cpp="$("$bin" "$fx/cpp")"
check "cycle a.js<->b.js reported" grep -q "^cycle 2: src/a.js src/b.js$" <<<"$ov"
check "arch root src/cli.js(main)" grep -qx "src/cli.js(main)" <<<"$ov"
check "arch nests app.js exports under cli" grep -qx "  src/app.js(VERSION, run, unusedHelper)" <<<"$ov"
check "arch nests helpers under app" grep -qx "    src/helpers.js(helper, ping)" <<<"$ov"
check "gitignore glob *.min.js and /dist excluded (9 files)" grep -q "9 files · " <<<"$ov"
check "C++ using lines add no spurious deps" bash -c '! grep -qE "external: .*(Clock|Box|Result)" <<<"$1"' _ "$cpp"
check "C++ #include dep kept" grep -q "vector" <<<"$cpp"
check "Python triple-quoted text does not leak imports" bash -c '! grep -q fake_mod <<<"$1"' _ "$py"
check "Python triple-quoted text does not leak defs (2 fn)" grep -q "^functions: 2$" <<<"$py"
check "env var counted once per occurrence" grep -qx "env: HOME×1" <<<"$py"
check "JS arrow and class method and function counted (4 fn)" grep -q "^functions: 4$" <<<"$js"
check "binary is statically linked" bash -c 'file "$1" | grep -q "statically linked"' _ "$bin"
echo "RESULT pass=$pass fail=$fail"
[ "$fail" -eq 0 ]
