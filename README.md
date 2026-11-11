# 🪶 slimr-cpp monorepo

`slimr-cpp` is a monorepo of 13 slim, header-only **C++26** libraries for building fast, zero-bloat Model Context Protocol (MCP) servers, narrow JSON CLIs, SQLite + FTS5 applications, and HTTP/WebSocket clients.

Inspired by the ergonomics of TypeScript/Node [`@slimr`](https://github.com/bdombro/slimr) and [`argsbarg`](https://github.com/bdombro/argsbarg), `slimr-cpp` brings single-source-of-truth schema reflection, safe subprocess/socket primitives, and production SQLite defaults to native C++26, without a Node/V8 runtime. Binary size depends on the libraries used and whether native dependencies are linked statically.

---

> **Audience and compatibility:** this is a developer library, not an installed
> end-user MCP service. Current MCP source supports **2026-07-28 only** and has
> no Gateway or legacy initialize API. The published registry baseline below
> is an older package release, not proof that July HTTP packages are published.


## Packages

| Package | Purpose |
| --- | --- |
| [json](packages/json/README.md) | JSON parsing and serialization |
| [reflect](packages/reflect/README.md) | Typed reflected tool schemas |
| [yaml](packages/yaml/README.md) | YAML and Markdown frontmatter |
| [mcp](packages/mcp/README.md) | July-only stdio/HTTP MCP and JSON CLI |
| [sqlite](packages/sqlite/README.md) | SQLite/FTS5, transactions and backup |
| [curl](packages/curl/README.md) | HTTP/HTTPS and Unix socket clients |
| [ws](packages/ws/README.md) | WebSocket client |
| [bugsink](packages/bugsink/README.md) | Optional error reporting |
| [encoding](packages/encoding/README.md) | Base64, Base64url and hexadecimal |
| [proc](packages/proc/README.md) | Safe subprocess execution |
| [time](packages/time/README.md) | UTC formatting and civil-day bounds |
| [text](packages/text/README.md) | Text sanitization and HTML conversion |
| [test](packages/test/README.md) | Lightweight tests and temporary fixtures |


## End-to-End Quick Start

Define a reflected input struct once and get both an MCP server (`./my-app mcp`) and a narrow JSON CLI (`./my-app add '{"title":"Buy milk"}'`) backed by SQLite in ~35 lines of C++26:

```cpp
#include <mcp.hpp>
#include <sqlite.hpp>

struct AddNoteInput {
    slimr::Field<std::string, "Note title"> title;
    slimr::OptField<int, "Priority (1-5)"> priority{3};
};

int main(int argc, char** argv) {
    slimr::Database db(":memory:", slimr::SqliteOptions{
        .migrations = std::vector<std::string>{
            "CREATE TABLE notes (id INTEGER PRIMARY KEY, title TEXT NOT NULL, priority INT NOT NULL);"
        }
    });

    slimr::PluginApp app("notes-demo", "1.0.0");
    app.instructions = "Use `add` to store notes and `list` to inspect them.";

    app.tool<AddNoteInput>(
        "add",
        "Add a note to SQLite",
        slimr::ToolAnnotations{.readOnlyHint = false, .openWorldHint = false},
        [&](const AddNoteInput& in) {
            auto r = db.run(
                "INSERT INTO notes (title, priority) VALUES (?, ?);",
                {in.title.get(), int64_t(in.priority.get())}
            );
            return slimr::json_obj({{"id", r.lastInsertRowid}});
        }
    );

    app.tool<std::monostate>(
        "list",
        "List all stored notes",
        slimr::ToolAnnotations{.readOnlyHint = true, .openWorldHint = false},
        [&](const std::monostate&) {
            return slimr::json_obj({{"notes", db.all("SELECT * FROM notes ORDER BY id ASC;")}});
        }
    );

    return app.run(argc, argv);
}
```

---

## Integration (vcpkg + CMake/Ninja)

### Dependencies

Requires a C++26 compiler (verified with Apple clang 21), CMake 3.31+, Ninja, [just](https://just.systems) and [vcpkg](https://vcpkg.io) on PATH. The environment owns `VCPKG_ROOT` and `VCPKG_OVERLAY_PORTS=$VCPKG_ROOT/local-ports`; recipes do not supply defaults or overwrite them. Host triplets are detected, but Linux portability is not yet verified.

A consumer declares only the packages it uses:

```json
{
  "name": "my-app",
  "version": "0.1.0",
  "dependencies": ["slimr-sqlite"]
}
```

`slimr-sqlite` brings in JSON and FTS5-enabled SQLite, not unrelated MCP, HTTP, YAML, or test libraries. WebSocket consumers explicitly request curl's `websockets` feature. Package tests use an optional `tests` feature, keeping doctest out of production dependency closures.

All thirteen packages are published: `slimr-proc` is `0.1.2`, and the others are `0.1.1`. No manual slimr-cpp checkout is needed: add this `vcpkg-configuration.json` alongside the app manifest:

```json
{
  "default-registry": {
    "kind": "builtin",
    "baseline": "5cad485d1ddb649c8fa1022bf0b3908555e6e6ba"
  },
  "registries": [{
    "kind": "git",
    "repository": "https://github.com/bdombro/slimr-cpp.git",
    "baseline": "91b48cf91739dc622f97c6dd8f0c0f54c367ce43",
    "packages": ["slimr-*"]
  }]
}
```

Run `vcpkg install` with `VCPKG_OVERLAY_PORTS` unset to use published packages. This baseline predates the July HTTP changes: current `slimr-mcp[http]` consumers need the local-development setup until those changes are published in a new registry baseline.

### Usage example: MCP + SQLite

```json
{
  "name": "example-app",
  "version": "2.0.0",
  "dependencies": ["slimr-mcp", "slimr-sqlite", "slimr-test"]
}
```

After installing dependencies, consumers use the installed CMake targets:

```cmake
cmake_minimum_required(VERSION 3.31)
project(example LANGUAGES CXX)
find_package(slimr-mcp CONFIG REQUIRED)
find_package(slimr-sqlite CONFIG REQUIRED)
find_package(slimr-test CONFIG REQUIRED)
add_executable(example src/main.cpp)
target_link_libraries(example PRIVATE slimr::mcp slimr::sqlite)
```

Each package exports `slimr::<key>` with C++26, owned includes and transitive dependencies. No manual include paths, pkg-config linker strings, or source overrides are needed. Use the single root publisher for named overlay symlinks under the environment's `VCPKG_OVERLAY_PORTS` directory:

```sh
just publish --dry --local --all
just publish --local --include sqlite,json
just publish --local --undo --include sqlite,json
just publish --dry --include sqlite,json
```

Choose exactly one of `--include key,key`, `--dirty`, or `--all`. `--dry` uses the normal read-only plan but skips all checks and mutations. Local mode creates/replaces selected named symlinks, or removes them with `--undo`; targets are never modified and selected nonlinks are rejected before any mutation. Registry mode adds affected dependents, patch-bumps versions and updates dependency minimums only after all release-set package tests, Release builds/tests and lint pass. It commits package source first, then metadata with real committed tree hashes, preserves unrelated dirty/staged work, and pushes the configured upstream. Do not run registry mode without explicit release authorization. See [Publishing](docs/publishing.md) for selection, switching sources and partial-failure recovery.

Use `vcpkg install --triplet=<host-triplet> --x-install-root=build/deps`.
Configure with vcpkg's toolchain, `VCPKG_MANIFEST_MODE=OFF`,
`VCPKG_MANIFEST_INSTALL=OFF`, the absolute `VCPKG_INSTALLED_DIR` and matching
`VCPKG_TARGET_TRIPLET`. Thin justfiles own automatic dependency installation;
CMake does not install a second time. Local source edits are refreshed through
`deps` before Ninja builds installed headers. Binary caching stays enabled.

---

## Troubleshooting

- **HTTP feature missing:** the old registry baseline predates July HTTP.
  Use a matching local checkout and overlay, not a source include override.
- **Port not found:** verify `VCPKG_ROOT`, `VCPKG_OVERLAY_PORTS` and the local
  links. The pinned libhv overlay is registered separately.
- **Compiler or hardening check fails:** use current Xcode/Apple Clang 21,
  or another compatible C++26/libc++ toolchain, selected before configure.
- **Switching local/published packages or compilers:** use fresh generated
  configuration/install output and re-resolve dependencies; don't mutate
  installed headers or disable caching for routine edits.


## Development

[Architecture, full package reference, tests and CI/fuzzing status](docs/development.md)
are documented separately. Use the [publishing guide](docs/publishing.md) for
registry releases and local overrides; registry publication requires explicit
authorization.

<details>
<summary>macOS source-build setup (vcpkg and local overlays)</summary>

### macOS: install and set up vcpkg

Install [Homebrew](https://brew.sh) and current Xcode Command Line Tools
(`xcode-select --install`; finish the installer before continuing), or full
Xcode. Apple Clang 21 is the verified C++26/libc++ toolchain; older installations
need upgrading. These instructions assume Apple Silicon Homebrew at `/opt/homebrew`.

Add this environment snippet to `~/.zshrc`, then open a new terminal (or run
`source ~/.zshrc`). Use `~/.zshrc`, not `~/.zshenv`, to avoid changing
non-interactive zsh scripts:

`local-ports` is the dedicated vcpkg overlay directory for local development
linking: `just publish --local --all` registers symlinks to your slimr-cpp checkout,
overriding published packages without making a registry release. See the
[publishing guide](https://github.com/bdombro/slimr-cpp/blob/main/docs/publishing.md#local-environment).

```bash
export VCPKG_ROOT="$HOME/.local/share/vcpkg"
export VCPKG_OVERLAY_PORTS="$VCPKG_ROOT/local-ports"
export CXX="/usr/bin/clang++"
export CLANG_TIDY="/opt/homebrew/opt/llvm/bin/clang-tidy"
export PATH="$VCPKG_ROOT:/opt/homebrew/opt/ruby/bin:/opt/homebrew/bin:$PATH"
```

Then install and build from this repository's root:

```bash
brew install cmake ninja just llvm node ruby autoconf automake libtool pkgconf
mkdir -p "$(dirname "$VCPKG_ROOT")"
if [ ! -d "$VCPKG_ROOT/.git" ]; then
  git clone https://github.com/microsoft/vcpkg.git "$VCPKG_ROOT"
fi
"$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics
just publish --local --all
# Register the pinned native HTTP dependency separately.
ln -s "$PWD/ports/libhv" "$VCPKG_OVERLAY_PORTS/libhv"
"$CXX" --version
vcpkg version
BUGSINK_DSN="" just test
just build
```

The libhv link is a one-time setup step. If it already exists, verify it points
to this checkout's `ports/libhv` before skipping that command; do not replace a
conflicting directory or modify installed headers/caches. The root build tests
source targets; vcpkg supplies native SQLite/FTS5, WebSocket-enabled curl and
libhv under `build/deps/`. Separate Homebrew installs of those libraries are
not required. Local publication only registers overlays; it does not commit,
push, or publish a registry release.

Bootstrapping puts the executable at `$VCPKG_ROOT/vcpkg`, with no `bin/`
subdirectory. Keg-only Homebrew Ruby supplies `/opt/homebrew/opt/ruby/bin/ruby`.
Homebrew LLVM supplies `clang-tidy`; recipes explicitly default to the Apple
compiler, regardless of `PATH`. Set `CXX` before first configure if selecting
another compatible compiler; CMake caches the selection. To reproduce CI's
vcpkg tooling, check out the tested full `VCPKG_REF` commit before bootstrapping.
Package registry baselines and the vcpkg executable version are separate pins.

</details>
