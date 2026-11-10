#define SLIMR_TEST_MAIN
#include "slimr/test.hpp"
#include "slimr/ws.hpp"

using namespace slimr;

TEST_SUITE("slimr::ws") {
    TEST_CASE("WebSocket lifecycle and defaults") {
        WebSocket ws;
        CHECK(!ws.isConnected());
        CHECK(!ws.sendText("test"));
        CHECK(!ws.recvText(100).has_value());

        // Move construction
        WebSocket movedWs(std::move(ws));
        CHECK(!movedWs.isConnected());
    }

    TEST_CASE("WebSocket connection failure on unreachable endpoint") {
        WebSocket ws;
        bool connected = ws.connect("ws://127.0.0.1:1/nonexistent", {}, 200);
        CHECK(!connected);
        CHECK(!ws.isConnected());
    }
}
