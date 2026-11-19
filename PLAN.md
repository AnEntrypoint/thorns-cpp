# thorns-cpp: plan and status

Goal: a C++ rewrite of `../thorns` (Node, tree-sitter, ~70 KB JS plus 12 grammar packages) that keeps the one-shot
overview and cuts size and run time. Boilerplate: `../deoxidene` (header-only C++23, `Result`, arena, pool, jobs).

## Measured (same tree: `/config/workspace/thorns`, Linux x64, g++ 15.2)

| | Node thorns 5.1.15 | thorns-cpp v0 |
|---|---|---|
| Install footprint | `node_modules` 194 MB, plus Node runtime | one binary, 63.8 KB (`-Os`, LTO, gc-sections, stripped), dynamic libstdc++ |
| Wall time, full overview | 441 ms | 12–18 ms (about 25–35x) |
| Output with `--budget 2000` | 2460 B (full) | 1329 B |
| Output identical across two builds | n/a | yes, md5 match between `-O2` and `-Os` LTO builds |

Node's output has architecture tree, issues, dead code and call graph. v0 has counts, hot spots and deps only. The size
comparison is for the feature set v0 actually covers, not for parity.

## Parity matrix

| Section | Node thorns | v0 | Next step |
|---|---|---|---|
| Project context (name, version, entry, description) | yes | yes (substring JSON read) | real JSON parser |
| Languages, file/line counts | yes | yes (lexical, 20 extensions) | — |
| Functions, long functions, branchiness | tree-sitter AST | brace-matching and Python indent heuristics | tree-sitter C API, or per-language lexer |
| Imports / external deps | AST | line-based heuristics | per-language grammar of import forms |
| Env vars, URLs | AST and regex | regex-like scan | — |
| Architecture (import tree from entries) | yes | partial: import graph edges, unresolved, unimported files | import tree rooted at entries, with each module's exports |
| Dead code, orphans | yes | no | needs import graph |
| Call graph | yes | no | needs AST |
| Duplicates, cycles | yes | no | hash of normalised function bodies, Tarjan on import graph |
| `.gitignore` / `.thornsignore` | full (anchored, negated, nested) | directory and bare-name patterns only | glob matcher |

## Measured parity on /config/workspace/thorns (Node thorns 5.1.15 vs thorns-cpp)

| Fact | Node | thorns-cpp | Status |
|---|---|---|---|
| JS source files | 12 | 12 | match |
| JS lines | 2.0k | 1958 | match (rounding) |
| Long function `buildTests` | 126L at verify-overview.mjs:97 | 126 at verify-overview.mjs:97 | match |
| `try` count | 14 | 14 | match |
| Orphan `queries.js` | yes | in unimported list | match |
| Functions | 114 | 114 | match (declarations, methods, and arrow or function values bound by const/let/var) |
| `throw` count | 3 | 3 | match (regex literal bodies are blanked before counting) |
| Unused exports | queries.js(QUERIES) | queries.js(QUERIES) | match (name-based across the project; a name used inside its own file counts as used) |
| Duplicates | assert×2 (verify-overview.mjs, verify-setup.mjs) | assert×2 (same files) | match (normalized token shape, not md5 of the AST) |
| Builtins split from deps | assert, child_process, crypto, fs, os, path, process, url | same set | match |
| Average complexity | cx 2.1 | cx 2.1 (branches 242 / functions 114) | match |

## Design decisions

- Lexical first. One pass per file over a comment/string-blanked copy (`codeView`), then one render. No runtime
  dependency, no tree-sitter. This is where the speed and size come from.
- Budgeted output. Sections are added in priority order and the rest are listed as `dropped (budget)`.
- Tree-sitter vs hand-written, measured (stripped, `-Os -flto -ffunction-sections -fdata-sections`, gc-sections):
  tree-sitter runtime + JavaScript grammar: 432,696 B; runtime + Python grammar: 555,208 B. Each driver only parses a
  file and prints the root child count, so this excludes query and tree-walk code. The full hand-written thorns-cpp
  is 84,264 B for all 20 extensions (it grew from 63,784 B with the graph and ignore engine), and it is dynamically
  linked to libstdc++ (9 ldd lines). Twelve tree-sitter grammars would be roughly 3–6 MB by extrapolation, not measured.
  Decision: keep hand-written scanners for v1. Revisit when a feature needs real syntax trees (call graph, duplicates).
