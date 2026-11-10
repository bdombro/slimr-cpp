#pragma once

#include "slimr/bugsink.hpp"
#include "slimr/json.hpp"
#include "slimr/reflect.hpp"

#include <algorithm>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace slimr {

struct ContentBlock {
    std::string type = "text"; // "text", "image", "audio"
    std::string text;
    std::string data;     // base64 for image/audio
    std::string mimeType; // for image/audio
};

inline Json content_to_json(const ContentBlock& b) {
    Json j = Json::object_t{};
    j["type"] = b.type;
    if (b.type == "text") {
        j["text"] = b.text;
    } else {
        j["data"] = b.data;
        j["mimeType"] = b.mimeType;
    }
    return j;
}

struct McpResult {
    std::vector<ContentBlock> content;
    bool isError = false;

    static McpResult text(std::string_view t, bool error = false) {
        return McpResult{.content = {ContentBlock{.type = "text", .text = std::string(t)}}, .isError = error};
    }

    static McpResult image(std::string_view base64Data, std::string_view mime = "image/png") {
        return McpResult{.content = {ContentBlock{.type = "image", .data = std::string(base64Data), .mimeType = std::string(mime)}}, .isError = false};
    }

    static McpResult audio(std::string_view base64Data, std::string_view mime = "audio/ogg") {
        return McpResult{.content = {ContentBlock{.type = "audio", .data = std::string(base64Data), .mimeType = std::string(mime)}}, .isError = false};
    }

    static McpResult error(std::string_view msg) {
        return McpResult{.content = {ContentBlock{.type = "text", .text = std::string(msg)}}, .isError = true};
    }

    Json toJson() const {
        Json j = Json::object_t{};
        Json::array_t arr;
        for (const auto& b : content) {
            arr.push_back(content_to_json(b));
        }
        j["content"] = arr;
        j["isError"] = isError;
        return j;
    }
};

struct Resource {
    std::string uri;
    std::string name;
    std::string description;
    std::string mimeType = "text/plain";
    std::function<std::string()> load;
};

struct ToolAnnotations {
    std::optional<bool> readOnlyHint;
    std::optional<bool> openWorldHint;

    Json toJson() const {
        Json j = Json::object_t{};
        if (readOnlyHint.has_value()) j["readOnlyHint"] = *readOnlyHint;
        if (openWorldHint.has_value()) j["openWorldHint"] = *openWorldHint;
        return j;
    }
};

struct ToolDef {
    std::string name;
    std::string description;
    ToolAnnotations annotations;
    Json inputSchema;
    std::function<McpResult(const Json&)> handler;
};

class PluginApp {
public:
    std::string key;
    std::string version = "1.0.0";
    std::string description;
    std::string instructions;
    std::string bugsinkDsn;
    std::vector<Resource> resources;

    PluginApp() = default;
    PluginApp(PluginApp&&) noexcept = default;
    PluginApp& operator=(PluginApp&&) noexcept = default;
    explicit PluginApp(std::string appKey, std::string appVer = "1.0.0")
        : key(std::move(appKey)), version(std::move(appVer)) {}

    void setBugsink(std::string dsn, std::string envVar = "BUGSINK_DSN") {
        bugsinkDsn = dsn;
        bugsink_ = std::make_unique<BugsinkReporter>(std::move(dsn), std::move(envVar));
    }

    BugsinkReporter* bugsink() const noexcept { return bugsink_.get(); }

