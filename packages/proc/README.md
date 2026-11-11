# slimr::proc (`packages/proc`)

Fast, clean POSIX process spawning library for C++26 using `posix_spawnp`, supporting custom working directories, environment variable merging, stdin piping, non-blocking polling, and timeout execution limits.

Borrowed directly from the design of [`@slimr/proc`](https://www.npmjs.com/package/@slimr/proc).

## Why `slimr::proc`?

- **POSIX Spawn:** Uses native `posix_spawnp` for efficient, reliable process execution.
- **Timeout & Watchdog Protection:** Configurable execution timeouts (`timeout_ms`) that automatically kill hung processes with `SIGKILL`.
- **Advanced I/O Redirection:** Capture `stdout` and `stderr` independently while optionally writing `stdin_data`.
- **Environment & CWD Customization:** Easily specify custom working directories and override or merge environment variables.

---

## Quick Start

```cpp
#include <proc.hpp>
#include <iostream>
#include <vector>

using namespace slimr::proc;

int main() {
    // Execute a simple command with options
    ProcResult res = exec({"git", "status"}, ProcOptions{
        .cwd = ".",
        .env = {{"GIT_TERMINAL_PROMPT", "0"}},
        .timeout_ms = 3000
    });

    if (res.timed_out) {
        std::cerr << "Process timed out!\n";
    } else if (res.exit_code == 0) {
        std::cout << "Output:\n" << res.stdout_str << "\n";
    } else {
        std::cerr << "Failed with exit code " << res.exit_code << ":\n" << res.stderr_str << "\n";
    }

    // Execute with stdin piping
    ProcResult echoRes = exec({"cat"}, ProcOptions{
        .stdin_data = "Hello from stdin pipe!"
    });
    std::cout << echoRes.stdout_str << "\n";

    return 0;
}
```

---

## API Cheat Sheet

| Type / Function | Signature | Description |
| --- | --- | --- |
| `ProcOptions` | `struct ProcOptions { std::string cwd; std::map<std::string, std::string> env; std::string stdin_data; int timeout_ms = 0; };` | Options configuring working directory, environment, stdin, and timeout. |
| `ProcResult` | `struct ProcResult { int exit_code = -1; std::string stdout_str; std::string stderr_str; bool timed_out = false; };` | Result container holding exit code, stdout, stderr, and timeout status. |
| `exec` | `inline ProcResult exec(const std::vector<std::string>& args, const ProcOptions& options = {})` | Spawns and executes process with args and options, polling output and enforcing timeouts. |

## Build & Test

This folder owns its header, `test.cpp`, CMake target/config, manifest, port recipe and licenses. See the [integration guide](../../README.md#integration-vcpkg--cmakeninja).

```bash
just test-pkg proc  # From the repository root; automatic native deps + ASan/UBSan
just build         # All optimized Release suites
```

Generated output stays under root `build/deps/` and `build/cmake/{Debug,Release}/`. Consumers link `slimr::proc` from `find_package(slimr-proc CONFIG REQUIRED)`. The optional manifest `tests` feature keeps test dependencies out of production consumption.
