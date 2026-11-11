# slimr::json (`packages/json`)

Fast, lightweight C++26 JSON library built on top of [Glaze](https://github.com/stephenberry/glaze) with precise 64-bit integer generic trees, `std::expected` parsing, and safe object/array constructors.

Borrowed directly from the design of [`@slimr/json`](https://www.npmjs.com/package/@slimr/json).

## Why `slimr::json`?

- **64-bit Integer Precision:** Uses `glz::generic_i64` (`slimr::Json`) to preserve exact 64-bit integer values without silent floating-point truncation.
- **`std::expected` Parsing:** `parse_json<T>(view)` returns clean `std::expected<T, std::string>` with human-readable syntax error formatting out of the box.
- **Ambiguity-free Helpers:** Provides `json_obj()` and `json_arr()` helper functions to prevent initializer-list ambiguity when constructing dynamic JSON trees.
- **Zero-overhead Glaze Foundation:** Direct inline wrappers around Glaze serialization and deserialization templates for maximum performance.

---

## Quick Start

```cpp
#include <json.hpp>
#include <iostream>

using namespace slimr;

int main() {
    // Parse JSON string into dynamic Json tree with std::expected error handling
    auto parsed = parse_json(R"({"name": "Alice", "age": 30, "tags": ["admin", "user"]})");
    if (!parsed) {
        std::cerr << "JSON parse failed: " << parsed.error() << "\n";
        return 1;
    }

    // Access dynamic values
    std::string name = (*parsed)["name"].get<std::string>();
    int64_t age = (*parsed)["age"].get<int64_t>();

    // Serialize back to JSON strings
    std::string compact = write_json(*parsed);
    std::string pretty = write_json_pretty(*parsed);

    // Construct dynamic JSON objects safely
    Json obj = json_obj({
        {"status", "ok"},
        {"code", 200}
    });

    std::cout << pretty << "\n";
    return 0;
}
```

---

## API Cheat Sheet

| Function / Type | Signature | Description |
| --- | --- | --- |
| `slimr::Json` / `json_t` | `using Json = glz::generic_i64;` | Dynamic JSON tree type preserving 64-bit integer precision. |
| `json_obj` | `inline Json json_obj(Json::object_t obj = {})` | Safe JSON object constructor preventing initializer list ambiguities. |
| `json_arr` | `inline Json json_arr(Json::array_t arr = {})` | Safe JSON array constructor preventing initializer list ambiguities. |
| `write_json` (buffer) | `template <typename T> inline auto write_json(const T& val, std::string& buffer)` | Serializes any supported C++ struct or value into target string buffer. |
| `write_json` (string) | `template <typename T> inline std::string write_json(const T& val)` | Serializes any supported C++ struct or value to a new string. |
| `to_json` | `template <typename T> inline std::string to_json(const T& val)` | Convenience alias for serializing to JSON string. |
| `write_json_pretty` | `template <typename T> inline std::string write_json_pretty(const T& val)` | Serializes value to formatted indented JSON string. |
| `read_json` | `template <typename T> inline auto read_json(T& val, std::string_view buffer)` | Deserializes JSON string into target C++ struct or value. |
| `parse_json` | `template <typename T = Json> inline std::expected<T, std::string> parse_json(std::string_view buffer)` | Parses JSON string into newly constructed value of type `T` inside `std::expected`. |

## Build & Test

This folder owns its header, `test.cpp`, CMake target/config, manifest, port recipe and licenses. See the [integration guide](../../README.md#integration-vcpkg--cmakeninja).

```bash
just test-pkg json  # From the repository root; automatic native deps + ASan/UBSan
just build         # All optimized Release suites
```

Generated output stays under root `build/deps/` and `build/cmake/{Debug,Release}/`. Consumers link `slimr::json` from `find_package(slimr-json CONFIG REQUIRED)`. The optional manifest `tests` feature keeps test dependencies out of production consumption.
