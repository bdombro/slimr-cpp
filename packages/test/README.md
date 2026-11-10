# slimr::test (`packages/test`)

Fast, clean C++26 testing wrapper and test fixture library built on top of [doctest](https://github.com/doctest/doctest), featuring automated temporary directory generation and RAII cleanup guards.

Borrowed directly from the design of [`@slimr/test`](https://www.npmjs.com/package/@slimr/test).

## Why `slimr::test`?

- **doctest Integration:** Clean single-header integration wrapping doctest with optional `SLIMR_TEST_MAIN` entrypoint definition.
- **RAII Temporary Directories:** `TempDirGuard` automatically creates unique temporary directories for tests and cleans up all files recursively on destruction.
- **Zero Heavy Dependencies:** Extremely lightweight and fast compiling.

---

## Quick Start

```cpp
#define SLIMR_TEST_MAIN
#include <slimr/test.hpp>

using namespace slimr::testing;

TEST_CASE("arithmetic operations") {
    CHECK(1 + 1 == 2);
    CHECK(2 * 3 == 6);
}

TEST_CASE("temporary directory guard") {
    TempDirGuard dir("my_test_");
    
    // Create a temporary file inside the managed directory
    auto filePath = dir.path / "test.txt";
    CHECK(std::filesystem::exists(dir.path));
    
    // Directory and contents are automatically removed when dir goes out of scope
}
```

---

## API Cheat Sheet

| Function / Type | Signature | Description |
| --- | --- | --- |
| `temp_dir` | `inline std::filesystem::path temp_dir(std::string_view prefix = "slimr_test_")` | Creates and returns a unique temporary directory path. |
| `TempDirGuard` | `struct TempDirGuard { std::filesystem::path path; explicit TempDirGuard(std::string_view prefix = "slimr_test_"); ~TempDirGuard(); };` | RAII guard that creates a unique temp directory on construction and recursively deletes it on destruction. |
