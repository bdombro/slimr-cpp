# slimr::bugsink (`packages/bugsink`)

Lightweight C++26 Bugsink / Sentry error reporting client built on top of `slimr::curl` and `slimr::json`, featuring DSN parsing, Sentry envelope formatting, and exception reporting.

Borrowed directly from the design of [`@slimr/bugsink`](https://www.npmjs.com/package/@slimr/bugsink).

## Why `slimr::bugsink`?

- **Standard Sentry Envelope Protocol:** Formats and posts Sentry-compatible event envelopes over HTTP.
- **Environment & Default DSNs:** Automatically checks environment variables (e.g. `BUGSINK_DSN`) with fallback to configured default DSNs.
- **Safe Exception Catching:** Designed so error reporting failures never crash or throw exceptions into user application logic.
- **Extra Context Support:** Attach arbitrary metadata and context dicts to exceptions or warning messages.

---

## Quick Start

```cpp
#include <bugsink.hpp>
#include <iostream>
#include <stdexcept>

using namespace slimr;

int main() {
    // Initialize Bugsink reporter (checks BUGDSINK_DSN env var or fallback string)
    BugsinkReporter reporter("https://public_key@bugsink.example.com/project_id");

    try {
        throw std::runtime_error("Database connection timeout");
    } catch (const std::exception& e) {
        // Report exception with optional extra context
        bool sent = reporter.reportError(e, json_obj({{"user_id", 42}}));
        if (sent) {
            std::cout << "Error reported to Bugsink successfully\n";
        }
    }

    // Report manual warning/info messages
    reporter.reportMessage("Low disk space warning", "warning", json_obj({{"free_gb", 2.5}}));

    return 0;
}
```

---

## API Cheat Sheet

| Member Function / Type | Signature | Description |
| --- | --- | --- |
| `ParsedDsn` | `struct ParsedDsn { std::string protocol; std::string publicKey; std::string host; std::string projectId; bool valid = false; };` | Parsed components of a Sentry/Bugsink DSN string. |
| `BugsinkReporter` constructor | `explicit BugsinkReporter(std::string dsn = "", std::string envVar = "BUGSINK_DSN")` | Constructs reporter with fallback DSN and target environment variable name. |
| `parseDsn` | `static ParsedDsn parseDsn(std::string_view dsn)` | Parses a DSN string into its protocol, public key, host, and project ID components. |
| `effectiveDsn` | `std::string effectiveDsn() const` | Returns the active DSN (checking environment variable first, then fallback). |
| `envelopeUrl` | `std::string envelopeUrl() const` | Builds the full Sentry HTTP envelope ingestion URL. |
| `formatEnvelope` | `std::string formatEnvelope(const Json& payload, const std::string& eventId, const std::string& timestamp) const` | Formats Sentry event payload into multi-line envelope format. |
| `reportError` | `bool reportError(const std::exception& err, const Json& extra = Json::object_t{})` | Reports a standard C++ exception with optional extra metadata. Returns `true` on success. |
| `reportMessage` | `bool reportMessage(std::string_view message, std::string_view level = "warning", const Json& extra = Json::object_t{})` | Reports a log message with severity level and optional extra metadata. |

## Build & Test

This folder owns its header, `test.cpp`, CMake target/config, manifest, port recipe and licenses. See the [integration guide](../../README.md#integration-vcpkg--cmakeninja).

```bash
just test-pkg bugsink  # From the repository root; automatic native deps + ASan/UBSan
just build            # All optimized Release suites
```

Generated output stays under root `build/deps/` and `build/cmake/{Debug,Release}/`. Consumers link `slimr::bugsink` from `find_package(slimr-bugsink CONFIG REQUIRED)`. The optional manifest `tests` feature keeps test dependencies out of production consumption.
