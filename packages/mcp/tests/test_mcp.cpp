#define SLIMR_TEST_MAIN
#include "slimr/test.hpp"
#include "slimr/mcp.hpp"

using namespace slimr;

struct AddInput {
    Field<int, "First operand"> a;
    Field<int, "Second operand"> b;
};

TEST_SUITE("MCP Protocol & CLI") {
    TEST_CASE("PluginApp initialize and tools/list") {
        PluginApp app("test-app", "1.2.3");
        app.instructions = "Test instructions for agent";
        app.resources.push_back(Resource{
            .uri = "test://notes",
            .name = "test_resource",
            .description = "Test resource text",
            .mimeType = "text/plain",
            .load = []() { return "Hello from resource!"; }
        });

        app.tool<AddInput>("add", "Adds two numbers", [](const AddInput& in) {
            Json res = Json::object_t{};
            res["sum"] = in.a.get() + in.b.get();
            return res;
        });

        // Initialize
        Json initReq = Json::object_t{};
        initReq["jsonrpc"] = "2.0";
        initReq["id"] = 1;
        initReq["method"] = "initialize";
        initReq["params"]["protocolVersion"] = "2025-11-25";

        Json initResp = app.dispatchMcpJson(initReq);
        CHECK(initResp["jsonrpc"].get<std::string>() == "2.0");
        CHECK(initResp["result"]["protocolVersion"].get<std::string>() == "2025-11-25");
        CHECK(initResp["result"]["serverInfo"]["name"].get<std::string>() == "test-app");
        CHECK(initResp["result"]["serverInfo"]["version"].get<std::string>() == "1.2.3");
        CHECK(initResp["result"]["instructions"].get<std::string>() == "Test instructions for agent");

        // Ping
        Json pingReq = Json::object_t{};
        pingReq["jsonrpc"] = "2.0";
        pingReq["id"] = 2;
        pingReq["method"] = "ping";
        Json pingResp = app.dispatchMcpJson(pingReq);
        CHECK(pingResp["jsonrpc"].get<std::string>() == "2.0");
        CHECK(pingResp["id"].get<int64_t>() == 2);

        // tools/list (should include 'add' and auto-added 'mcp_resource_read')
        Json listReq = Json::object_t{};
        listReq["jsonrpc"] = "2.0";
        listReq["id"] = 3;
        listReq["method"] = "tools/list";
        Json listResp = app.dispatchMcpJson(listReq);
        auto tools = listResp["result"]["tools"].get<Json::array_t>();
        CHECK(tools.size() == 2);

        bool foundAdd = false;
        bool foundResourceRead = false;
        for (const auto& t : tools) {
            std::string tName = t["name"].get<std::string>();
            if (tName == "add") foundAdd = true;
            if (tName == "mcp_resource_read") foundResourceRead = true;
        }
        CHECK(foundAdd == true);
        CHECK(foundResourceRead == true);
    }

    TEST_CASE("tools/call: tool execution and media content blocks") {
        PluginApp app("media-app");

        app.tool<AddInput>("add", "Adds two numbers", [](const AddInput& in) {
            Json res = Json::object_t{};
            res["sum"] = in.a.get() + in.b.get();
            return res;
        });

        app.tool<std::monostate>("get_image", "Returns a mock image block", [](const std::monostate&) {
            return McpResult::image("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNk+M9QDwADhgGAWjR9awAAAABJRU5ErkJggg==", "image/png");
        });

        // Call add
        Json callAdd = Json::object_t{};
        callAdd["jsonrpc"] = "2.0";
        callAdd["id"] = 10;
        callAdd["method"] = "tools/call";
        callAdd["params"]["name"] = "add";
        callAdd["params"]["arguments"]["a"] = 20;
        callAdd["params"]["arguments"]["b"] = 22;

        Json addResp = app.dispatchMcpJson(callAdd);
        CHECK(addResp["result"]["isError"].get<bool>() == false);
        auto contents = addResp["result"]["content"].get<Json::array_t>();
        CHECK(contents.size() == 1);
        CHECK(contents[0]["type"].get<std::string>() == "text");
        CHECK(contents[0]["text"].get<std::string>().find("\"sum\":42") != std::string::npos);

        // Call get_image
        Json callImg = Json::object_t{};
        callImg["jsonrpc"] = "2.0";
        callImg["id"] = 11;
        callImg["method"] = "tools/call";
        callImg["params"]["name"] = "get_image";
        callImg["params"]["arguments"] = Json::object_t{};

        Json imgResp = app.dispatchMcpJson(callImg);
        CHECK(imgResp["result"]["isError"].get<bool>() == false);
        auto imgContents = imgResp["result"]["content"].get<Json::array_t>();
        CHECK(imgContents.size() == 1);
        CHECK(imgContents[0]["type"].get<std::string>() == "image");
        CHECK(imgContents[0]["mimeType"].get<std::string>() == "image/png");
        CHECK(imgContents[0]["data"].get<std::string>().find("iVBOR") != std::string::npos);
    }

    TEST_CASE("Resources list, read, and mcp_resource_read fallback") {
        PluginApp app("res-app");
        app.resources.push_back(Resource{
            .uri = "docs://intro",
            .name = "intro",
            .description = "Introduction doc",
            .mimeType = "text/markdown",
            .load = []() { return "# Intro Header"; }
        });

        // resources/list
        Json listReq = Json::object_t{};
        listReq["jsonrpc"] = "2.0";
        listReq["id"] = 20;
        listReq["method"] = "resources/list";
        Json listResp = app.dispatchMcpJson(listReq);
        auto resArr = listResp["result"]["resources"].get<Json::array_t>();
        CHECK(resArr.size() == 1);
        CHECK(resArr[0]["uri"].get<std::string>() == "docs://intro");

        // resources/read
        Json readReq = Json::object_t{};
        readReq["jsonrpc"] = "2.0";
        readReq["id"] = 21;
        readReq["method"] = "resources/read";
        readReq["params"]["uri"] = "docs://intro";
        Json readResp = app.dispatchMcpJson(readReq);
        auto contents = readResp["result"]["contents"].get<Json::array_t>();
        CHECK(contents.size() == 1);
        CHECK(contents[0]["text"].get<std::string>() == "# Intro Header");

        // tools/call with mcp_resource_read
        Json toolCallReq = Json::object_t{};
        toolCallReq["jsonrpc"] = "2.0";
        toolCallReq["id"] = 22;
        toolCallReq["method"] = "tools/call";
        toolCallReq["params"]["name"] = "mcp_resource_read";
        toolCallReq["params"]["arguments"]["uri"] = "docs://intro";
        Json toolCallResp = app.dispatchMcpJson(toolCallReq);
        CHECK(toolCallResp["result"]["isError"].get<bool>() == false);
        auto toolContents = toolCallResp["result"]["content"].get<Json::array_t>();
        CHECK(toolContents[0]["text"].get<std::string>() == "# Intro Header");
    }

    TEST_CASE("Gateway multi-plugin mounting and routing") {
        PluginApp notesApp("obsidian");
        notesApp.instructions = "Notes instructions";
        notesApp.tool<std::monostate>("read", "Reads note", [](const std::monostate&) {
            return "note content";
        });

        PluginApp logApp("personal-log");
        logApp.instructions = "Log instructions";
        logApp.tool<std::monostate>("search", "Searches log", [](const std::monostate&) {
            return "log entry";
        });

        Gateway gw;
        gw.mount("notes", notesApp);
        gw.mount("log", logApp);

        CHECK(gw.tools().contains("notes_read"));
        CHECK(gw.tools().contains("log_search"));
        CHECK(gw.instructions.find("## notes\nNotes instructions") != std::string::npos);
        CHECK(gw.instructions.find("## log\nLog instructions") != std::string::npos);

        // Dispatch call to notes_read
        Json callNotes = Json::object_t{};
        callNotes["jsonrpc"] = "2.0";
        callNotes["id"] = 30;
        callNotes["method"] = "tools/call";
        callNotes["params"]["name"] = "notes_read";
        callNotes["params"]["arguments"] = Json::object_t{};

        Json notesResp = gw.dispatchMcpJson(callNotes);
        CHECK(notesResp["result"]["isError"].get<bool>() == false);
        auto contents = notesResp["result"]["content"].get<Json::array_t>();
        CHECK(contents[0]["text"].get<std::string>() == "note content");
    }

    TEST_CASE("CLI direct invocation") {
        PluginApp app("cli-app");
        app.tool<AddInput>("add", "Adds two numbers", [](const AddInput& in) {
            Json res = Json::object_t{};
            res["result"] = in.a.get() + in.b.get();
            return res;
        });

        // Test tool with arguments via argv
        char arg0[] = "cli-app";
        char arg1[] = "add";
        char arg2[] = "{\"a\":15,\"b\":27}";
        char* argv[] = {arg0, arg1, arg2, nullptr};

        // Redirect stdout to capture
        std::stringstream ss;
        auto* oldBuf = std::cout.rdbuf(ss.rdbuf());
        int code = app.run(3, argv);
        std::cout.rdbuf(oldBuf);

        CHECK(code == 0);
        std::string output = ss.str();
        CHECK(output.find("\"result\":42") != std::string::npos);
    }

    TEST_CASE("PluginApp setBugsink populates bugsinkDsn") {
        PluginApp app("test-app", "1.0.0");
        CHECK(app.bugsinkDsn.empty());
        app.setBugsink("https://key@bugsink.example.com/1");
        CHECK(app.bugsinkDsn == "https://key@bugsink.example.com/1");
        CHECK(app.bugsink() != nullptr);
    }
}
