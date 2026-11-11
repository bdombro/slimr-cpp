# slimr-cpp Developer Reference

[Library quick start](../README.md) | [Publishing](publishing.md)

## Packages

| Package | Header | vcpkg package | Description |
| --- | --- | --- | --- |
| [`slimr::json`](../packages/json/README.md) | `<json.hpp>` | `slimr-json` | RFC-8259 UTF-8 JSON DOM (`slimr::Json` preserving 64-bit integer precision over [Glaze](https://github.com/stephenberry/glaze)), `parse_json<T>` (`std::expected`), `write_json`, `read_json`, and `json_obj` / `json_arr` builders without `std::initializer_list` traps. |
| [`slimr::reflect`](../packages/reflect/README.md) | `<reflect.hpp>` | `slimr-reflect` | Zero-macro C++26 compile-time struct reflection (`Field<T, "desc">`, `OptField<T, "desc">`, `ReqField<T, "desc">`) that maps C++ structs directly to strict MCP JSON Schemas (`additionalProperties: false`) and typed Glaze deserializers. |
| [`slimr::yaml`](../packages/yaml/README.md) | `<yaml.hpp>` | `slimr-yaml` | Arena-allocated YAML 1.2 parser and emitter (`parse_yaml<T>`, `to_yaml`) plus Markdown YAML frontmatter splitter (`parse_frontmatter`) over [RapidYAML](https://github.com/biojppm/rapidyaml). |
| [`slimr::mcp`](../packages/mcp/README.md) | `<mcp.hpp>` | `slimr-mcp` | `PluginApp`: July-only stdio/HTTP MCP and JSON CLI; see the MCP package reference.
| [`slimr::sqlite`](../packages/sqlite/README.md) | `<sqlite.hpp>` | `slimr-sqlite` | RAII SQLite 3 + FTS5 wrapper (`slimr::Database`) with WAL defaults, a 256-statement LRU cache, positional & named parameters (`:k`, `$k`, `@k`), typed `get<T>`/`all<T>`/`pluck<T>`/`pluckAll<T>`, nested `SAVEPOINT` transactions, `PRAGMA user_version` migrations, `iterateLimited` (`readOnlyConnection` + `PRAGMA query_only = ON`), custom scalar functions, and online backup. |
| [`slimr::curl`](../packages/curl/README.md) | `<curl.hpp>` | `slimr-curl` | RAII HTTP/HTTPS client (`slimr::request`, `slimr::url_encode`) over `libcurl` with lowercased response headers, timeouts, custom HTTP methods, and Unix domain socket transport (`CURLOPT_UNIX_SOCKET_PATH`). |
| [`slimr::ws`](../packages/ws/README.md) | `<ws.hpp>` | `slimr-ws` | RAII WebSocket client (`connect`, `sendJson`, `recvJson`, `sendText`, `recvText`) over `libcurl` 8.7+ native WebSockets (`CURLOPT_CONNECT_ONLY = 2L`, `curl_ws_send`, `curl_ws_recv`). |
| [`slimr::bugsink`](../packages/bugsink/README.md) | `<bugsink.hpp>` | `slimr-bugsink` | Sentry v7 NDJSON envelope formatter and non-throwing error/warning reporter (`BugsinkReporter`) for self-hosted Bugsink or Sentry (`BUGSINK_DSN` override, `""` disables). |
| [`slimr::encoding`](../packages/encoding/README.md) | `<encoding.hpp>` | `slimr-encoding` | RFC 4648 Base64, Base64url (with optional `=` padding), and hexadecimal encoding/decoding with `std::expected` validation. |
| [`slimr::proc`](../packages/proc/README.md) | `<proc.hpp>` | `slimr-proc` | Safe subprocess execution (`slimr::proc::exec`) via `posix_spawnp` + `poll()` pipe multiplexor (never invokes a shell) with `cwd`, merged `env`, `stdin_data`, and `timeout_ms` (`SIGKILL` watchdog). |
| [`slimr::time`](../packages/time/README.md) | `<time.hpp>` | `slimr-time` | Thread-safe UTC ISO-8601 formatting/parsing (`format_iso8601_utc`, `format_iso8601_utc_ms`, `parse_iso8601_utc`) and DST-accurate IANA timezone civil day bounds (`civil_day_bounds("2026-07-01", "Europe/Dublin")`). |
| [`slimr::text`](../packages/text/README.md) | `<text.hpp>` | `slimr-text` | UTF-8 text sanitizer (`clean_text` stripping ASCII control chars, zero-width spaces, bidi overrides, and Unicode tag-plane prompt injections) and 12-stage HTML-to-readable-text converter (`html_to_text`). |
| [`slimr::test`](../packages/test/README.md) | `<test.hpp>` | `slimr-test` | Lightweight test runner macros (`TEST_SUITE`, `TEST_CASE`, `CHECK`, `REQUIRE`) and RAII `TempDirGuard` fixture helper over [doctest](https://github.com/doctest/doctest). |

---

## Integration (vcpkg + CMake/Ninja)

Each library owns everything it needs in one flat folder:

```text
packages/sqlite/
  sqlite.hpp
  test.cpp
  CMakeLists.txt
  slimr-sqlite-config.cmake
  vcpkg.json
  portfile.cmake
  README.md
  LICENSE
```

There are no forwarding headers. Package-local CMake files declare source targets and standalone tests; each simple port installs headers, relocatable CMake config and licenses. Glaze lives under `packages/json/vendor/`, rapidyaml under `packages/yaml/vendor/`, and doctest under `packages/test/vendor/`, so each port's source is self-contained.



## Development & Sanitizer Testing

Every package test suite is compiled and executed under **AddressSanitizer** and **UndefinedBehaviorSanitizer** (`-fsanitize=address,undefined`):

One root CMake/Ninja project tests source targets, while `build/deps/` contains only native development dependencies, never installed slimr copies. Debug instrumentation includes ASan, fatal UBSan, zero auto-init and verified libc++ FAST hardening; Release retains hardening/zero-init with O3 and checked IPO. These settings are development-only, not exported to consumers.

```bash
just deps             # Install native development dependencies
just configure Debug  # Automatic deps, manifest auto-install OFF
just test             # Run the aggregate suite and all 13 standalone package suites
just test-pkg sqlite  # Run a single package's test suite (json, reflect, yaml, mcp, sqlite, curl, ws, ...)
just build            # Build and run all 14 optimized Release suites
just lint             # Analyze all 14 test translation units
```

Debug and Release graphs/binaries live in `build/cmake/{Debug,Release}/`; generated files never enter port trees. All test suites have 120-second CTest timeouts and repo-local temporary directories; commands use `--no-tests=error`, and `test-pkg` rejects unknown names. Unchanged Ninja builds do no work. All configure/test/build/lint commands run `deps` automatically. Set `CLANG_TIDY` to a compatible executable when not on PATH; missing tools and diagnostics fail rather than being silently skipped. Builds use at most two parallel jobs.

Original code is [ISC](../LICENSE). Vendored code keeps its upstream licenses.

---

## Native CI and Bounded Fuzzing

CI is temporarily disabled at the repository level by maintainer request, and required CI check gates have been removed. The last Linux/macOS matrix passed before this pause. The workflow configuration is retained for later use.

Fuzzing is separately paused: an earlier agent received a platform safety rejection (HTTP 422) while implementing it. The specific trigger is unknown; this was not a compiler or GitHub Actions failure. No fuzz execution or required fuzz gate has been verified, and `ENABLE_FUZZING` remains unset.

Retained Linux configuration example, not a validated fuzzing receipt:

```sh
just deps
CXX=clang++-21 CXXFLAGS=-stdlib=libc++ LDFLAGS=-fuse-ld=lld-21 \
cmake -S . -B build/cmake/Fuzz -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=OFF \
  -DBUILD_FUZZING=ON -DENABLE_SANITIZERS=ON \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_MANIFEST_MODE=OFF -DVCPKG_MANIFEST_INSTALL=OFF \
  -DVCPKG_INSTALLED_DIR="$PWD/build/deps" -DVCPKG_TARGET_TRIPLET=x64-linux
cmake --build build/cmake/Fuzz
mkdir -p build/fuzz/corpus build/fuzz/artifacts/{json,yaml,encoding}
cp -R fuzz/corpus/. build/fuzz/corpus/
for target in json yaml encoding; do
  "./build/cmake/Fuzz/fuzz/fuzz_$target" "build/fuzz/corpus/$target" \
    -max_total_time=15 -timeout=2 -rss_limit_mb=2048 -max_len=1024 \
    "-artifact_prefix=build/fuzz/artifacts/$target/"
done
```

Fuzz flags are target-local and never exported. The retained optional workflow steps configure fuzz-source linting, corpus caching and seven-day reproducer uploads; these fuzz steps remain unverified and disabled.

---

## Cheat Sheet

| Goal | Header | Primary API |
| --- | --- | --- |
| Parse / serialize JSON | `<json.hpp>` | `slimr::parse_json<T>(sv)`, `slimr::write_json(v)`, `slimr::json_obj({...})` |
| Reflect struct to JSON Schema | `<reflect.hpp>` | `slimr::Field<T, "desc">`, `slimr::OptField<T, "desc">`, `slimr::schema<T>()` |
| Parse YAML / frontmatter | `<yaml.hpp>` | `slimr::parse_yaml<T>(sv)`, `slimr::to_yaml(v)`, `slimr::parse_frontmatter(md)` |
| Build MCP server + JSON CLI | `<mcp.hpp>` | `slimr::PluginApp`, `app.tool<In>(...)`, `app.run(argc, argv)` |
| Query SQLite + FTS5 | `<sqlite.hpp>` | `slimr::Database`, `db.get<T>`, `db.all<T>`, `db.pluck<T>`, `db.transaction`, `db.iterateLimited` |
| HTTP / Unix socket requests | `<curl.hpp>` | `slimr::request(HttpRequest{...})`, `slimr::url_encode(sv)` |
| WebSocket client (CDP) | `<ws.hpp>` | `slimr::WebSocket`, `ws.connect(url)`, `ws.sendJson(j)`, `ws.recvJson(timeoutMs)` |
| Report errors to Bugsink | `<bugsink.hpp>` | `slimr::BugsinkReporter`, `reporter.reportError(e, extra)`, `reporter.reportMessage(...)` |
| Safe subprocess (`posix_spawnp`) | `<proc.hpp>` | `slimr::proc::exec({"gws", ...}, {.timeout_ms = 5000})` |
| Civil dates & ISO-8601 | `<time.hpp>` | `slimr::civil_day_bounds("2026-07-01", "Europe/Dublin")`, `slimr::format_iso8601_utc(sec)` |
| Clean HTML & untrusted UTF-8 | `<text.hpp>` | `slimr::clean_text(sv)`, `slimr::html_to_text(html)` |
| Base64 / Base64url / Hex | `<encoding.hpp>` | `slimr::base64_encode(sv)`, `slimr::base64url_encode(sv)`, `slimr::hex_encode(sv)` |
| Unit tests & temp dirs | `<test.hpp>` | `TEST_CASE`, `CHECK`, `REQUIRE`, `slimr::testing::TempDirGuard` |
