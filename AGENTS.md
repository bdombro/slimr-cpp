# AGENTS.md (`slimr-cpp`)

Written for: AI coding agents working in the `slimr-cpp` C++26 monorepo.

`slimr-cpp` is a header-only C++26 library monorepo organized into flat, standalone packages under `packages/<pkg>/`. Include `<pkg>.hpp` directly; there is no forwarding-header layer.

## Commands

| Task | Command |
| --- | --- |
| Run full test suite (ASan + UBSan) | `just test` |
| Run a single package's test suite | `just test-pkg <pkg>` (e.g. `just test-pkg sqlite`) |
| Configure Debug/Release | `just configure [Debug\|Release]` |
| Build and test optimized Release (O3 + IPO) | `just build` |
| Build and test one package in Release | `just build-pkg <pkg>` |
| Analyze all first-party test translation units | `just lint` |
| Analyze one package's test translation unit | `just lint-pkg <pkg>` |
| Preview registry/local changes without checks or mutation | `just publish --dry [--local] --include sqlite,json` |
| Enable/remove selected local overlay links | `just publish --local [--undo] --include sqlite,json` |
| Validate publishing automation in isolated Git fixtures | `node --test scripts/publish.test.mjs` |
| Clean build artifacts | `just clean` |
| Install dependencies | `just deps` |

## Package Structure & Conventions

Every package under `packages/<pkg>/` must contain:
- `<pkg>.hpp` — the self-contained `#pragma once` header in namespace `slimr` (or `slimr::<subns>`).
- `test.cpp` — standalone `doctest` test suite.
- `CMakeLists.txt` — source target and standalone CTest declarations.
- `vcpkg.json` — production dependencies and an optional `tests` feature.
- `portfile.cmake` — installs this package's header, owned vendor files, license and relocatable config.
- `slimr-<pkg>-config.cmake` — dependency-aware installed `slimr::<pkg>` target.
- `LICENSE` — ISC license for original code; vendor notices retain their original terms.
- `README.md` — concise usage guide and API reference for the package.

And at the monorepo root:
- `tests/test_main.cpp` — unified test runner exercising all packages together.
- `vcpkg.json` — native development dependencies only (SQLite FTS5, curl WebSockets).
- `CMakeLists.txt` — C++26, CTest, development-only safety settings and package ordering.
- `justfile` — thin CMake/Ninja orchestration with automatic `deps` prerequisites.
- `.clang-tidy` — curated first-party analyzer and bug-prone checks.

Consumers declare `slimr-<pkg>` dependencies in vcpkg manifests, use `find_package(slimr-<pkg> CONFIG REQUIRED)`, and link `slimr::<pkg>`. Source and installed targets have the same names. Root self-tests use source targets exclusively, never installed slimr snapshots. Each package owns its includes/link dependencies; Glaze, rapidyaml, and doctest belong to json, yaml, and test. Do not mutate installed headers or caches. Local overlays use named links such as `$VCPKG_ROOT/local-ports/slimr-json` to individual package folders.

The environment owns `VCPKG_ROOT` (bootstrapped checkout) and `VCPKG_OVERLAY_PORTS=$VCPKG_ROOT/local-ports`; recipes do not override them. Put `vcpkg`, CMake 3.31+, Ninja, just, zsh and a C++26 compiler on PATH. The verified host is Apple clang 21/macOS ARM64; host triplet detection alone does not establish Linux portability. Set `CLANG_TIDY` to a compatible executable when not on PATH. Missing tools or diagnostics fail visibly. Generated files live under `build/deps/` and `build/cmake/{Debug,Release}/`. Configure/build/test/lint automatically run `deps`; CMake manifest auto-install is OFF. Native `--x-wait-for-lock` serializes concurrent installs sharing vcpkg's buildtrees. Preserve `zsh -f` in the recipe shell: user startup files otherwise can overwrite inherited overlay variables. `test-pkg` validates its selector before configuring. Every CTest command uses `--no-tests=error`, every suite has a finite timeout and a repo-local `TMPDIR`.

The private registry is not published: do not claim clone-free remote resolution works or invent a baseline. The single root `publish *args` forwards to `scripts/publish.mjs`; there are no per-package justfiles or separate local publishing recipes. Select exactly one of `--include key,key`, `--dirty`, or `--all`; `--undo` requires `--local`. Dry runs use the same read-only plan as normal execution but skip checks and all mutations. Local mode only changes selected named overlay symlinks: enable can replace another symlink, undo can unlink it, targets are untouched, and all selected nonlinks are rejected before mutation. No ownership markers, installed-header/cache changes or registry writes.

Registry execution requires explicit release authorization. It includes affected reverse dependents (including feature dependencies), plans patch bumps/dependency minimums, and requires every release-set package's `test-pkg`, `build-pkg` and `lint-pkg` checks before any release writes. It commits exact package paths first, generates metadata from real committed package trees, preserves previous history/baseline entries, commits exact metadata paths, then pushes the configured upstream. Preserve unrelated working/staged changes; reject preexisting registry metadata edits rather than mixing them into a release. Include the required Copilot coauthor. Report partial failures with known commit SHAs and push uncertainty; never claim rollback or blindly retry. See [Publishing](docs/publishing.md) for procedures and current gates. Automation fixture tests are not a real release or a passing C++ check gate.