- Deoxidene is consumed through its include path only (`DEOXIDENE_DIR`). Its `Result` is used for `--budget` parsing.
  Nothing else from it is used yet.

## Phases

1. (done) Lexical overview, budget, CMake size profile, measured against Node.
2. Import graph (done, read-only on targets): resolve JS/TS/Python/Rust/C-include specifiers to files. Gives edges,
   unresolved specifiers and unimported files. Still to add: cycles, and the architecture tree from entries.
   Resolution rules:
   - JS/TS: relative specifiers, then extensions .js .mjs .cjs .jsx .ts .tsx .json, `.js` mapped to `.ts`/`.tsx`, then `index.*`.
   - Python: leading dots give the level; `from . import x` resolves to the package `__init__.py` only (submodule names are not read).
   - Rust: `crate::` from `src/`, `self::` and `super::` from the module dir; `mod x;` resolves to `x.rs` or `x/mod.rs`. The
     longest path prefix that is a file wins, so `use crate::a::Item` lands on `a.rs` or `a/mod.rs`.
   - C/C++: quoted includes are tried relative to the file, then from the root, `include/`, `src/` and `third_party/`.
   - Fixture results: 7 edges, 1 unresolved (a missing file), no false edges. deoxidene: 18 edges, 2 unresolved (`quickjs.h`
     is vendored outside the tree, `.tmpparse4.cpp` has an absolute Windows path).
   - Binary grew from 63.8 KB to 80.2 KB with the graph code.
3. Real parsing: tree-sitter C library behind a per-language adapter (only the grammars needed), or hand-written
   per-language scanners if the grammar size dominates the binary. Decide by measuring both.
4. Parity tests: run Node thorns' `verify-*.mjs` expectations against the C++ output for the shared sections.
5. Packaging: static build per platform (mac/win/linux, x64 and arm), size gate in CI.

## Known gaps

Fixed in this pass, with witnesses in `docs/PENDING_VALIDATION.md` and `tests/witness.sh`:

- C++ `using X = ...` and `using` lines no longer produce spurious deps.
- Python triple-quoted strings no longer leak imports, defs or env reads into the overview.
- Env reads are counted once (`os.getenv(` no longer also matches `getenv(`).
- JS arrow functions and class methods are counted (witness: 4 functions in the JS fixture).
- `.gitignore` globs (`*`, `/anchored`) filter files (witness: `*.min.js` and `/dist` excluded).
- The architecture tree is rooted at `main` and every `package.json` `bin` target, and shows each module's exports.

Open (see `tests/parity.sh`, which fails on these by design):

- No Markdown section layout (`## Architecture`, `## Issues`, `## Dead code`, `## I/O`, `## Calls`, `## Dependencies`), no `legend:` line, no `orphans:` or `unused deps:` lines, no call graph, no I/O storage detection.
- Python `from pkg.mod import x` against a local package is counted as an external dep (`src`).
- Git: the repo is initialised locally (baseline commit by lanmower). There is no remote, so nothing is pushed.

## Build

    cmake -S /config/workspace/thorns-cpp -B /config/workspace/thorns-cpp/build -DDEOXIDENE_DIR=/config/workspace/deoxidene
    cmake --build /config/workspace/thorns-cpp/build -j
    /config/workspace/thorns-cpp/build/thorns /path/to/project --budget 2000

The release binary is statically linked (`-DTHORNS_STATIC=OFF` for a dynamic build). A size gate
(`THORNS_MAX_BYTES`, default 2 MiB) runs on every build and fails the build when the binary is larger.

## Tests

    tests/witness.sh   # behaviour witnesses, expected 12 pass
    tests/parity.sh    # Node verify-overview parity, shared sections; FAIL lines are open gaps
