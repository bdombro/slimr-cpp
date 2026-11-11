# slimr::yaml (`packages/yaml`)

Fast, robust C++26 YAML parsing and serialization library built on top of [RapidYAML (ryml)](https://github.com/biojppm/rapidyaml) and `slimr::json`, featuring `std::expected` error handling and Markdown frontmatter extraction.

Borrowed directly from the design of [`@slimr/yaml`](https://www.npmjs.com/package/@slimr/yaml).

## Why `slimr::yaml`?

- **RapidYAML Core:** Powered by high-performance zero-copy RapidYAML underneath for fast in-arena parsing.
- **Seamless `slimr::Json` & Struct Integration:** Parses YAML directly into `slimr::Json` trees or arbitrary C++ structs via Glaze.
- **Markdown Frontmatter Support:** Built-in `parse_frontmatter()` helper for extracting and parsing YAML frontmatter blocks (`--- ... ---`) from Markdown documents.
- **Clean `std::expected` Errors:** Catches exceptions and reports descriptive error messages without crashing.

---

## Quick Start

```cpp
#include <yaml.hpp>
#include <iostream>

using namespace slimr;

int main() {
    // Parse YAML string into slimr::Json with std::expected
    auto yaml_str = R"(
        title: Hello Slimr
        version: 1.0
        features:
          - fast
          - robust
    )";

    auto doc = parse_yaml(yaml_str);
    if (!doc) {
        std::cerr << "YAML parse error: " << doc.error() << "\n";
        return 1;
    }

    std::string title = (*doc)["title"].get<std::string>();

    // Serialize back to YAML
    std::string serialized = to_yaml(*doc);

    // Parse Markdown frontmatter
    std::string markdown = R"(---
author: Alice
tags: [cpp, yaml]
---
# Document Body
Hello world!
)";

    auto parsed_fm = parse_frontmatter(markdown);
    if (parsed_fm && parsed_fm->frontmatter) {
        std::string author = (*parsed_fm->frontmatter)["author"].get<std::string>();
        std::cout << "Author: " << author << "\nBody: " << parsed_fm->body << "\n";
    }

    return 0;
}
```

---

## API Cheat Sheet

| Function / Type | Signature | Description |
| --- | --- | --- |
| `parse_yaml` | `template <typename T = Json> inline std::expected<T, std::string> parse_yaml(std::string_view yaml_str)` | Parses YAML string into `slimr::Json` or typed C++ struct `T` inside `std::expected`. |
| `to_yaml` | `template <typename T> inline std::string to_yaml(const T& val)` | Serializes a `slimr::Json` value or C++ struct `T` to a YAML string. |
| `FrontmatterSplit` | `struct FrontmatterSplit { std::optional<Json> frontmatter; std::string raw_frontmatter; std::string body; };` | Result structure containing parsed frontmatter and markdown body. |
| `parse_frontmatter` | `inline std::expected<FrontmatterSplit, std::string> parse_frontmatter(std::string_view markdown)` | Splits Markdown with `---` YAML frontmatter into `{ frontmatter, body }`. |

## Build & Test

This folder owns its header, `test.cpp`, CMake target/config, manifest, port recipe and licenses. See the [integration guide](../../README.md#integration-vcpkg--cmakeninja).

```bash
just test-pkg yaml  # From the repository root; automatic native deps + ASan/UBSan
just build         # All optimized Release suites
```

Generated output stays under root `build/deps/` and `build/cmake/{Debug,Release}/`. Consumers link `slimr::yaml` from `find_package(slimr-yaml CONFIG REQUIRED)`. The optional manifest `tests` feature keeps test dependencies out of production consumption.
