# slimr::curl (`packages/curl`)

Fast, clean RAII libcurl wrapper for C++26 supporting HTTP requests, custom headers, timeouts, Unix domain sockets, and URL encoding.

Borrowed directly from the design of [`@slimr/curl`](https://www.npmjs.com/package/@slimr/curl).

## Why `slimr::curl`?

- **Clean RAII API:** Simple `HttpRequest` and `HttpResponse` structures with no manual curl handle cleanup boilerplate.
- **Unix Domain Socket Support:** Built-in `unixSocketPath` option for talking directly to local daemons (e.g., Docker, containerd, or system APIs).
- **Header Parsing:** Automatically captures and lowercases response headers into an STL `std::map`.
- **URL Encoding:** Easy string escaping via `url_encode()`.

---

## Quick Start

```cpp
#include <curl.hpp>
#include <iostream>

using namespace slimr;

int main() {
    try {
        // Simple GET request
        HttpResponse res = request(HttpRequest{
            .method = "GET",
            .url = "https://api.github.com/zen",
            .headers = {{"User-Agent", "slimr-cpp"}}
        });

        std::cout << "Status: " << res.status << "\n";
        std::cout << "Body: " << res.body << "\n";

        // POST request with JSON and custom timeout / Unix socket
        HttpResponse postRes = request(HttpRequest{
            .method = "POST",
            .url = "http://localhost/v1/exec",
            .headers = {{"Content-Type", "application/json"}},
            .body = R"({"command": "status"})",
            .unixSocketPath = "/var/run/docker.sock",
            .timeoutMs = 5000
        });

        // URL Encoding
        std::string encoded = url_encode("hello world/foo@bar");
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
    }
    return 0;
}
```

---

## API Cheat Sheet

| Type / Function | Signature | Description |
| --- | --- | --- |
| `HttpResponse` | `struct HttpResponse { long status = 0; std::map<std::string, std::string> headers; std::string body; };` | HTTP response container holding status code, parsed headers, and body. |
| `HttpRequest` | `struct HttpRequest { std::string method = "GET"; std::string url; std::map<std::string, std::string> headers; std::string body; std::string unixSocketPath; long timeoutMs = 10000; };` | HTTP request configuration options. |
| `url_encode` | `inline std::string url_encode(std::string_view input)` | URL-escapes an input string using libcurl. |
| `request` | `inline HttpResponse request(const HttpRequest& req)` | Executes HTTP request and returns `HttpResponse`. Throws `std::runtime_error` on failure. |

## Build & Test

This folder owns its header, `test.cpp`, CMake target/config, manifest, port recipe and licenses. See the [integration guide](../../README.md#integration-vcpkg--cmakeninja).

```bash
just test-pkg curl  # From the repository root; automatic native deps + ASan/UBSan
just build         # All optimized Release suites
```

Generated output stays under root `build/deps/` and `build/cmake/{Debug,Release}/`. Consumers link `slimr::curl` from `find_package(slimr-curl CONFIG REQUIRED)`. The optional manifest `tests` feature keeps test dependencies out of production consumption.