    template <typename InputStruct, typename Handler>
    PluginApp& tool(std::string name, std::string desc, ToolAnnotations annotations, Handler&& h) {
        ToolDef def;
        def.name = name;
        def.description = std::move(desc);
        def.annotations = annotations;
        def.inputSchema = schema<InputStruct>();

        def.handler = [this, toolName = name, h = std::forward<Handler>(h)](const Json& args_json) -> McpResult {
            InputStruct input{};
            if constexpr (!std::is_same_v<InputStruct, std::monostate>) {
                std::string serialized_args = slimr::write_json(args_json);
                auto ec = glz::read_json(input, serialized_args);
                if (ec) {
                    return McpResult::error("Invalid input parameters: " + glz::format_error(ec, serialized_args));
                }
            }

            try {
                using Ret = decltype(h(input));
                if constexpr (std::is_same_v<Ret, McpResult>) {
                    return h(input);
                } else if constexpr (std::is_same_v<Ret, std::string> || std::is_same_v<Ret, std::string_view> || std::is_same_v<Ret, const char*>) {
                    return McpResult::text(h(input));
                } else if constexpr (std::is_same_v<Ret, Json>) {
                    Json res = h(input);
                    return McpResult::text(slimr::write_json(res));
                } else if constexpr (std::is_void_v<Ret>) {
                    h(input);
                    return McpResult::text("{}");
                } else {
                    auto res = h(input);
                    return McpResult::text(slimr::write_json(res));
                }
            } catch (const std::exception& e) {
                if (bugsink_) {
                    bugsink_->reportError(e, Json{{"tool", toolName}});
                }
                return McpResult::error(e.what());
            } catch (...) {
                std::runtime_error unk("Unknown exception during tool execution");
                if (bugsink_) {
                    bugsink_->reportError(unk, Json{{"tool", toolName}});
                }
                return McpResult::error(unk.what());
            }
        };

        tools_[name] = std::move(def);
        return *this;
    }

    template <typename InputStruct, typename Handler>
    PluginApp& tool(std::string name, std::string desc, Handler&& h) {
        return tool<InputStruct>(std::move(name), std::move(desc), ToolAnnotations{}, std::forward<Handler>(h));
    }

    const std::map<std::string, ToolDef>& tools() const noexcept { return tools_; }

    PluginApp& registerTool(ToolDef def) {
        tools_[def.name] = std::move(def);
        return *this;
    }

    Json buildMcpResourceReadTool() const {
        Json tool = Json::object_t{};
        tool["name"] = "mcp_resource_read";
        tool["description"] = "Read an MCP resource by URI directly through a tool call (fallback for hosts that do not support MCP resources natively).";
        Json::array_t enum_uris;
        for (const auto& r : resources) {
            enum_uris.push_back(r.uri);
        }
        Json inputSchema = Json::object_t{};
        inputSchema["type"] = "object";
        inputSchema["properties"]["uri"]["type"] = "string";
        inputSchema["properties"]["uri"]["enum"] = enum_uris;
        inputSchema["required"] = Json::array_t{"uri"};
        inputSchema["additionalProperties"] = false;
        tool["inputSchema"] = inputSchema;
        return tool;
    }