Original code is ISC; retain bundled upstream notices and verify redistribution licensing before an authorized release. Switching overlays affects all consumers sharing the environment: clean consumer-generated output and explicitly re-resolve when switching local/published sources or architectures, not for ordinary local edit refresh.

`CXX` selects the compiler on first configure (default `/usr/bin/clang++` on macOS, `clang++` elsewhere). CMake then retains its cached absolute compiler; adding clang-tidy to PATH must not reselect the build compiler. Use a fresh configuration tree when intentionally changing compilers.

## Native CI and Fuzzing

`.github/workflows/ci.yml` runs root tests, optimized builds and lint on Linux/macOS for PRs, main pushes and merge groups, with a 45-minute job timeout. Set repository variable `VCPKG_REF` to a tested full 40-character commit; no slimr checkout/token is needed for this repo's source tests. CI uses Clang/libc++ 21 and `llvm@21` on macOS. Remote runner execution and required branch rules must be verified separately; workflow syntax validation is not a passing CI run.

`BUILD_FUZZING=ON` adds `fuzz_json`, `fuzz_yaml` and `fuzz_encoding`, with target-local libFuzzer instrumentation plus the existing ASan/fatal UBSan protections. It requires `ENABLE_SANITIZERS=ON` and a Clang distribution shipping libFuzzer (the host's Apple clang 21 does not; Homebrew LLVM does). See the [README](README.md#native-ci-and-bounded-fuzzing) for local commands. Linux CI lints all three fuzz translation units, then runs each for 15 seconds with a two-second per-input timeout, 1024-byte input limit and 2 GiB RSS limit. Corpus caches carry useful inputs forward; artifacts preserve crashes and corpora for seven days. Fixed findings need focused deterministic regressions.

## C++ Memory Safety

For new or modified first-party C++:
- Wrap owned C resources immediately in RAII (SQLite handles/statements/backups, curl handles/lists, file descriptors, spawn actions). Use containers/smart pointers for C++ ownership. Document C-API ownership transfers and failure-path cleanup.
- Borrow `string_view`, `span`, pointers and references for the current call by default. Store owning values unless the longer-lived owner's lifetime/invalidation rules are explicit. Deferred callbacks need explicit captures with valid ownership; no escaping reference captures without a demonstrated lifetime guarantee.
- Initialize scalar/pointer members at declaration and local scalars before use. Compiler auto-initialization is defense in depth, not permission to read uninitialized values.
- Check range/overflow before narrowing lengths/counts at C boundaries (especially `size_t` to SQLite `int`); a cast is not validation. Use the existing error mechanism.
- Resource owners are noncopyable unless copying creates independent ownership. Moves must transfer ownership and leave the source safe to destroy.
- Never let C++ exceptions escape C-library callbacks. Translate them into the documented error/abort mechanism and propagate/report at the C++ boundary; do not silently swallow failures.

Preserve `-ftrivial-auto-var-init=zero`, verified libc++ FAST hardening, and compile/link ASan + fatal UBSan when `ENABLE_SANITIZERS=ON`. Use `just test`, `just test-pkg <pkg>`, `just build`, and `just lint`; these guardrails do not prove memory safety. Existing wrappers are not fully compliant: curl request callbacks can throw during string/map allocation; curl handles are manually cleaned on some paths; SQLite binding narrows lengths without validation; SQLite `return_statement` allocates a string inside `noexcept`. Treat these as follow-up work, not permission to copy those patterns. No broader wrapper rewrite is part of this build migration.

## Important Implementation Rules

1. **`glz::generic_i64` (`slimr::Json`) brace initialization:**
   Never write `Json{std::move(obj)}` when `obj` is `Json::object_t` or `Json::array_t` — `glz::generic` has an `std::initializer_list` constructor that wraps the value inside a 1-element JSON array! Always use `slimr::json_obj(std::move(obj))` / `slimr::json_arr(std::move(arr))` or assign `j.data = std::move(obj)`.
2. **`rapidyaml` (`ryml_all.hpp`) node checks in `packages/yaml`:**
   In `ryml`, a key-value map child (`key: val`) has both `is_keyval() == true` and `has_val() == true`. When converting a `c4::yml::ConstNodeRef` to `slimr::Json`, check `node.is_map()` and `node.is_seq()` appropriately, and for scalar values check `node.has_val()`.
3. **Sanitizer cleanliness:**
   Follow C++ Memory Safety above and pass `just test` with zero sanitizer findings.
4. **Zero shell execution:**
   `packages/proc` uses `posix_spawnp` + `poll()` directly and must never invoke `/bin/sh` or `popen`.
