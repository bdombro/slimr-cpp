# Package consumption and publication status

The thirteen flat `packages/<key>/` trees are independently consumable vcpkg overlay ports. Each contains its manifest, header, CMake config, ISC license, and any owned vendor files. Original code is ISC; bundled upstream notices remain in the distributed vendor headers/licenses.

All thirteen packages are published in the public Git registry. Baseline `91b48cf91739dc622f97c6dd8f0c0f54c367ce43`, source commit `bdf5b5c786aec6dc5aae5c0d54561667529dcc97`, contains `slimr-proc` `0.1.2` and the other packages at `0.1.1`. Releases passed Debug, Release and lint checks before version writes. The real Brave history MCP resolved, built and passed tests against both registry baselines with local overlays disabled. See the [consumer configuration](../README.md#dependencies).

## One publishing entry point

```sh
# Safe previews: no checks, writes, commits or pushes.
just publish --dry --include sqlite,json
just publish --dry --local --all

# Local overrides only.
just publish --local --include sqlite,json
just publish --local --all
just publish --local --undo --include sqlite,json
just publish --local --undo --all

# Registry releases: run ONLY with explicit commit/push authorization.
just publish --include sqlite,json
just publish --dirty
just publish --all
```

Exactly one selector is required: `--include` takes distinct comma-separated package directory keys, `--dirty` selects packages with staged, unstaged or untracked changes, and `--all` selects all packages. `--undo` requires `--local`. There are no package-level publishing tasks or separate local wrappers.

Both dry and normal execution use the same read-only planner and print its plan. Registry previews include the release set, affected-dependent reasons, old/new versions, dependency minimum changes, checks, metadata paths, intended commits and configured push destination. Future tree hashes and the baseline SHA are unknown until committed; a dry run is not evidence that checks pass.

HTTP destination previews omit credentials, query parameters and fragments. The actual push uses the configured remote, not the displayed URL. Unexpected Git configuration errors fail explicitly rather than appearing as a missing upstream.

Registry mode includes the transitive reverse-dependent closure, including dependencies in optional features. Every release-set package receives a patch bump from its manifest's numeric `version`; constraints on released dependencies become `version>=` requirements for their new versions. Dependency options/features are preserved. Unsupported version schemes, conflicting/non-increasing history entries and ambiguous preexisting `versions/` edits fail before release writes.

For every package in the release set, the publisher runs `just test-pkg <key>`, `just build-pkg <key>` and `just lint-pkg <key>`. All checks must pass before any version, metadata or release commit changes. These are ordinary Debug tests, optimized Release builds/tests and first-party package lint; there is no downstream app smoke-test framework.

After checks, the publisher writes versions/constraints and commits only the release-set package paths. It then reads the real committed package trees, prepends immutable entries in `versions/s-/slimr-<key>.json`, preserves existing history and unrelated baseline entries, and commits the exact registry metadata paths. Missing first-release metadata is initialized only at this stage, never from uncommitted trees. Both commits include the Copilot coauthor trailer. Unrelated working-tree and staged changes are neither stashed nor included.

The current branch must have a configured upstream remote/branch and exactly one push URL. The publisher pushes `HEAD` explicitly to that configured branch and reports the full metadata commit as the consumer baseline only after push success. Inspect the preview carefully: existing branch history is also pushed, not just the two new commits.

When the default branch is protected, publish from a release branch with a configured upstream and merge its pull request after required checks pass. The publisher does not bypass branch rules.

On failure, the publisher reports its phase, known source/metadata commit SHAs, observed HEAD and push outcome. Version writes, staged changes or local commits may remain; a failed push can have an uncertain remote outcome. Nothing claims to roll back. Inspect the tree, index and remote before retrying; do not blindly rerun a partially completed release.

Focused automation validation (no real publishing):

```sh
node --test scripts/publish.test.mjs
just --list
```

The automation tests create disposable repositories and bare remotes under `scripts/`, stub package checks, and remove their fixtures. They are separate from the real package release and app-consumption checks recorded above.

## Local environment

```sh
export VCPKG_ROOT="$HOME/.local/share/vcpkg"
export VCPKG_OVERLAY_PORTS="$VCPKG_ROOT/local-ports"
```

`just publish --local` prepares that one dedicated overlay directory with named symlinks, for example `slimr-sqlite -> /checkout/slimr-cpp/packages/sqlite`. Path lists are rejected. The complete selection is validated before any link mutation. The selected `<overlay>/<manifest-name>` paths are the ownership boundary: existing correct links are left alone, explicit local enable replaces other symlinks (including dangling ones), and undo unlinks only selected named symlinks. Link targets are never modified. Already-absent undo is safe. Every selected nonlink is rejected before any mutation; unselected paths are untouched. No ownership markers or state files are used.

The overlay must be separate from this source checkout, must not have symlinked ancestors, and must not be a vcpkg source/installed/cache directory. Local mode changes only selected overlay links: no checks, versions, registry metadata, commits, pushes, source discovery, installed headers or cache edits. Registry mode never changes local overrides. Keep generated build files out of port trees. Neither consumers nor this repo override the environment variables or search for checkouts.

Overrides affect every consumer sharing the environment. After enabling/removing overrides or switching architectures, use fresh consumer build/install output and explicitly re-resolve dependencies. Normal edits while staying local use the automatic `deps` refresh, without cleaning or disabling the binary cache. Published mode was verified with separate generated output so it did not disturb the local development install.

## Consumers

Declare only direct `slimr-*` dependencies in the app's `vcpkg.json`.
Optional `tests` features bring in `slimr-test` without adding it to production
dependency closures. A minimal CMake consumer uses installed targets:

```cmake
cmake_minimum_required(VERSION 3.31)
project(example LANGUAGES CXX)
find_package(slimr-sqlite CONFIG REQUIRED)
add_executable(example main.cpp)
target_link_libraries(example PRIVATE slimr::sqlite)
```

`slimr::sqlite` brings JSON and static FTS5-enabled SQLite. Curl WebSocket support
is explicit in the `slimr-ws` manifest. All thirteen packages install a
`share/slimr-<key>/slimr-<key>-config.cmake` with matching `slimr::<key>` targets.
Configs find transitive dependencies and resolve includes relative to their own
installed prefix; no checkout paths are exported. Safety instrumentation belongs
to first-party development targets, not these public interfaces.

Install with `vcpkg install --triplet=<host-triplet> --x-install-root=build/deps`.
Configure CMake with vcpkg's toolchain, `VCPKG_MANIFEST_MODE=OFF`,
`VCPKG_MANIFEST_INSTALL=OFF`, `VCPKG_INSTALLED_DIR=<absolute>/build/deps`
and the same `VCPKG_TARGET_TRIPLET`.

The intended normal consumer cycle is source edit → automatic `deps` prerequisite
→ refreshed installed headers → CMake/Ninja rebuild. Verified locally with a
copied port behind a named overlay symlink: editing only the header changed
vcpkg's package ABI and observable compiled behavior without cleaning or disabling
the binary cache. Broken/missing overrides must fail; they must not silently reuse
installed stale snapshots. Switching between local and published sources
requires explicit re-resolution and verification.

## Library development

Use the root commands documented in [AGENTS.md](../AGENTS.md):

```sh
just deps
just test
just test-pkg sqlite
just build
just build-pkg sqlite
just lint
just lint-pkg sqlite
```

Root development installs only native dependencies, so source self-tests never
accidentally consume installed copies of slimr. Package-local CMake declarations
own their dependency/include requirements. Root CTest covers the aggregate and
all thirteen standalone suites in both Debug and Release.

All generated output is under `build/deps/` and `build/cmake/{Debug,Release}/`.
vcpkg's binary cache shares package builds, not a writable shared include tree.
Nothing installs executables globally.