    Json dispatchMcpJson(const Json& req) {
        if (!req.contains("jsonrpc") || req["jsonrpc"].get<std::string>() != "2.0") {
            Json err = Json::object_t{};
            err["jsonrpc"] = "2.0";
            if (req.contains("id")) err["id"] = req["id"];
            err["error"]["code"] = -32600;
            err["error"]["message"] = "Invalid Request";
            return err;
        }

        std::string method = req.contains("method") ? req["method"].get<std::string>() : "";
        bool hasId = req.contains("id");
        Json id = hasId ? req["id"] : Json{};

        if (method == "initialize") {
            std::string requestedProto = "";
            if (req.contains("params") && req["params"].contains("protocolVersion")) {
                requestedProto = req["params"]["protocolVersion"].get<std::string>();
            }
            std::string negotiated = "2025-11-25";
            if (requestedProto == "2025-11-25" || requestedProto == "2025-06-18" || requestedProto == "2024-11-05") {
                negotiated = requestedProto;
            }

            Json res = Json::object_t{};
            res["jsonrpc"] = "2.0";
            res["id"] = id;
            res["result"]["protocolVersion"] = negotiated;
            res["result"]["capabilities"]["tools"] = Json::object_t{};
            res["result"]["capabilities"]["resources"] = Json::object_t{};
            res["result"]["serverInfo"]["name"] = key.empty() ? "plugin-app" : key;
            res["result"]["serverInfo"]["version"] = version;
            if (!instructions.empty()) {
                res["result"]["instructions"] = instructions;
            }
            return res;
        }

        if (method == "ping") {
            Json res = Json::object_t{};
            res["jsonrpc"] = "2.0";
            res["id"] = id;
            res["result"] = Json::object_t{};
            return res;
        }

        if (method == "notifications/initialized" || method == "notifications/cancelled") {
            return Json{}; // No response for notifications
        }

        if (method == "tools/list") {
            Json res = Json::object_t{};
            res["jsonrpc"] = "2.0";
            res["id"] = id;
            Json::array_t toolList;

            for (const auto& [name, def] : tools_) {
                Json t = Json::object_t{};
                t["name"] = def.name;
                t["description"] = def.description;
                t["inputSchema"] = def.inputSchema;
                auto ann = def.annotations.toJson();
                if (!ann.empty()) {
                    t["annotations"] = ann;
                }
                toolList.push_back(t);
            }

            if (!resources.empty() && !tools_.contains("mcp_resource_read")) {
                toolList.push_back(buildMcpResourceReadTool());
            }

            res["result"]["tools"] = toolList;
            return res;
        }

        if (method == "tools/call") {
            if (!req.contains("params") || !req["params"].contains("name")) {
                Json err = Json::object_t{};
                err["jsonrpc"] = "2.0";
                err["id"] = id;
                err["error"]["code"] = -32602;
                err["error"]["message"] = "Invalid params: name required";
                return err;
            }
            std::string toolName = req["params"]["name"].get<std::string>();
            Json args = Json::object_t{};
            if (req["params"].contains("arguments") && req["params"]["arguments"].is_object()) {
                args = req["params"]["arguments"];
            }

            if (toolName == "mcp_resource_read") {
                std::string uri = args.contains("uri") ? args["uri"].get<std::string>() : "";
                for (const auto& r : resources) {
                    if (r.uri == uri) {
                        try {
                            std::string content = r.load ? r.load() : "";
                            Json res = Json::object_t{};
                            res["jsonrpc"] = "2.0";
                            res["id"] = id;
                            res["result"] = McpResult::text(content).toJson();
                            return res;
                        } catch (const std::exception& e) {
                            Json res = Json::object_t{};
                            res["jsonrpc"] = "2.0";
                            res["id"] = id;
                            res["result"] = McpResult::error(e.what()).toJson();
                            return res;
                        }
                    }
                }
                Json res = Json::object_t{};
                res["jsonrpc"] = "2.0";
                res["id"] = id;
                res["result"] = McpResult::error("Unknown resource URI: " + uri).toJson();
                return res;
            }

            auto it = tools_.find(toolName);
            if (it == tools_.end()) {
                std::string alt = toolName;
                std::replace(alt.begin(), alt.end(), '_', '-');
                it = tools_.find(alt);
                if (it == tools_.end()) {
                    alt = toolName;
                    std::replace(alt.begin(), alt.end(), '-', '_');
                    it = tools_.find(alt);
                }
            }
            if (it == tools_.end()) {
                Json err = Json::object_t{};
                err["jsonrpc"] = "2.0";
                err["id"] = id;
                err["error"]["code"] = -32602;
                err["error"]["message"] = "Tool not found: " + toolName;
                return err;
            }

            McpResult callRes = it->second.handler(args);
            Json res = Json::object_t{};
            res["jsonrpc"] = "2.0";
            res["id"] = id;
            res["result"] = callRes.toJson();
            return res;
        }

        if (method == "resources/list") {
            Json res = Json::object_t{};
            res["jsonrpc"] = "2.0";
            res["id"] = id;
            Json::array_t resList;
            for (const auto& r : resources) {
                Json item = Json::object_t{};
                item["uri"] = r.uri;
                item["name"] = r.name;
                item["description"] = r.description;
                item["mimeType"] = r.mimeType;
                resList.push_back(item);
            }
            res["result"]["resources"] = resList;
            return res;
        }

        if (method == "resources/read") {
            if (!req.contains("params") || !req["params"].contains("uri")) {
                Json err = Json::object_t{};
                err["jsonrpc"] = "2.0";
                err["id"] = id;
                err["error"]["code"] = -32602;
                err["error"]["message"] = "Invalid params: uri required";
                return err;
            }
            std::string uri = req["params"]["uri"].get<std::string>();
            for (const auto& r : resources) {
                if (r.uri == uri) {
                    try {
                        std::string text = r.load ? r.load() : "";
                        Json res = Json::object_t{};
                        res["jsonrpc"] = "2.0";
                        res["id"] = id;
                        Json::array_t contents;
                        Json c = Json::object_t{};
                        c["uri"] = r.uri;
                        c["mimeType"] = r.mimeType;
                        c["text"] = text;
                        contents.push_back(c);
                        res["result"]["contents"] = contents;
                        return res;
                    } catch (const std::exception& e) {
                        Json err = Json::object_t{};
                        err["jsonrpc"] = "2.0";
                        err["id"] = id;
                        err["error"]["code"] = -32603;
                        err["error"]["message"] = "Resource load failed: " + std::string(e.what());
                        return err;
                    }
                }
            }
            Json err = Json::object_t{};
            err["jsonrpc"] = "2.0";
            err["id"] = id;
            err["error"]["code"] = -32602;
            err["error"]["message"] = "Unknown resource: " + uri;
            return err;
        }

        // Method not found
        Json err = Json::object_t{};
        err["jsonrpc"] = "2.0";
        err["id"] = id;
        err["error"]["code"] = -32601;
        err["error"]["message"] = "Method not found: " + method;
        return err;
    }

