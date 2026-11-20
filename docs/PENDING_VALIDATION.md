# Pending validation

Owner: lanmower. Remote: none. Push is blocked on an owner-provided remote; nothing has been pushed and no remote was created.

## Phase A changes (each committed as lanmower)

| Commit | Change | Witness |
|---|---|---|
| 9ca0bfb | Baseline: git init, .gitignore for build/ and runs/ | git log |
| c9aa669 | Python triple-quote blanking, getenv dedupe, architecture tree rooted at main and bin with exports | witness 12/12 at that commit |
| be0db38 | Static link (THORNS_STATIC) and size_gate target | build prints size gate line |
| 03fc247 | tests/witness.sh, tests/parity.sh, fixtures | script output |
| a1b2fea | PLAN.md known gaps and build sections | n/a |
| 9 commits after a1b2fea | Markdown sections, call graph, storage sql, default ignore list, nested repo and venv skip | witness, ignore witness, parity 14/14 |
| be6f1cd, 7d61534, 82d1a18 | Ignore witness and fixtures; .playwriter/ ignored; PENDING doc | ignore witness 13 files |
| (this pass, grouped) | Remove deoxidene dependency (deoxidene is not present on this machine; local parsePositive replaces parse_positive_int); --budget counts characters with section-heading and dropped-line accounting; Python calls (function spans for def bodies); Python absolute imports resolve to local modules, unresolved ones count as external | witness 14/14, parity 18/18, ignore witness 13 files |
| (this pass) | tests/fixtures/pkg, Python witnesses, parity checks for budget, verify-setup analyze, Python calls | same |

## Phase B validation

| Step | Wall time | Result |
|---|---|---|
| Clean configure and build, static, with size gate | 7288 ms (measured as 7288 ms in the run) | -- size gate: 1686408 bytes within 2097152 |
| witness, ignore witness, parity, file | 195 ms | RESULT pass=14 fail=0
RESULT pass=1 fail=0
RESULT pass=18 fail=0 (fail = open parity gaps)
build/thorns: ELF 64-bit LSB executable, x86-64, version 1 (SYSV), statically linked, Buil |

Defects found and fixed in Phase B: deoxidene was missing from the machine, so the build failed on its include; the dependency was removed (one function used). The default ignore list dropped bin, which the Node expectations keep. Nested git repos and venvs were walked; now skipped. The budget was measured in bytes instead of characters, and the dropped-section line was unbounded; fixed.

## Open

- Node verify-setup.mjs: only the "analyze current directory" check is applicable to the C++ binary (covered). Its other checks test the Node repo's own files (bun config, shell scripts, executable bit) and are not C++ behaviour.
- Mac, Windows and arm builds are not produced here. No CI config exists.
- Push: blocked on an owner-provided remote.

REPORT: open — push blocked on an owner-provided remote; cross-platform builds not produced — witness: tests/witness.sh RESULT pass=14 fail=0; tests/ignore_witness.sh RESULT pass=1 fail=0; tests/parity.sh RESULT pass=18 fail=0
