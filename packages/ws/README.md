# slimr::ws (`packages/ws`)

Fast, clean RAII WebSocket client for C++26 built on top of libcurl's native WebSocket support (`CURLOPT_CONNECT_ONLY`, `curl_ws_send`, `curl_ws_recv`), featuring JSON message helpers and timeout management.

Borrowed directly from the design of [`@slimr/ws`](https://www.npmjs.com/package/@slimr/ws).

## Why `slimr::ws`?

- **Libcurl WebSocket Native:** Uses modern libcurl WebSocket APIs under the hood without needing heavy external socket libraries.
- **RAII Lifecycle:** Automatic connection teardown and handle cleanup on destruction or move assignment.
- **Convenient JSON Integration:** Send and receive `slimr::Json` objects directly alongside raw text frames.
- **Timeout & Polling Control:** Configurable timeouts for both connection establishment and frame reception.

---

## Quick Start

```cpp
#include <slimr/ws.hpp>
#include <iostream>

using namespace slimr;

int main() {
    WebSocket ws;

    // Connect with custom headers and timeout
    if (!ws.connect("wss://echo.websocket.events", {{"Authorization": "Bearer token"}}, 5000)) {
        std::cerr << "Failed to connect to WebSocket\n";
        return 1;
    }

    // Send text message
    ws.sendText("Hello Slimr WebSockets!");

    // Send JSON message
    ws.sendJson(json_obj({{"action", "ping"}, {"id", 123}}));

    // Receive response text
    if (auto reply = ws.recvText(3000)) {
        std::cout << "Received: " << *reply << "\n";
    }

    // Receive JSON response
    if (auto jsonReply = ws.recvJson(3000)) {
        std::cout << "Received JSON response successfully\n";
    }

    ws.close();
    return 0;
}
```

---

## API Cheat Sheet

| Member Function | Signature | Description |
| --- | --- | --- |
| `connect` | `bool connect(const std::string& url, const std::map<std::string, std::string>& headers = {}, long timeoutMs = 5000)` | Establishes WebSocket connection via libcurl. Returns `true` on success. |
| `sendText` | `bool sendText(std::string_view msg)` | Sends a text WebSocket frame. |
| `sendJson` | `bool sendJson(const Json& j)` | Serializes and sends `slimr::Json` as a text frame. |
| `recvText` | `std::optional<std::string> recvText(long timeoutMs = 5000)` | Receives text frame data up to `timeoutMs`. Returns `std::nullopt` on timeout or close. |
| `recvJson` | `std::optional<Json> recvJson(long timeoutMs = 5000)` | Receives and parses JSON response into `slimr::Json`. |
| `close` | `void close() noexcept` | Gracefully sends close frame and cleans up curl handles. |
| `isConnected` | `bool isConnected() const noexcept` | Returns `true` if currently connected. |
