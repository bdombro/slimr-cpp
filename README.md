# 🪶 slimr-cpp monorepo

`slimr-cpp` is a monorepo of 13 slim, header-only **C++26** libraries for building fast, zero-bloat Model Context Protocol (MCP) servers, narrow JSON CLIs, SQLite + FTS5 applications, and HTTP/WebSocket clients.

Inspired by the ergonomics of TypeScript/Node [`@slimr`](https://github.com/bdombro/slimr) and `argsbarg`, `slimr-cpp` brings single-source-of-truth schema reflection, safe subprocess/socket primitives, and production SQLite defaults to native C++26 — compiling complete MCP servers into ~500 KB–1.3 MB binaries with `<2 ms` cold start and `~1.5 MB` RSS.

---

## Packages

| Package | Header | Target | Description |
| --- | --- | --- | --- |
| [`slimr::json`](packages/json/README.md) | `<slimr/json.hpp>` | `slimr::json` | RFC-8259 UTF-8 JSON DOM (`slimr::Json` preserving 64-bit integer precision over [Glaze](https://github.com/stephenberry/glaze)), `parse_json<T>` (`std::expected`), `write_json`, `read_json`, and `json_obj` / `json_arr` builders without `std::initializer_list` traps. |
| [`slimr::reflect`](packages/reflect/README.md) | `<slimr/reflect.hpp>` | `slimr::reflect` | Zero-macro C++26 compile-time struct reflection (`Field<T, "desc">`, `OptField<T, "desc">`, `ReqField<T, "desc">`) that maps C++ structs directly to strict MCP JSON Schemas (`additionalProperties: false`) and typed Glaze deserializers. |
| [`slimr::yaml`](packages/yaml/README.md) | `<slimr/yaml.hpp>` | `slimr::yaml` | Arena-allocated YAML 1.2 parser and emitter (`parse_yaml<T>`, `to_yaml`) plus Markdown YAML frontmatter splitter (`parse_frontmatter`) over [RapidYAML](https://github.com/biojppm/rapidyaml). |
| [`slimr::mcp`](packages/mcp/README.md) | `<slimr/mcp.hpp>` | `slimr::mcp` | `PluginApp` and multi-plugin `Gateway`: stdio NDJSON MCP server (`2025-11-25` / `2025-06-18` / `2024-11-05`) and narrow JSON CLI (`<bin> <tool> ['{"k":"v"}']`) sharing one deserializer, with resources (`mcp_resource_read`) and native `text`/`image`/`audio` content blocks. |
| [`slimr::sqlite`](packages/sqlite/README.md) | `<slimr/sqlite.hpp>` | `slimr::sqlite` | RAII SQLite 3 + FTS5 wrapper (`slimr::Database`) with WAL defaults, a 256-statement LRU cache, positional & named parameters (`:k`, `$k`, `@k`), typed `get<T>`/`all<T>`/`pluck<T>`/`pluckAll<T>`, nested `SAVEPOINT` transactions, `PRAGMA user_version` migrations, `iterateLimited` (`readOnlyConnection` + `PRAGMA query_only = ON`), custom scalar functions, and online backup. |
| [`slimr::curl`](packages/curl/README.md) | `<slimr/curl.hpp>` | `slimr::curl` | RAII HTTP/HTTPS client (`slimr::request`, `slimr::url_encode`) over `libcurl` with lowercased response headers, timeouts, custom HTTP methods, and Unix domain socket transport (`CURLOPT_UNIX_SOCKET_PATH`). |
| [`slimr::ws`](packages/ws/README.md) | `<slimr/ws.hpp>` | `slimr::ws` | RAII WebSocket client (`connect`, `sendJson`, `recvJson`, `sendText`, `recvText`) over `libcurl` 8.7+ native WebSockets (`CURLOPT_CONNECT_ONLY = 2L`, `curl_ws_send`, `curl_ws_recv`). |
| [`slimr::bugsink`](packages/bugsink/README.md) | `<slimr/bugsink.hpp>` | `slimr::bugsink` | Sentry v7 NDJSON envelope formatter and non-throwing error/warning reporter (`BugsinkReporter`) for self-hosted Bugsink or Sentry (`BUGSINK_DSN` override, `""` disables). |
| [`slimr::encoding`](packages/encoding/README.md) | `<slimr/encoding.hpp>` | `slimr::encoding` | RFC 4648 Base64, Base64url (with optional `=` padding), and hexadecimal encoding/decoding with `std::expected` validation. |
| [`slimr::proc`](packages/proc/README.md) | `<slimr/proc.hpp>` | `slimr::proc` | Safe subprocess execution (`slimr::proc::exec`) via `posix_spawnp` + `poll()` pipe multiplexor (never invokes a shell) with `cwd`, merged `env`, `stdin_data`, and `timeout_ms` (`SIGKILL` watchdog). |
| [`slimr::time`](packages/time/README.md) | `<slimr/time.hpp>` | `slimr::time` | Thread-safe UTC ISO-8601 formatting/parsing (`format_iso8601_utc`, `format_iso8601_utc_ms`, `parse_iso8601_utc`) and DST-accurate IANA timezone civil day bounds (`civil_day_bounds("2026-07-01", "Europe/Dublin")`). |
| [`slimr::text`](packages/text/README.md) | `<slimr/text.hpp>` | `slimr::text` | UTF-8 text sanitizer (`clean_text` stripping ASCII control chars, zero-width spaces, bidi overrides, and Unicode tag-plane prompt injections) and 12-stage HTML-to-readable-text converter (`html_to_text`). |
| [`slimr::test`](packages/test/README.md) | `<slimr/test.hpp>` | `slimr::test` | Lightweight test runner macros (`TEST_SUITE`, `TEST_CASE`, `CHECK`, `REQUIRE`) and RAII `TempDirGuard` fixture helper over [doctest](https://github.com/doctest/doctest). |

---

## Integration (Direct Includes, CMake, or vcpkg)

Each package lives in `packages/<pkg>/` with its own self-contained `include/slimr/<pkg>.hpp`, `tests/test_<pkg>.cpp`, `CMakeLists.txt` (`slimr::<pkg>`), and `README.md`.

### 1. Direct Compiler Include (`-Islimr-cpp/include`)

Forwarding headers in `slimr-cpp/include/slimr/<pkg>.hpp` re-export each package header, so you can compile directly with `clang++ -std=c++26` without CMake:

```bash
clang++ -std=c++26 -O3 -flto \
  -Islimr-cpp/include -Islimr-cpp/vendor/glaze/include -Islimr-cpp/vendor -Islimr-cpp/vendor/ryml \
  src/main.cpp -lsqlite3 -lcurl -o dist/my-app
```

For sub-second incremental compilation during development, precompile `slimr/pch.hpp`:

```bash
clang++ -std=c++26 -O2 -g -fsanitize=address,undefined \
  -Islimr-cpp/include -Islimr-cpp/vendor/glaze/include -Islimr-cpp/vendor \
  -x c++-header slimr-cpp/include/slimr/pch.hpp -o slimr-cpp/include/slimr/pch.hpp.gch
```

### 2. CMake (`add_subdirectory`)

```cmake
add_subdirectory(slimr-cpp)

add_executable(my-app src/main.cpp)
target_link_libraries(my-app PRIVATE
  slimr::mcp
  slimr::sqlite
  slimr::yaml
  slimr::proc
)
```

The project exports installable `slimr::` targets, including `slimr::all`. The headers and CMake package config are installed by the vcpkg port.

### 3. vcpkg (local overlay port)

The assistant2 workspace keeps an overlay port at `vcpkg/ports/slimr-cpp`; it builds this checkout directly, so no registry publication or global install is needed. Its `just vcpkg-install` task refreshes the local package from current sources. From the workspace root, set `VCPKG_ROOT` to a bootstrapped vcpkg checkout and run:

```bash
just vcpkg-install
```

Manifest mode installs the library and its external dependencies under `vcpkg_installed/`. To consume it from another vcpkg project on the same machine, add the workspace overlay to that project's `vcpkg-configuration.json`:

```json
{
  "overlay-ports": ["/path/to/assistant2/vcpkg/ports"]
}
```

Then add `"slimr-cpp"` to that project's `vcpkg.json` dependencies and link the exported targets, such as `slimr::mcp` or `slimr::sqlite`.

---

## End-to-End Quick Start

Define a reflected input struct once and get both an MCP server (`./my-app mcp`) and a narrow JSON CLI (`./my-app add '{"title":"Buy milk"}'`) backed by SQLite in ~35 lines of C++26:

```cpp
#include <slimr/mcp.hpp>
#include <slimr/sqlite.hpp>

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

## Development & Sanitizer Testing

Every package test suite is compiled and executed under **AddressSanitizer** and **UndefinedBehaviorSanitizer** (`-fsanitize=address,undefined`):

```bash
just test             # Run all 33 test cases (154 assertions) across all 13 packages
just test-pkg sqlite  # Run a single package's test suite (json, reflect, yaml, mcp, sqlite, curl, ws, ...)
just build            # Build optimized (-O3 -flto) release test binary
```

---

## Cheat Sheet

| Goal | Header | Primary API |
| --- | --- | --- |
| Parse / serialize JSON | `<slimr/json.hpp>` | `slimr::parse_json<T>(sv)`, `slimr::write_json(v)`, `slimr::json_obj({...})` |
| Reflect struct to JSON Schema | `<slimr/reflect.hpp>` | `slimr::Field<T, "desc">`, `slimr::OptField<T, "desc">`, `slimr::schema<T>()` |
| Parse YAML / frontmatter | `<slimr/yaml.hpp>` | `slimr::parse_yaml<T>(sv)`, `slimr::to_yaml(v)`, `slimr::parse_frontmatter(md)` |
| Build MCP server + JSON CLI | `<slimr/mcp.hpp>` | `slimr::PluginApp`, `app.tool<In>(...)`, `slimr::Gateway`, `app.run(argc, argv)` |
| Query SQLite + FTS5 | `<slimr/sqlite.hpp>` | `slimr::Database`, `db.get<T>`, `db.all<T>`, `db.pluck<T>`, `db.transaction`, `db.iterateLimited` |
| HTTP / Unix socket requests | `<slimr/curl.hpp>` | `slimr::request(HttpRequest{...})`, `slimr::url_encode(sv)` |
| WebSocket client (CDP) | `<slimr/ws.hpp>` | `slimr::WebSocket`, `ws.connect(url)`, `ws.sendJson(j)`, `ws.recvJson(timeoutMs)` |
| Report errors to Bugsink | `<slimr/bugsink.hpp>` | `slimr::BugsinkReporter`, `reporter.reportError(e, extra)`, `reporter.reportMessage(...)` |
| Safe subprocess (`posix_spawnp`) | `<slimr/proc.hpp>` | `slimr::proc::exec({"gws", ...}, {.timeout_ms = 5000})` |
| Civil dates & ISO-8601 | `<slimr/time.hpp>` | `slimr::civil_day_bounds("2026-07-01", "Europe/Dublin")`, `slimr::format_iso8601_utc(sec)` |
| Clean HTML & untrusted UTF-8 | `<slimr/text.hpp>` | `slimr::clean_text(sv)`, `slimr::html_to_text(html)` |
| Base64 / Base64url / Hex | `<slimr/encoding.hpp>` | `slimr::base64_encode(sv)`, `slimr::base64url_encode(sv)`, `slimr::hex_encode(sv)` |
| Unit tests & temp dirs | `<slimr/test.hpp>` | `TEST_CASE`, `CHECK`, `REQUIRE`, `slimr::testing::TempDirGuard` |