    void serveMcpStdio() {
        std::string line;
        while (std::getline(std::cin, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) {
                line.pop_back();
            }
            if (line.empty()) continue;

            auto parsed = parse_json<Json>(line);
            if (!parsed) {
                Json err = Json::object_t{};
                err["jsonrpc"] = "2.0";
                err["error"]["code"] = -32700;
                err["error"]["message"] = "Parse error";
                std::cout << slimr::write_json(err) << "\n" << std::flush;
                continue;
            }

            Json resp = dispatchMcpJson(*parsed);
            if (!resp.empty()) {
                std::cout << slimr::write_json(resp) << "\n" << std::flush;
            }
        }
    }

    int run(int argc, char* argv[]) {
        if (!bugsinkDsn.empty() && !bugsink_) {
            setBugsink(bugsinkDsn);
        }

        if (argc == 1 || (argc == 2 && (std::strcmp(argv[1], "--help") == 0 ||
                                        std::strcmp(argv[1], "-h") == 0 ||
                                        std::strcmp(argv[1], "--list") == 0))) {
            printHelp();
            return 0;
        }

        std::string firstArg = argv[1];
        if (firstArg == "mcp") {
            serveMcpStdio();
            return 0;
        }

        // CLI invocation of tool: <bin> <tool> ['{"arg":"val"}']
        auto it = tools_.find(firstArg);
        if (it == tools_.end()) {
            std::string alt = firstArg;
            std::replace(alt.begin(), alt.end(), '_', '-');
            it = tools_.find(alt);
            if (it == tools_.end()) {
                alt = firstArg;
                std::replace(alt.begin(), alt.end(), '-', '_');
                it = tools_.find(alt);
            }
        }
        if (it == tools_.end()) {
            std::cerr << "Unknown tool: " << firstArg << "\n";
            printHelp();
            return 1;
        }

        Json args = Json::object_t{};
        if (argc >= 3) {
            auto parsed = parse_json<Json>(argv[2]);
            if (!parsed) {
                std::cerr << "Invalid JSON arguments: " << parsed.error() << "\n";
                return 1;
            }
            args = *parsed;
        }

        McpResult res = it->second.handler(args);
        if (res.isError) {
            for (const auto& b : res.content) {
                std::cerr << b.text << "\n";
            }
            return 1;
        }

        for (const auto& b : res.content) {
            if (b.type == "text") {
                std::cout << b.text << "\n";
            } else {
                std::cout << slimr::write_json(content_to_json(b)) << "\n";
            }
        }
        return 0;
    }

