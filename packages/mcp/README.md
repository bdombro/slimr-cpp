# slimr::mcp (`packages/mcp`)

MCP-first C++26 framework (`2026-07-28` specification) turning reflected C++ structs into a stdio MCP server, a standalone Streamable HTTP + OAuth Resource Server daemon (`slimr::mcp_http`), and a narrow JSON CLI.

> **Breaking change (`2026-07-28`):** `slimr::Gateway` and pre-July session/init APIs (`initialize`, `ping`, HTTP session IDs, GET SSE streams) have been removed. Each plugin runs as its own native binary (`<bin> mcp` or `<bin> http`) with typed [`RequestContext`](mcp.hpp) metadata, `server/discover`, `resultType` / MRTR (`-32021`), `CacheableResult`, and cooperative cancellation.

## Why `slimr::mcp`?

Traditional CLI frameworks pull in thousands of lines of flag parsing that AI agents never use and that duplicate MCP's JSON input schema. `slimr::mcp` keeps a single source of truth per tool:

1. **One struct, three surfaces:** Every tool takes a single reflected C++ struct (`slimr::Field<T, "description">` / `slimr::OptField<T, "description">`).
   - **`<bin> mcp`** runs the multi-worker stdio NDJSON MCP `2026-07-28` server (`server/discover`, `_meta` validation, `notifications/cancelled`, `instructions`, `resources`, and auto-registered `mcp_resource_read` when resources are present).
   - **`<bin> http [--host <addr>] [--port <n>] [--allow-ephemeral-port]`** (when linked with `slimr::mcp_http` + `slimr::enable_http(app, opts)`) serves Streamable HTTP on `POST /mcp` with request-scoped chunked SSE streaming, HTTP header reflection (`Mcp-Method`, `Mcp-Name`, `Mcp-Param-*` including `x-mcp-header` + RFC 2047 `=?base64?...?=`), DNS-rebinding guards, bounded concurrency, and RFC 9728 / OAuth 2.1 Resource Server bearer validation.
   - **`<bin> <tool> ['{"arg":"val"}']`** invokes `<tool>` directly from the terminal or a `launchd` job using the exact same JSON-to-struct deserializer (defaulting to `"{}"` when omitted).
2. **Thread safety & cooperative cancellation:** Tools serialize by default (`ConcurrencyPolicy::Serialized`) or opt into concurrent execution via `ToolOptions{.threadSafe = true}`. Handlers taking `(const Input&, const slimr::RequestContext&)` receive `ctx.principal`, `ctx.cancellation`, `ctx.emitProgress(...)`, `ctx.emitLog(...)`, and MRTR continuation fields (`inputResponses`, `requestState`).
3. **Native MCP Media & Security:** Return `McpResult::image(base64, mime)` or `McpResult::audio(base64, mime)` directly from tool handlers. Sensitive bearer tokens and secrets are redacted from internal error/log strings (`redact_sensitive`).
4. **Built-in Bugsink Reporting:** Unhandled exceptions in tool handlers are reported cleanly when `app.setBugsink(dsn)` is configured.

---

## Quick Start

### 1. Define a `PluginApp` (`src/app.hpp`)

```cpp
#pragma once
#include <mcp.hpp>

struct GreetInput {
    slimr::Field<std::string, "Person to greet"> name;
    slimr::OptField<bool, "Use uppercase"> loud{false};
};

inline slimr::PluginApp make_greet_app() {
    slimr::PluginApp app("greeter", "1.0.0");
    app.description = "Greeting service over MCP and CLI";
    app.instructions = "Use `greet` to welcome users.";

    app.resources.push_back(slimr::Resource{
        .uri = "greeter://etiquette",
        .name = "etiquette",
        .description = "Greeting conventions",
        .mimeType = "text/markdown",
        .load = []() { return "# Always be polite\n"; }
    });

    app.tool<GreetInput>(
        "greet",
        "Greet a user by name",
        slimr::ToolAnnotations{.readOnlyHint = true, .openWorldHint = false},
        [](const GreetInput& in) {
            std::string msg = "Hello, " + in.name.get() + "!";
            if (in.loud.get()) {
                for (char& c : msg) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }
            auto out = slimr::json_obj();
            out["message"] = msg;
            return out;
        }
    );

    return app;
}
```

