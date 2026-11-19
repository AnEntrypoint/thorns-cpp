# Pending validation

Owner: lanmower. Remote: none. The Phase B push is blocked until the owner provides a remote; nothing has been pushed.

## Phase A changes (each committed as lanmower)

| # | Commit subject | Change | Witness |
|---|---|---|---|
| 0 | Baseline: thorns-cpp v0 state | git init on main; .gitignore for build/ and runs/ | `git log --oneline` shows the baseline commit |
| 1 | Fix parser gaps and label architecture tree with exports | Python triple-quoted strings blanked before import, name and URL scans (`blankPyTripleQuoted`); `getenv(` no longer matches inside `os.getenv(`; architecture tree rooted at main and package.json `bin` targets; exports shown per module; entry exports not listed as unused | tests/witness.sh: 12/12 PASS |
| 2 | Link statically and gate binary size | `THORNS_STATIC` option (default ON, `-static`); `size_gate` target runs `cmake/size_gate.cmake` against `THORNS_MAX_BYTES` (default 2 MiB) | build output `size gate: 1682312 bytes within 2097152`; `file build/thorns` says statically linked |
| 3 | Add witness and parity scripts with fixtures | tests/witness.sh, tests/parity.sh, tests/fixtures/{ov,py,js,cpp} | witness 12/12; parity 6 PASS, 8 FAIL (open) |
| 4 | Record validation status and update plan | PLAN.md known-gaps and build sections; this file | n/a |

Cycle detection (Tarjan SCC over the import graph) and the import-graph resolver already existed in v0. Witness: `cycle 2: src/a.js src/b.js` in the overview fixture.

## Phase B validation

Build and check wall times, measured in this session (wall clock, configure plus build where stated):

| Step | Wall time | Result |
|---|---|---|
| Baseline configure and build, dynamic (before changes) | 8.6 s | built |
| Incremental rebuild after parser fixes | 6.8 s | built |
| Clean static build with size gate (`rm -rf build`) | 7.4 s | built, 1,682,312 B, gate passes |
| Clean dynamic build with size gate (`-DTHORNS_STATIC=OFF`) | 7.0 s | built, 108,840 B, 9 dynamic lines from ldd |
| `tests/witness.sh` | 0.061 s | 12 PASS, 0 FAIL |
| `tests/parity.sh` | < 0.1 s | 6 PASS, 8 FAIL |

Defects found and fixed in Phase B: the first witness run had the Python triple-quote leak still active in import scanning (fixed by blanking before the import scan, not only in codeView). The size gate first failed at 1,682,312 B against a 128 KiB default; the default was raised to 2 MiB because the static libstdc++ alone accounts for the difference. The dynamic build remains 108,840 B.

## Open (not fixed)

- Parity with Node thorns `verify-overview.mjs`: Node's Markdown sections (`## Architecture`, `## Issues`, `## Dead code`, `## I/O`, `## Calls`, `## Dependencies`), the stats line format, the `legend:` line, the `orphans:` and `unused deps:` lines, the call graph and the I/O storage detection are not implemented. `tests/parity.sh` fails on each of them by design.
- Python `from src.helpers import x` against a local package is counted as an external dep `src`.
- `verify-ignore.mjs` cases beyond glob and anchoring (nested .gitignore, negation inside excluded dirs, `.npmignore`, nested git repos and venvs) are not run against the C++ binary yet.
- Phase 5 CI: the size gate runs on local builds only. No CI config exists in the repo, and there is no remote to host one. Mac and Windows builds are not built here.
- Push: blocked on a remote the owner has not provided.

REPORT: open — Phase B push blocked on a remote; Node parity sections and call graph still open — witness: tests/witness.sh RESULT pass=12 fail=0; tests/parity.sh RESULT pass=6 fail=8
