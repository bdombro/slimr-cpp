# AGENTS.md (`slimr-cpp`)

Written for: AI coding agents working in the `slimr-cpp` C++26 monorepo.

`slimr-cpp` is a header-only C++26 library monorepo organized into standalone packages under `packages/<pkg>/` with forwarding convenience headers in `include/slimr/<pkg>.hpp`.

## Commands

| Task | Command |
| --- | --- |
| Run full test suite (ASan + UBSan) | `just test` |
| Run a single package's test suite | `just test-pkg <pkg>` (e.g. `just test-pkg sqlite`) |
| Build optimized release test binary | `just build` |
| Clean build artifacts | `just clean` |

## Package Structure & Conventions

Every package under `packages/<pkg>/` must contain:
- `include/slimr/<pkg>.hpp` — the self-contained `#pragma once` header in namespace `slimr` (or `slimr::<subns>`).
- `tests/test_<pkg>.cpp` — standalone `doctest` test suite verifying all public types, functions, edge cases, and error paths.
- `CMakeLists.txt` — `INTERFACE` library target `slimr::<pkg>` declaring its include directories and link dependencies.
- `README.md` — concise usage guide and API reference for the package.

And at the monorepo root:
- `include/slimr/<pkg>.hpp` — forwards to `../../packages/<pkg>/include/slimr/<pkg>.hpp` so downstream projects can pass `-Islimr-cpp/include` and `#include <slimr/<pkg>.hpp>` without listing 13 `-I` flags.
- `tests/test_main.cpp` — unified test runner exercising all packages together.

## Important Implementation Rules

1. **`glz::generic_i64` (`slimr::Json`) brace initialization:**
   Never write `Json{std::move(obj)}` when `obj` is `Json::object_t` or `Json::array_t` — `glz::generic` has an `std::initializer_list` constructor that wraps the value inside a 1-element JSON array! Always use `slimr::json_obj(std::move(obj))` / `slimr::json_arr(std::move(arr))` or assign `j.data = std::move(obj)`.
2. **`rapidyaml` (`ryml_all.hpp`) node checks in `packages/yaml`:**
   In `ryml`, a key-value map child (`key: val`) has both `is_keyval() == true` and `has_val() == true`. When converting a `c4::yml::ConstNodeRef` to `slimr::Json`, check `node.is_map()` and `node.is_seq()` appropriately, and for scalar values check `node.has_val()`.
3. **RAII & Sanitizer cleanliness:**
   All resources (`sqlite3*`, `sqlite3_stmt*`, `sqlite3_backup*`, `CURL*`, `curl_slist*`, file descriptors, `posix_spawn_file_actions_t`, temp directories) must be wrapped in RAII types and pass `just test` under `-fsanitize=address,undefined` with zero leaks or undefined behavior.
4. **Zero shell execution:**
   `packages/proc` uses `posix_spawnp` + `poll()` directly and must never invoke `/bin/sh` or `popen`.