### 2. Standalone Entrypoint with Streamable HTTP & OAuth (`src/main.cpp`)

Use `slimr::mcp` alone for stdio+CLI, or link `slimr::mcp_http` (via the `"http"` vcpkg feature) to attach `http` subcommand support:

```cpp
#include "app.hpp"
#include <mcp_http.hpp>

int main(int argc, char** argv) {
    auto app = make_greet_app();
    slimr::HttpServerOptions httpOpts;
    httpOpts.host = "127.0.0.1";
    httpOpts.port = 8081;
    // Configure httpOpts.oauth (OAuthResourceConfig) for RFC 9728 metadata,
    // bearer validation, and required scopes. Consumers own any AS endpoints
    // via httpOpts.customRoutes.
    slimr::enable_http(app, std::move(httpOpts));
    return app.run(argc, argv);
}
```

---

## Resource Server OAuth & Security Boundaries

- **Standard Resource Server (`mcp_oauth.hpp`):** `OAuthResourceConfig` fails closed when enabled: bearer tokens must come in the `Authorization: Bearer ...` header (`?access_token=` is rejected with `401`), and `TokenValidationResult` checks issuer, audience, expiration, and required tool scopes (`403 insufficient_scope`). `/.well-known/oauth-protected-resource` advertises RFC 9728 metadata without requiring auth.
- **Authorization Server ownership:** `packages/mcp` deliberately does **not** ship a hosted IdP, credential broker, or database persistence. Applications needing an authorization flow (consent/pairing UI, PKCE token issuance) mount explicit routes via `HttpServerOptions::customRoutes` or point `authorizationServers` to an external AS.
- **HTTP server defaults:** Loopback-only bind by default (`127.0.0.1` / `::1`, refusing port `0` unless `allowEphemeralPort = true`), strict `Host` and `Origin` `403` checks against DNS rebinding, static serving disabled, bounded worker threads/queue (`503` on saturation while keeping fast `server/discover` responsive), `maxBodyBytes` `413` enforcement, and incremental chunked SSE with bounded backpressure (`sseHighWaterBytes`) and cooperative `shutdownTimeoutMs` drain.

---

## Cheat Sheet

| Goal | API |
| --- | --- |
| Create plugin app | `slimr::PluginApp app("key", "1.0.0")` |
| Register tool with context | `app.tool<InputStruct>("name", "desc", ToolOptions{...}, [](const InputStruct& in, const slimr::RequestContext& ctx) { ... })` |
| Register zero-arg tool | `app.tool<std::monostate>("status", "desc", annotations, [](const auto&) { ... })` |
| Return native MCP image/audio block | `slimr::McpResult::image(b64, "image/jpeg")` / `slimr::McpResult::audio(...)` |
| Request MRTR follow-up input | `slimr::McpResult::inputRequired("Need choice", jsonSchema, state)` |
| Return tool or protocol error | `slimr::McpResult::error("Not found")` / `throw slimr::McpProtocolException(slimr::McpErrorCode::InputRequired, ...)` |
| Attach Streamable HTTP CLI subcommand | `slimr::enable_http(app, httpOpts)` (links `slimr::mcp_http`) |
| Run CLI, `mcp`, or `http` | `app.run(argc, argv)` |

## Build & Test

This folder owns its headers, `test.cpp`, CMake target/config, manifest, port recipe and licenses. See the [integration guide](../../README.md#integration-vcpkg--cmakeninja).

```bash
just test-pkg mcp  # Automatic native deps + ASan/UBSan
just build-pkg mcp # Optimized Release build + tests
```

Stdio-only consumers link `slimr::mcp`; HTTP/OAuth servers enable the `"http"` feature in `vcpkg.json` (`{"name": "slimr-mcp", "features": ["http"]}`) and link `slimr::mcp_http` (which pulls `libhv::hv_static`).
