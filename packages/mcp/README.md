# slimr::mcp (`packages/mcp`)

MCP-first application framework and multi-plugin gateway for C++26 — turning reflected C++ structs into both a stdio Model Context Protocol server and a narrow JSON CLI in a single ~500 KB native binary.

## Why `slimr::mcp`?

Traditional CLI frameworks pull in thousands of lines of flag parsing (`--foo`, `-f`, positional varargs, shell completions) that AI agents never use and that duplicate MCP's JSON input schema. `slimr::mcp` eliminates that complexity:

1. **One struct, two surfaces:** Every tool takes a single reflected C++ struct (`slimr::Field<T, "description">` / `slimr::OptField<T, "description">`).
   - **`<bin> mcp`** runs the stdio NDJSON MCP server (`2025-11-25` / `2025-06-18` / `2024-11-05`) with full `inputSchema`, `annotations` (`readOnlyHint`, `openWorldHint`), `instructions`, `resources`, and an auto-registered `mcp_resource_read` fallback tool.
   - **`<bin> <tool> ['{"arg":"val"}']`** invokes `<tool>` directly from the terminal or a `launchd` job using the exact same JSON-to-struct deserializer (defaulting to `"{}"` when omitted).
2. **Multi-plugin `Gateway`:** Mount any number of `PluginApp` instances under aliases (`gw.mount("notes", make_obsidian_app())`) to produce a single binary with `<alias>_<tool>` routing, merged instructions, and merged resources.
3. **Native MCP Media Blocks:** Return `McpResult::image(base64, mime)` or `McpResult::audio(base64, mime)` directly from any tool handler.
4. **Built-in Bugsink Reporting:** Unhandled exceptions in tool handlers are automatically reported to Bugsink (`app.setBugsink(dsn)`) and returned as clean MCP/CLI errors.

---

## Quick Start

### 1. Define a `PluginApp` (`src/app.hpp`)

```cpp
#pragma once
#include <slimr/mcp.hpp>

struct GreetInput {
    slimr::Field<std::string, "Person to greet"> name;
    slimr::OptField<bool, "Use uppercase"> loud{false};
};

inline slimr::PluginApp make_greet_app() {
    slimr::PluginApp app("greeter", "1.0.0");
    app.description = "Greeting service over MCP and CLI";
    app.instructions = "Use `greet` to welcome users.";
    app.setBugsink("https://key@bugsink.btek.cc/1");

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

### 2. Standalone Entrypoint (`src/main.cpp`)

```cpp
#include "app.hpp"

int main(int argc, char** argv) {
    return make_greet_app().run(argc, argv);
}
```

### 3. Multi-Plugin Gateway (`src/gateway.cpp`)

```cpp
#include <slimr/mcp.hpp>
#include "../plugins/obsidian/src/app.hpp"
#include "../plugins/personal-log/src/app.hpp"

int main(int argc, char** argv) {
    slimr::Gateway gw("assistant", "2.0.0");
    gw.mount("notes", obsidian::make_obsidian_app());
    gw.mount("log", personal_log::make_personal_log_app());
    return gw.run(argc, argv);
}
```

---

## Cheat Sheet

| Goal | API |
| --- | --- |
| Create plugin app | `slimr::PluginApp app("key", "1.0.0")` |
| Register tool with input struct | `app.tool<InputStruct>("name", "desc", annotations, handler)` |
| Register zero-arg tool | `app.tool<std::monostate>("status", "desc", annotations, [](const auto&) { ... })` |
| Return native MCP image block | `return slimr::McpResult::image(b64, "image/jpeg");` |
| Return native MCP audio block | `return slimr::McpResult::audio(b64, "audio/ogg");` |
| Return tool error | `return slimr::McpResult::error("Not found");` (or `throw std::runtime_error(...)`) |
| Register MCP resource | `app.resources.push_back({.uri = ..., .name = ..., .load = ...})` |
| Mount into gateway | `gw.mount("alias", make_plugin_app())` |
| Run CLI or `mcp` server | `app.run(argc, argv)` / `gw.run(argc, argv)` |