    void printHelp() const {
        std::cout << "Usage:\n";
        std::cout << "  " << (key.empty() ? "app" : key) << " mcp\n";
        std::cout << "  " << (key.empty() ? "app" : key) << " <tool> ['{\"arg\":\"val\"}']\n\n";
        std::cout << "Available tools:\n";
        for (const auto& [name, def] : tools_) {
            std::cout << "  " << def.name << " - " << def.description << "\n";
            std::cout << "    Schema: " << slimr::write_json(def.inputSchema) << "\n";
        }
    }

private:
    std::map<std::string, ToolDef> tools_;
    std::unique_ptr<BugsinkReporter> bugsink_;
};

class Gateway {
public:
    std::string key = "assistant-mcp";
    std::string version = "1.0.0";
    std::string instructions;

    Gateway() = default;
    Gateway(std::string appKey, std::string appVer = "1.0.0")
        : key(std::move(appKey)), version(std::move(appVer)) {}

    void mount(std::string_view alias, PluginApp& app) {
        std::string prefix(alias);
        mounted_apps_[prefix] = &app;

        // Register tools with alias prefix
        for (const auto& [name, def] : app.tools()) {
            std::string normalized = name;
            std::replace(normalized.begin(), normalized.end(), '-', '_');
            std::string mountedName = normalized.starts_with(prefix + "_") ? normalized : (prefix + "_" + normalized);
            ToolDef mountedDef = def;
            mountedDef.name = mountedName;
            // Wrap handler to use app's bugsink on error
            mountedDef.handler = [&app, origHandler = def.handler, tName = mountedName](const Json& args) -> McpResult {
                try {
                    return origHandler(args);
                } catch (const std::exception& e) {
                    if (app.bugsink()) {
                        app.bugsink()->reportError(e, Json{{"tool", tName}});
                    }
                    return McpResult::error(e.what());
                }
            };
            tools_[mountedName] = std::move(mountedDef);
        }

        // Collect resources
        for (const auto& r : app.resources) {
            resources_.push_back(r);
        }

        // Merge instructions
        if (!app.instructions.empty()) {
            if (!instructions.empty()) instructions += "\n\n";
            instructions += "## " + prefix + "\n" + app.instructions;
        }
    }

    void mount(std::string_view alias, PluginApp&& app) {
        auto owned = std::make_unique<PluginApp>(std::move(app));
        PluginApp* ptr = owned.get();
        owned_apps_.push_back(std::move(owned));
        mount(alias, *ptr);
    }

    Json dispatchMcpJson(const Json& req) {
        PluginApp unified(key, version);
        unified.instructions = instructions;
        unified.resources = resources_;
        for (const auto& [name, def] : tools_) {
            unified.registerTool(def);
        }
        return unified.dispatchMcpJson(req);
    }

    int run(int argc, char* argv[]) {
        PluginApp unified(key, version);
        unified.instructions = instructions;
        unified.resources = resources_;
        for (const auto& [name, def] : tools_) {
            unified.registerTool(def);
        }
        return unified.run(argc, argv);
    }

    const std::map<std::string, ToolDef>& tools() const noexcept { return tools_; }
    const std::vector<Resource>& resources() const noexcept { return resources_; }

private:
    std::map<std::string, PluginApp*> mounted_apps_;
    std::vector<std::unique_ptr<PluginApp>> owned_apps_;
    std::map<std::string, ToolDef> tools_;
    std::vector<Resource> resources_;
};

} // namespace slimr
