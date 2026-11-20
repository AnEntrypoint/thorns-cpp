# Pending validation

Owner: lanmower. Remote: none. The Phase B push is blocked on an owner-provided remote; nothing has been pushed and no remote has been created.

## Phase A changes (each committed as lanmower)

| Commit | Change | Witness |
|---|---|---|
| 9ca0bfb | Baseline: git init on main, .gitignore for build/ and runs/ | git log shows the baseline |
| c9aa669 | Python triple-quoted text blanked before import, name and URL scans; getenv marker no longer matches inside os.getenv; architecture tree rooted at main and bin targets with exports; entry exports not listed as unused | tests/witness.sh 12/12 at that commit |
| be0db38 | Static link (THORNS_STATIC, default ON); size_gate target against THORNS_MAX_BYTES (default 2 MiB) | build prints "size gate: 1682312 bytes within 2097152" |
| 03fc247 | tests/witness.sh, tests/parity.sh, tests/fixtures | witness and parity output |
| a1b2fea | PLAN.md known gaps and build sections, this file | n/a |
| (next) | Markdown sections (Architecture, Issues, Dead code, Dependencies, I/O, Calls, Metrics), stats line, header with (cli, ESM), orphans, unused deps, external counts with ×N, storage sql, call graph by name, legend, default ignore list (drop bin, add .config, tests/fixtures, *.min.js, /embeddings), skip nested git repos and Python venvs | tests/witness.sh 12/12; tests/parity.sh 14/14; tests/ignore_witness.sh 13 files |
| (next) | .playwriter/ added to .gitignore | git check-ignore |

## Phase B validation

| Step | Wall time | Result |
|---|---|---|
| Clean configure and build, static, with size gate | 7616 ms | -- size gate: 1686408 bytes within 2097152 |
| tests/witness.sh, tests/ignore_witness.sh, tests/parity.sh | 142 ms | RESULT pass=12 fail=0
RESULT pass=1 fail=0
RESULT pass=14 fail=0 (fail = open parity gaps) |

Defects found and fixed in Phase B: the Python triple-quote leak also reached import scanning (fixed by blanking before scans); the default ignore list dropped source directories named bin, which the Node verify-ignore expectations keep (removed); nested git repos and venvs were walked (now skipped); the header and the counts in witness/ignore fixtures were updated to the new layout.

## Open

- Python calls: `execute → helper` is not extracted (call graph covers brace-language functions only).
- Python local package import: `from src.helpers import x` is counted as external `src`.
- `--budget`: Node's verify expects a character budget (budget 400 gives at most 400 characters); this build treats the budget as tokens (×4 characters). Not changed.
- verify-setup.mjs has not been mapped to a C++ check.
- Mac, Windows and arm builds are not produced here. No CI config exists.
- Push: blocked on an owner-provided remote.

REPORT: open — push blocked on owner-provided remote; Python call graph, budget semantics, verify-setup parity open — witness: tests/witness.sh RESULT pass=12 fail=0; tests/ignore_witness.sh RESULT pass=1 fail=0; tests/parity.sh RESULT pass=14 fail=0
