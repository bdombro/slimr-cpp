#pragma once
#include "bugsink.hpp"
#include "json.hpp"
#include "reflect.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <variant>
#include <vector>

namespace slimr {

inline constexpr std::string_view kMcpProtocolVersion = "2026-07-28";

namespace McpErrorCode {
inline constexpr int ParseError = -32700;
inline constexpr int InvalidRequest = -32600;
inline constexpr int MethodNotFound = -32601;
inline constexpr int InvalidParams = -32602;
inline constexpr int InternalError = -32603;
inline constexpr int HeaderMismatch = -32020;
inline constexpr int MissingRequiredClientCapability = -32021;
inline constexpr int UnsupportedProtocolVersion = -32022;
inline constexpr int RequestCancelled = -32000;
inline constexpr int InsufficientScope = -32001;
} // namespace McpErrorCode

namespace mcp_detail {

inline std::string ascii_lower(std::string_view sv) {
    std::string out;
    out.reserve(sv.size());
    for (unsigned char c : sv) {
        out.push_back(static_cast<char>(std::tolower(c)));
    }
    return out;
}

inline bool is_sensitive_key(std::string_view key) {
    std::string lower = ascii_lower(key);
    return lower == "authorization" || lower == "proxy-authorization" ||
           lower == "cookie" || lower == "set-cookie" ||
           lower == "access_token" || lower == "refresh_token" ||
           lower == "id_token" || lower == "client_secret" ||
           lower == "token" || lower == "secret" ||
           lower == "password" || lower == "api_key" ||
           lower == "apikey" || lower == "code" ||
           lower == "code_verifier" || lower == "x-api-key";
}

inline void redact_param_in_place(std::string& text, std::string_view param) {
    if (text.empty() || param.empty()) return;
    std::string lower = ascii_lower(text);
    std::string needle = ascii_lower(param) + "=";
    size_t pos = 0;
    while ((pos = lower.find(needle, pos)) != std::string::npos) {
        if (pos > 0) {
            char prev = lower[pos - 1];
            if (std::isalnum(static_cast<unsigned char>(prev)) || prev == '_') {
                pos += needle.size();
                continue;
            }
        }
        size_t valStart = pos + needle.size();
        size_t valEnd = valStart;
        while (valEnd < text.size() && text[valEnd] != '&' &&
               text[valEnd] != ' ' && text[valEnd] != '"' &&
               text[valEnd] != '\'' && text[valEnd] != '\n' &&
               text[valEnd] != '\r') {
            ++valEnd;
        }
        text.replace(valStart, valEnd - valStart, "[REDACTED]");
        lower = ascii_lower(text);
        pos = valStart + 10;
    }
}

inline void redact_scheme_in_place(std::string& text, std::string_view scheme) {
    if (text.empty()) return;
    std::string lower = ascii_lower(text);
    std::string needle = ascii_lower(scheme) + " ";
    size_t pos = 0;
    while ((pos = lower.find(needle, pos)) != std::string::npos) {
        if (pos > 0 && std::isalnum(static_cast<unsigned char>(lower[pos - 1]))) {
            pos += needle.size();
            continue;
        }
        size_t valStart = pos + needle.size();
        while (valStart < text.size() && text[valStart] == ' ') ++valStart;
        size_t valEnd = valStart;
        while (valEnd < text.size() && text[valEnd] != ' ' &&
               text[valEnd] != '"' && text[valEnd] != '\'' &&
               text[valEnd] != '\n' && text[valEnd] != '\r' &&
               text[valEnd] != ',' && text[valEnd] != ';') {
            ++valEnd;
        }
        if (valEnd > valStart) {
            text.replace(valStart, valEnd - valStart, "[REDACTED]");
            lower = ascii_lower(text);
            pos = valStart + 10;
        } else {
            pos = valStart;
        }
    }
}

inline int log_level_severity(std::string_view level) noexcept {
    if (level == "debug") return 0;
    if (level == "info") return 1;
    if (level == "notice") return 2;
    if (level == "warning") return 3;
    if (level == "error") return 4;
    if (level == "critical") return 5;
    if (level == "alert") return 6;
    if (level == "emergency") return 7;
    return -1;
}

inline std::string id_key(const Json& id) {
    if (id.is_string()) {
        return "s:" + id.get<std::string>();
    }
    return "n:" + slimr::write_json(id);
}

inline bool is_valid_rpc_id(const Json& id) noexcept {
    if (id.is_string()) return true;
    if (id.is_number() && !id.is_boolean()) return true;
    return false;
}

inline bool is_valid_progress_token(const Json& tok) noexcept {
    return is_valid_rpc_id(tok);
}

inline void strip_trailing_ws(std::string& line) {
    while (!line.empty() &&
           (line.back() == '\r' || line.back() == '\n' ||
            line.back() == ' ' || line.back() == '\t')) {
        line.pop_back();
    }
}

} // namespace mcp_detail

/** Redacts bearer tokens, authorization headers, and secret URL query parameters from text. */
inline std::string redact_sensitive(std::string_view input) {
    std::string out(input);
    mcp_detail::redact_scheme_in_place(out, "Bearer");
    mcp_detail::redact_scheme_in_place(out, "Basic");
    static constexpr std::string_view kSensitiveParams[] = {
        "access_token", "refresh_token", "id_token", "client_secret",
        "code_verifier", "code", "token", "secret", "password",
        "api_key", "apikey", "key"
    };
    for (std::string_view p : kSensitiveParams) {
        mcp_detail::redact_param_in_place(out, p);
    }
    return out;
}

/** Recursively redacts sensitive keys and secret-bearing string values inside a JSON value. */
inline Json redact_json(const Json& val) {
    if (val.is_object()) {
        Json out = json_obj();
        for (const auto& [k, v] : val.get_object()) {
            if (mcp_detail::is_sensitive_key(k)) {
                out[k] = "[REDACTED]";
            } else {
                out[k] = redact_json(v);
            }
        }
        return out;
    }
    if (val.is_array()) {
        Json::array_t arr;
        arr.reserve(val.get_array().size());
        for (const auto& el : val.get_array()) {
            arr.push_back(redact_json(el));
        }
        return json_arr(std::move(arr));
    }
    if (val.is_string()) {
        return Json(redact_sensitive(val.get<std::string>()));
    }
    return val;
}

/** Authenticated caller identity and authorization grants for a single MCP request. */
struct Principal {
    bool authenticated{false};
    std::string subject;
    std::string clientId;
    std::string issuer;
    std::string audience;
    std::vector<std::string> scopes;
    std::optional<std::chrono::system_clock::time_point> expiresAt;
    Json claims = json_obj();

    bool hasScope(std::string_view scope) const noexcept {
        for (const auto& s : scopes) {
            if (s == scope) return true;
        }
        return false;
    }

    bool hasAllScopes(std::span<const std::string_view> required) const noexcept {
        for (std::string_view r : required) {
            if (!hasScope(r)) return false;
        }
        return true;
    }

    bool hasAllScopes(std::span<const std::string> required) const noexcept {
        for (const auto& r : required) {
            if (!hasScope(r)) return false;
        }
        return true;
    }
};

/** Thread-safe cooperative cancellation token observed by request handlers and sinks. */
class CancellationToken {
public:
    struct State {
        std::atomic<bool> cancelled{false};
        std::mutex mu;
        uint64_t nextId{1};
        std::vector<std::pair<uint64_t, std::function<void()>>> callbacks;
    };

    CancellationToken() = default;
    explicit CancellationToken(std::shared_ptr<State> state) noexcept
        : state_(std::move(state)) {}

    bool canBeCancelled() const noexcept { return state_ != nullptr; }

    bool isCancelled() const noexcept {
        return state_ && state_->cancelled.load(std::memory_order_acquire);
    }

    uint64_t onCancel(std::function<void()> cb) const {
        if (!state_ || !cb) return 0;
        std::unique_lock lock(state_->mu);
        if (state_->cancelled.load(std::memory_order_acquire)) {
            lock.unlock();
            try {
                cb();
            } catch (...) {
            }
            return 0;
        }
        uint64_t id = state_->nextId++;
        state_->callbacks.emplace_back(id, std::move(cb));
        return id;
    }

    void removeOnCancel(uint64_t id) const {
        if (!state_ || id == 0) return;
        std::lock_guard lock(state_->mu);
        std::erase_if(state_->callbacks, [id](const auto& entry) { return entry.first == id; });
    }

private:
    std::shared_ptr<State> state_;
};

/** Owns the trigger side of a CancellationToken. */
class CancellationSource {
public:
    CancellationSource() : state_(std::make_shared<CancellationToken::State>()) {}

    CancellationToken token() const noexcept { return CancellationToken(state_); }

    bool isCancelled() const noexcept {
        return state_->cancelled.load(std::memory_order_acquire);
    }

    void cancel() const noexcept {
        bool expected = false;
        if (!state_->cancelled.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            return;
        }
        std::vector<std::pair<uint64_t, std::function<void()>>> cbs;
        {
            std::lock_guard lock(state_->mu);
            cbs.swap(state_->callbacks);
        }
        for (auto& [id, fn] : cbs) {
            (void)id;
            try {
                if (fn) fn();
            } catch (...) {
            }
        }
    }

private:
    std::shared_ptr<CancellationToken::State> state_;
};

/** Protocol-level JSON-RPC exception propagated directly as a JSON-RPC error response. */
class McpProtocolException : public std::runtime_error {
public:
    int code{McpErrorCode::InternalError};
    Json data = Json{};

    McpProtocolException(int errCode, std::string message, Json errData = Json{})
        : std::runtime_error(std::move(message)), code(errCode), data(std::move(errData)) {}
};

/** Thread-safe request-scoped notification channel bound by the active transport. */
class McpNotificationChannel {
public:
    using SinkFn = std::function<void(const Json&)>;

    explicit McpNotificationChannel(SinkFn sink = nullptr) : sink_(std::move(sink)) {}

    void emit(const Json& notification) {
        SinkFn fn;
        {
            std::lock_guard lock(mu_);
            if (closed_ || !sink_) return;
            fn = sink_;
        }
        fn(notification);
    }

    void close() noexcept {
        std::lock_guard lock(mu_);
        closed_ = true;
        sink_ = nullptr;
    }

private:
    std::mutex mu_;
    bool closed_{false};
    SinkFn sink_;
};

/** Per-request metadata, caller principal, cancellation token, and notification emitters. */
struct RequestContext {
    std::string protocolVersion{std::string(kMcpProtocolVersion)};
    Json clientCapabilities = json_obj();
    Json clientInfo = json_obj();
    Json requestMeta = json_obj();
    std::optional<Json> requestId;
    std::optional<Json> progressToken;
    std::optional<std::string> minLogLevel;
    std::optional<Json> inputResponses;
    std::optional<std::string> requestState;
    Principal principal;
    CancellationToken cancellation;
    std::shared_ptr<McpNotificationChannel> notificationChannel;

    bool hasClientCapability(std::string_view cap) const noexcept {
        return clientCapabilities.is_object() && clientCapabilities.contains(cap);
    }

    void requireClientCapability(std::string_view cap, const Json& requirement = json_obj()) const {
        if (hasClientCapability(cap)) return;
        Json reqCaps = json_obj();
        reqCaps[std::string(cap)] = requirement;
        Json data = json_obj();
        data["requiredCapabilities"] = std::move(reqCaps);
        throw McpProtocolException(
            McpErrorCode::MissingRequiredClientCapability,
            "Client lacks required capability: " + std::string(cap),
            std::move(data));
    }

    void emitProgress(double progress,
                      std::optional<double> total = std::nullopt,
                      std::optional<std::string> message = std::nullopt) const {
        if (!progressToken.has_value() || !notificationChannel || cancellation.isCancelled()) {
            return;
        }
        Json params = json_obj();
        params["progressToken"] = *progressToken;
        params["progress"] = progress;
        if (total.has_value()) {
            params["total"] = *total;
        }
        if (message.has_value()) {
            params["message"] = *message;
        }
        Json notif = json_obj();
        notif["jsonrpc"] = "2.0";
        notif["method"] = "notifications/progress";
        notif["params"] = std::move(params);
        notificationChannel->emit(notif);
    }

    void emitLog(std::string_view level,
                 const Json& data,
                 std::optional<std::string> logger = std::nullopt) const {
        if (!minLogLevel.has_value() || !notificationChannel || cancellation.isCancelled()) {
            return;
        }
        int msgSev = mcp_detail::log_level_severity(level);
        int minSev = mcp_detail::log_level_severity(*minLogLevel);
        if (msgSev < 0 || minSev < 0 || msgSev < minSev) {
            return;
        }
        Json params = json_obj();
        params["level"] = std::string(level);
        if (logger.has_value()) {
            params["logger"] = *logger;
        }
        params["data"] = redact_json(data);
        Json notif = json_obj();
        notif["jsonrpc"] = "2.0";
        notif["method"] = "notifications/message";
        notif["params"] = std::move(params);
        notificationChannel->emit(notif);
    }
};

struct ContentBlock {
    std::string type;     // "text", "image", "audio", "resource"
    std::string text;     // for type == "text"
    std::string data;     // base64 for image/audio
    std::string mimeType; // e.g., "image/jpeg", "audio/ogg"
};

inline Json content_to_json(const ContentBlock& c) {
    Json j = json_obj();
    j["type"] = c.type;
    if (c.type == "text") {
        j["text"] = c.text;
    } else if (c.type == "image" || c.type == "audio") {
        j["data"] = c.data;
        j["mimeType"] = c.mimeType;
    }
    return j;
}

struct McpResult {
    std::vector<ContentBlock> content;
    bool isError{false};
    std::optional<Json> structuredContent;
    bool isInputRequired{false};
    Json inputRequests = json_obj();
    std::optional<std::string> requestState;

    static McpResult text(std::string s) {
        McpResult r;
        r.content.push_back(ContentBlock{.type = "text", .text = std::move(s), .data = {}, .mimeType = {}});
        return r;
    }

    static McpResult structured(Json obj, std::string fallbackText = "") {
        McpResult r;
        std::string txt = fallbackText.empty() ? slimr::write_json(obj) : std::move(fallbackText);
        r.content.push_back(ContentBlock{.type = "text", .text = std::move(txt), .data = {}, .mimeType = {}});
        r.structuredContent = std::move(obj);
        return r;
    }

    static McpResult image(std::string base64Data, std::string mimeType) {
        McpResult r;
        r.content.push_back(ContentBlock{.type = "image", .text = {}, .data = std::move(base64Data), .mimeType = std::move(mimeType)});
        return r;
    }

    static McpResult audio(std::string base64Data, std::string mimeType) {
        McpResult r;
        r.content.push_back(ContentBlock{.type = "audio", .text = {}, .data = std::move(base64Data), .mimeType = std::move(mimeType)});
        return r;
    }

    static McpResult error(std::string msg) {
        McpResult r;
        r.isError = true;
        r.content.push_back(ContentBlock{.type = "text", .text = std::move(msg), .data = {}, .mimeType = {}});
        return r;
    }

    static McpResult inputRequired(Json requests, std::optional<std::string> state = std::nullopt) {
        McpResult r;
        r.isInputRequired = true;
        r.inputRequests = std::move(requests);
        r.requestState = std::move(state);
        return r;
    }

    Json toJson() const {
        Json j = json_obj();
        if (isInputRequired) {
            j["resultType"] = "input_required";
            if (inputRequests.is_object() && !inputRequests.empty()) {
                j["inputRequests"] = inputRequests;
            }
            if (requestState.has_value()) {
                j["requestState"] = *requestState;
            }
            return j;
        }
        j["resultType"] = "complete";
        Json::array_t arr;
        arr.reserve(content.size());
        for (const auto& c : content) {
            arr.push_back(content_to_json(c));
        }
        j["content"] = json_arr(std::move(arr));
        if (structuredContent.has_value()) {
            j["structuredContent"] = *structuredContent;
        }
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
    int64_t ttlMs{0};
    std::string cacheScope{"private"};
    std::vector<std::string> requiredScopes;
};

struct ToolAnnotations {
    std::optional<bool> readOnlyHint;
    std::optional<bool> destructiveHint;
    std::optional<bool> idempotentHint;
    std::optional<bool> openWorldHint;

    Json toJson() const {
        Json j = json_obj();
        if (readOnlyHint.has_value()) j["readOnlyHint"] = *readOnlyHint;
        if (destructiveHint.has_value()) j["destructiveHint"] = *destructiveHint;
        if (idempotentHint.has_value()) j["idempotentHint"] = *idempotentHint;
        if (openWorldHint.has_value()) j["openWorldHint"] = *openWorldHint;
        return j;
    }
};

struct ToolOptions {
    ToolAnnotations annotations;
    bool threadSafe{false};
    std::vector<std::string> requiredScopes;
};

struct ToolDef {
    std::string name;
    std::string description;
    ToolAnnotations annotations;
    bool threadSafe{false};
    std::vector<std::string> requiredScopes;
    Json inputSchema;
    std::function<McpResult(const Json&, const RequestContext&)> handler;
};

enum class ConcurrencyPolicy {
    Serialized,
    Parallel
};

struct CachePolicy {
    int64_t discoverTtlMs{3600000};
    std::string discoverCacheScope{"public"};
    int64_t toolsListTtlMs{3600000};
    std::string toolsListCacheScope{"public"};
    int64_t resourcesListTtlMs{3600000};
    std::string resourcesListCacheScope{"public"};
};

class PluginApp {
public:
    using AuthorizerFn = std::function<bool(const Principal&, std::string_view operation, std::string& outError)>;
    using HttpRunnerFn = std::function<int(PluginApp&, int argc, char** argv)>;

    std::string key;
    std::string version = "1.0.0";
    std::string description;
    std::string instructions;
    std::string bugsinkDsn;
    std::vector<Resource> resources;
    ConcurrencyPolicy concurrency{ConcurrencyPolicy::Serialized};
    CachePolicy cachePolicy;
    std::vector<std::string> defaultRequiredScopes;
    AuthorizerFn authorizer;

    PluginApp() : runtime_(std::make_shared<RuntimeState>()) {}
    PluginApp(PluginApp&&) noexcept = default;
    PluginApp& operator=(PluginApp&&) noexcept = default;
    explicit PluginApp(std::string appKey, std::string appVer = "1.0.0")
        : key(std::move(appKey)), version(std::move(appVer)), runtime_(std::make_shared<RuntimeState>()) {}

    void setBugsink(std::string dsn, std::string envVar = "BUGSINK_DSN") {
        bugsinkDsn = dsn;
        runtime_->bugsink = std::make_shared<BugsinkReporter>(std::move(dsn), std::move(envVar));
    }

    BugsinkReporter* bugsink() const noexcept {
        return runtime_ ? runtime_->bugsink.get() : nullptr;
    }

    void setHttpRunner(HttpRunnerFn runner) {
        httpRunner_ = std::move(runner);
    }

    bool hasHttpRunner() const noexcept { return static_cast<bool>(httpRunner_); }

    template <typename InputStruct, typename Handler>
    PluginApp& tool(std::string name, std::string desc, ToolOptions opts, Handler&& h) {
        ToolDef def;
        def.name = name;
        def.description = std::move(desc);
        def.annotations = std::move(opts.annotations);
        def.threadSafe = opts.threadSafe;
        def.requiredScopes = std::move(opts.requiredScopes);
        def.inputSchema = schema<InputStruct>();

        std::weak_ptr<RuntimeState> weakRt = runtime_;
        def.handler = [weakRt, toolName = name, h = std::forward<Handler>(h)](
                          const Json& args_json, const RequestContext& ctx) mutable -> McpResult {
            InputStruct input{};
            if constexpr (!std::is_same_v<InputStruct, std::monostate>) {
                std::string serialized_args = slimr::write_json(args_json);
                auto ec = glz::read_json(input, serialized_args);
                if (ec) {
                    return McpResult::error("Invalid input parameters: " + glz::format_error(ec, serialized_args));
                }
            }

            if (ctx.cancellation.isCancelled()) {
                throw McpProtocolException(McpErrorCode::RequestCancelled, "Request cancelled");
            }

            try {
                auto invoke = [&]() -> decltype(auto) {
                    if constexpr (std::invocable<Handler&, const InputStruct&, const RequestContext&>) {
                        return h(input, ctx);
                    } else {
                        return h(input);
                    }
                };
                using Ret = decltype(invoke());
                if constexpr (std::is_same_v<Ret, McpResult>) {
                    return invoke();
                } else if constexpr (std::is_same_v<Ret, std::string> || std::is_same_v<Ret, std::string_view> || std::is_same_v<Ret, const char*>) {
                    return McpResult::text(invoke());
                } else if constexpr (std::is_same_v<Ret, Json>) {
                    Json res = invoke();
                    return McpResult::text(slimr::write_json(res));
                } else if constexpr (std::is_void_v<Ret>) {
                    invoke();
                    return McpResult::text("{}");
                } else {
                    auto res = invoke();
                    return McpResult::text(slimr::write_json(res));
                }
            } catch (const McpProtocolException&) {
                throw;
            } catch (const std::exception& e) {
                std::string safeMsg = redact_sensitive(e.what());
                if (auto rt = weakRt.lock()) {
                    if (rt->bugsink) {
                        Json extra = json_obj();
                        extra["tool"] = toolName;
                        rt->bugsink->reportError(std::runtime_error(safeMsg), extra);
                    }
                }
                return McpResult::error(safeMsg);
            } catch (...) {
                std::runtime_error unk("Unknown exception during tool execution");
                if (auto rt = weakRt.lock()) {
                    if (rt->bugsink) {
                        Json extra = json_obj();
                        extra["tool"] = toolName;
                        rt->bugsink->reportError(unk, extra);
                    }
                }
                return McpResult::error(unk.what());
            }
        };

        tools_[name] = std::move(def);
        return *this;
    }

    template <typename InputStruct, typename Handler>
    PluginApp& tool(std::string name, std::string desc, ToolAnnotations annotations, Handler&& h) {
        return tool<InputStruct>(std::move(name), std::move(desc), ToolOptions{.annotations = std::move(annotations)}, std::forward<Handler>(h));
    }

    template <typename InputStruct, typename Handler>
    PluginApp& tool(std::string name, std::string desc, Handler&& h) {
        return tool<InputStruct>(std::move(name), std::move(desc), ToolOptions{}, std::forward<Handler>(h));
    }

    const std::map<std::string, ToolDef>& tools() const noexcept { return tools_; }

    PluginApp& registerTool(ToolDef def) {
        tools_[def.name] = std::move(def);
        return *this;
    }

    Json buildMcpResourceReadTool() const {
        Json tool = json_obj();
        tool["name"] = "mcp_resource_read";
        tool["description"] = "Read an MCP resource by URI directly through a tool call (fallback for hosts that do not support MCP resources natively).";
        Json::array_t enum_uris;
        for (const auto& r : resources) {
            enum_uris.emplace_back(r.uri);
        }
        Json uriProp = json_obj();
        uriProp["type"] = "string";
        uriProp["enum"] = json_arr(std::move(enum_uris));
        Json props = json_obj();
        props["uri"] = std::move(uriProp);
        Json inputSchema = json_obj();
        inputSchema["type"] = "object";
        inputSchema["properties"] = std::move(props);
        inputSchema["required"] = json_arr(Json::array_t{Json("uri")});
        inputSchema["additionalProperties"] = false;
        tool["inputSchema"] = std::move(inputSchema);
        tool["annotations"] = ToolAnnotations{.readOnlyHint = true, .openWorldHint = false}.toJson();
        return tool;
    }

    /** Validates HTTP-bound header expectations against the parsed body; throws nothing and returns std::nullopt when valid. */
    std::optional<Json> makeErrorResponse(const std::optional<Json>& id, int code, std::string message, Json data = Json{}) const {
        Json err = json_obj();
        err["code"] = code;
        err["message"] = std::move(message);
        if (!data.is_null() && !(data.is_object() && data.empty() && code != McpErrorCode::MissingRequiredClientCapability)) {
            err["data"] = std::move(data);
        }
        Json resp = json_obj();
        resp["jsonrpc"] = "2.0";
        if (id.has_value()) {
            resp["id"] = *id;
        }
        resp["error"] = std::move(err);
        return resp;
    }

    /** Finds a registered tool by exact name or underscore/hyphen normalization. */
    const ToolDef* findTool(std::string_view toolName) const {
        std::string exact(toolName);
        if (auto it = tools_.find(exact); it != tools_.end()) {
            return &it->second;
        }
        std::string alt = exact;
        std::replace(alt.begin(), alt.end(), '_', '-');
        if (auto it = tools_.find(alt); it != tools_.end()) {
            return &it->second;
        }
        alt = exact;
        std::replace(alt.begin(), alt.end(), '-', '_');
        if (auto it = tools_.find(alt); it != tools_.end()) {
            return &it->second;
        }
        return nullptr;
    }

    const Resource* findResource(std::string_view uri) const noexcept {
        for (const auto& r : resources) {
            if (r.uri == uri) return &r;
        }
        return nullptr;
    }

    Json dispatchMcpJson(const Json& req, const RequestContext& baseCtx = RequestContext{}) const {
        if (!req.is_object() || !req.contains("jsonrpc") || !req["jsonrpc"].is_string() ||
            req["jsonrpc"].get<std::string>() != "2.0") {
            std::optional<Json> errId;
            if (req.is_object() && req.contains("id") && mcp_detail::is_valid_rpc_id(req["id"])) {
                errId = req["id"];
            }
            return *makeErrorResponse(errId, McpErrorCode::InvalidRequest, "Invalid Request");
        }

        const bool hasId = req.contains("id");
        if (hasId && !mcp_detail::is_valid_rpc_id(req["id"])) {
            return *makeErrorResponse(std::nullopt, McpErrorCode::InvalidRequest, "Invalid Request: id must be a non-null string or number");
        }
        std::optional<Json> id = hasId ? std::optional<Json>(req["id"]) : std::nullopt;

        if (!req.contains("method") || !req["method"].is_string() || req["method"].get<std::string>().empty()) {
            return *makeErrorResponse(id, McpErrorCode::InvalidRequest, "Invalid Request: method must be a non-empty string");
        }
        std::string method = req["method"].get<std::string>();

        if (!hasId) {
            return Json{};
        }

        if (method == "initialize") {
            return *makeErrorResponse(
                id,
                McpErrorCode::MethodNotFound,
                "Method 'initialize' is not supported; this server implements MCP 2026-07-28 only (use 'server/discover' and per-request '_meta')");
        }

        if (!req.contains("params") || !req["params"].is_object()) {
            return *makeErrorResponse(id, McpErrorCode::InvalidParams, "Invalid params: params object with _meta is required");
        }
        const Json& params = req["params"];
        if (!params.contains("_meta") || !params["_meta"].is_object()) {
            return *makeErrorResponse(id, McpErrorCode::InvalidParams, "Invalid params: params._meta object is required");
        }
        const Json& meta = params["_meta"];
        if (!meta.contains("io.modelcontextprotocol/protocolVersion") ||
            !meta["io.modelcontextprotocol/protocolVersion"].is_string()) {
            return *makeErrorResponse(id, McpErrorCode::InvalidParams, "Invalid params: _meta['io.modelcontextprotocol/protocolVersion'] string is required");
        }
        std::string reqVersion = meta["io.modelcontextprotocol/protocolVersion"].get<std::string>();
        if (reqVersion != kMcpProtocolVersion) {
            Json data = json_obj();
            data["supported"] = json_arr(Json::array_t{Json(std::string(kMcpProtocolVersion))});
            data["requested"] = reqVersion;
            return *makeErrorResponse(id, McpErrorCode::UnsupportedProtocolVersion,
                                      "Unsupported protocol version: " + reqVersion,
                                      std::move(data));
        }

        if (!meta.contains("io.modelcontextprotocol/clientCapabilities") ||
            !meta["io.modelcontextprotocol/clientCapabilities"].is_object()) {
            return *makeErrorResponse(id, McpErrorCode::InvalidParams, "Invalid params: _meta['io.modelcontextprotocol/clientCapabilities'] object is required");
        }

        if (meta.contains("io.modelcontextprotocol/clientInfo") &&
            !meta["io.modelcontextprotocol/clientInfo"].is_object()) {
            return *makeErrorResponse(id, McpErrorCode::InvalidParams, "Invalid params: _meta['io.modelcontextprotocol/clientInfo'] must be an object");
        }

        RequestContext ctx = baseCtx;
        ctx.protocolVersion = reqVersion;
        ctx.requestId = id;
        ctx.requestMeta = meta;
        ctx.clientCapabilities = meta["io.modelcontextprotocol/clientCapabilities"];
        if (meta.contains("io.modelcontextprotocol/clientInfo")) {
            ctx.clientInfo = meta["io.modelcontextprotocol/clientInfo"];
        }
        if (meta.contains("progressToken")) {
            if (!mcp_detail::is_valid_progress_token(meta["progressToken"])) {
                return *makeErrorResponse(id, McpErrorCode::InvalidParams, "Invalid params: _meta.progressToken must be a string or number");
            }
            ctx.progressToken = meta["progressToken"];
        }
        if (meta.contains("io.modelcontextprotocol/logLevel")) {
            if (!meta["io.modelcontextprotocol/logLevel"].is_string() ||
                mcp_detail::log_level_severity(meta["io.modelcontextprotocol/logLevel"].get<std::string>()) < 0) {
                return *makeErrorResponse(id, McpErrorCode::InvalidParams, "Invalid params: invalid _meta['io.modelcontextprotocol/logLevel']");
            }
            ctx.minLogLevel = meta["io.modelcontextprotocol/logLevel"].get<std::string>();
        }
        if (params.contains("inputResponses")) {
            if (!params["inputResponses"].is_object()) {
                return *makeErrorResponse(id, McpErrorCode::InvalidParams, "Invalid params: inputResponses must be an object");
            }
            ctx.inputResponses = params["inputResponses"];
        }
        if (params.contains("requestState")) {
            if (!params["requestState"].is_string()) {
                return *makeErrorResponse(id, McpErrorCode::InvalidParams, "Invalid params: requestState must be a string");
            }
            ctx.requestState = params["requestState"].get<std::string>();
        }

        if (method == "server/discover") {
            return makeResultResponse(*id, buildDiscoverResult());
        }

        if (method == "tools/list") {
            Json::array_t toolList;
            for (const auto& [name, def] : tools_) {
                (void)name;
                Json t = json_obj();
                t["name"] = def.name;
                t["description"] = def.description;
                t["inputSchema"] = def.inputSchema;
                auto ann = def.annotations.toJson();
                if (!ann.empty()) {
                    t["annotations"] = std::move(ann);
                }
                toolList.push_back(std::move(t));
            }
            if (!resources.empty() && !tools_.contains("mcp_resource_read")) {
                toolList.push_back(buildMcpResourceReadTool());
            }
            Json result = json_obj();
            result["resultType"] = "complete";
            result["tools"] = json_arr(std::move(toolList));
            result["ttlMs"] = cachePolicy.toolsListTtlMs;
            result["cacheScope"] = cachePolicy.toolsListCacheScope;
            attachServerMeta(result);
            return makeResultResponse(*id, std::move(result));
        }

        if (method == "tools/call") {
            if (!params.contains("name") || !params["name"].is_string() || params["name"].get<std::string>().empty()) {
                return *makeErrorResponse(id, McpErrorCode::InvalidParams, "Invalid params: name required");
            }
            std::string toolName = params["name"].get<std::string>();
            Json args = json_obj();
            if (params.contains("arguments")) {
                if (!params["arguments"].is_object()) {
                    return *makeErrorResponse(id, McpErrorCode::InvalidParams, "Invalid params: arguments must be an object");
                }
                args = params["arguments"];
            }

            if (toolName == "mcp_resource_read") {
                if (!args.contains("uri") || !args["uri"].is_string() || args["uri"].get<std::string>().empty()) {
                    return *makeErrorResponse(id, McpErrorCode::InvalidParams, "Invalid params: uri required");
                }
                std::string uri = args["uri"].get<std::string>();
                const Resource* r = findResource(uri);
                if (!r) {
                    return *makeErrorResponse(id, McpErrorCode::InvalidParams, "Unknown resource URI: " + uri);
                }
                if (auto authErr = checkAuthorization(ctx.principal, r->requiredScopes, "resources/read:" + uri, id)) {
                    return *authErr;
                }
                std::unique_lock<std::mutex> execLock;
                if (concurrency == ConcurrencyPolicy::Serialized && runtime_) {
                    execLock = std::unique_lock<std::mutex>(runtime_->execMutex);
                }
                if (ctx.cancellation.isCancelled()) {
                    return *makeErrorResponse(id, McpErrorCode::RequestCancelled, "Request cancelled");
                }
                try {
                    std::string content = r->load ? r->load() : "";
                    Json result = McpResult::text(std::move(content)).toJson();
                    attachServerMeta(result);
                    return makeResultResponse(*id, std::move(result));
                } catch (const McpProtocolException& pe) {
                    return *makeErrorResponse(id, pe.code, pe.what(), pe.data);
                } catch (const std::exception& e) {
                    Json result = McpResult::error(redact_sensitive(e.what())).toJson();
                    attachServerMeta(result);
                    return makeResultResponse(*id, std::move(result));
                }
            }

            const ToolDef* def = findTool(toolName);
            if (!def) {
                return *makeErrorResponse(id, McpErrorCode::InvalidParams, "Tool not found: " + toolName);
            }
            if (auto authErr = checkAuthorization(ctx.principal, def->requiredScopes, "tools/call:" + def->name, id)) {
                return *authErr;
            }

            std::unique_lock<std::mutex> execLock;
            if (!def->threadSafe && concurrency == ConcurrencyPolicy::Serialized && runtime_) {
                execLock = std::unique_lock<std::mutex>(runtime_->execMutex);
            }
            if (ctx.cancellation.isCancelled()) {
                return *makeErrorResponse(id, McpErrorCode::RequestCancelled, "Request cancelled");
            }
            try {
                McpResult callRes = def->handler(args, ctx);
                Json result = callRes.toJson();
                attachServerMeta(result);
                return makeResultResponse(*id, std::move(result));
            } catch (const McpProtocolException& pe) {
                return *makeErrorResponse(id, pe.code, pe.what(), pe.data);
            }
        }

        if (method == "resources/list") {
            Json::array_t resList;
            for (const auto& r : resources) {
                Json item = json_obj();
                item["uri"] = r.uri;
                item["name"] = r.name;
                item["description"] = r.description;
                item["mimeType"] = r.mimeType;
                resList.push_back(std::move(item));
            }
            Json result = json_obj();
            result["resultType"] = "complete";
            result["resources"] = json_arr(std::move(resList));
            result["ttlMs"] = cachePolicy.resourcesListTtlMs;
            result["cacheScope"] = cachePolicy.resourcesListCacheScope;
            attachServerMeta(result);
            return makeResultResponse(*id, std::move(result));
        }

        if (method == "resources/read") {
            if (!params.contains("uri") || !params["uri"].is_string() || params["uri"].get<std::string>().empty()) {
                return *makeErrorResponse(id, McpErrorCode::InvalidParams, "Invalid params: uri required");
            }
            std::string uri = params["uri"].get<std::string>();
            const Resource* r = findResource(uri);
            if (!r) {
                return *makeErrorResponse(id, McpErrorCode::InvalidParams, "Unknown resource URI: " + uri);
            }
            if (auto authErr = checkAuthorization(ctx.principal, r->requiredScopes, "resources/read:" + uri, id)) {
                return *authErr;
            }
            std::unique_lock<std::mutex> execLock;
            if (concurrency == ConcurrencyPolicy::Serialized && runtime_) {
                execLock = std::unique_lock<std::mutex>(runtime_->execMutex);
            }
            if (ctx.cancellation.isCancelled()) {
                return *makeErrorResponse(id, McpErrorCode::RequestCancelled, "Request cancelled");
            }
            try {
                std::string text = r->load ? r->load() : "";
                Json::array_t contents;
                Json c = json_obj();
                c["uri"] = r->uri;
                c["mimeType"] = r->mimeType;
                c["text"] = std::move(text);
                contents.push_back(std::move(c));

                Json result = json_obj();
                result["resultType"] = "complete";
                result["contents"] = json_arr(std::move(contents));
                bool mrtrRetry = ctx.inputResponses.has_value() || ctx.requestState.has_value();
                result["ttlMs"] = mrtrRetry ? 0 : r->ttlMs;
                result["cacheScope"] = mrtrRetry ? std::string("private") : r->cacheScope;
                attachServerMeta(result);
                return makeResultResponse(*id, std::move(result));
            } catch (const McpProtocolException& pe) {
                return *makeErrorResponse(id, pe.code, pe.what(), pe.data);
            } catch (const std::exception& e) {
                return *makeErrorResponse(id, McpErrorCode::InternalError,
                                          "Resource load failed: " + redact_sensitive(e.what()));
            }
        }

        return *makeErrorResponse(id, McpErrorCode::MethodNotFound, "Method not found: " + method);
    }

    /** Runs stdio NDJSON transport over custom or standard streams, with non-blocking cancellation support. */
    void serveMcpStdio(std::istream& in = std::cin,
                       std::ostream& out = std::cout,
                       std::ostream& err = std::cerr) const {
        struct StdioState {
            std::mutex outMutex;
            std::mutex queueMutex;
            std::condition_variable queueCv;
            std::queue<std::function<void()>> queue;
            bool stopping{false};
            std::mutex inflightMutex;
            std::unordered_map<std::string, std::shared_ptr<CancellationSource>> inflight;
        };

        auto state = std::make_shared<StdioState>();
        auto writeLine = [state, &out](const Json& msg) {
            std::string raw = slimr::write_json(msg);
            std::erase(raw, '\n');
            std::erase(raw, '\r');
            std::lock_guard lock(state->outMutex);
            out << raw << "\n" << std::flush;
        };

        static constexpr size_t kWorkerCount = 4;
        std::vector<std::thread> workers;
        workers.reserve(kWorkerCount);
        for (size_t i = 0; i < kWorkerCount; ++i) {
            workers.emplace_back([state]() {
                for (;;) {
                    std::function<void()> task;
                    {
                        std::unique_lock lock(state->queueMutex);
                        state->queueCv.wait(lock, [&]() {
                            return state->stopping || !state->queue.empty();
                        });
                        if (state->queue.empty() && state->stopping) return;
                        task = std::move(state->queue.front());
                        state->queue.pop();
                    }
                    if (task) task();
                }
            });
        }

        auto joinWorkers = [&]() {
            {
                std::lock_guard lock(state->queueMutex);
                state->stopping = true;
            }
            state->queueCv.notify_all();
            for (auto& w : workers) {
                if (w.joinable()) w.join();
            }
        };

        std::string line;
        try {
            while (std::getline(in, line)) {
                mcp_detail::strip_trailing_ws(line);
                if (line.empty()) continue;

                auto parsed = parse_json<Json>(line);
                if (!parsed) {
                    writeLine(*makeErrorResponse(std::nullopt, McpErrorCode::ParseError, "Parse error"));
                    continue;
                }

                const Json& req = *parsed;
                if (req.is_object() &&
                    req.contains("jsonrpc") && req["jsonrpc"].is_string() &&
                    req["jsonrpc"].get<std::string>() == "2.0" &&
                    !req.contains("id") &&
                    req.contains("method") && req["method"].is_string() &&
                    req["method"].get<std::string>() == "notifications/cancelled") {
                    if (req.contains("params") && req["params"].is_object() &&
                        req["params"].contains("requestId") &&
                        mcp_detail::is_valid_rpc_id(req["params"]["requestId"])) {
                        std::string keyId = mcp_detail::id_key(req["params"]["requestId"]);
                        std::shared_ptr<CancellationSource> src;
                        {
                            std::lock_guard lock(state->inflightMutex);
                            if (auto it = state->inflight.find(keyId); it != state->inflight.end()) {
                                src = it->second;
                            }
                        }
                        if (src) {
                            src->cancel();
                        }
                    }
                    continue;
                }

                const bool trackRequest = req.is_object() && req.contains("id") &&
                                          mcp_detail::is_valid_rpc_id(req["id"]);
                std::optional<std::string> keyId;
                auto cancelSrc = std::make_shared<CancellationSource>();
                if (trackRequest) {
                    keyId = mcp_detail::id_key(req["id"]);
                    std::lock_guard lock(state->inflightMutex);
                    state->inflight[*keyId] = cancelSrc;
                }

                {
                    std::lock_guard lock(state->queueMutex);
                    state->queue.push([this, state, writeLine, &err, req, keyId, cancelSrc]() {
                        RequestContext baseCtx;
                        baseCtx.cancellation = cancelSrc->token();
                        auto channel = std::make_shared<McpNotificationChannel>(
                            [writeLine, tok = baseCtx.cancellation](const Json& notif) {
                                if (!tok.isCancelled()) {
                                    writeLine(notif);
                                }
                            });
                        baseCtx.notificationChannel = channel;

                        Json resp;
                        try {
                            resp = dispatchMcpJson(req, baseCtx);
                        } catch (const std::exception& e) {
                            std::string safeErr = redact_sensitive(e.what());
                            err << "mcp dispatch error: " << safeErr << "\n" << std::flush;
                            if (keyId.has_value()) {
                                resp = *makeErrorResponse(req["id"], McpErrorCode::InternalError, safeErr);
                            }
                        } catch (...) {
                            err << "mcp dispatch unknown error\n" << std::flush;
                            if (keyId.has_value()) {
                                resp = *makeErrorResponse(req["id"], McpErrorCode::InternalError, "Internal error");
                            }
                        }

                        channel->close();

                        if (keyId.has_value()) {
                            std::lock_guard lock(state->inflightMutex);
                            state->inflight.erase(*keyId);
                        }

                        if (!resp.empty() && !cancelSrc->isCancelled()) {
                            writeLine(resp);
                        }
                    });
                }
                state->queueCv.notify_one();
            }
        } catch (...) {
            joinWorkers();
            throw;
        }
        joinWorkers();
    }

    int run(int argc, char* argv[]) {
        if (!bugsinkDsn.empty() && !bugsink()) {
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

        if (firstArg == "http") {
            if (!httpRunner_) {
                std::cerr << "HTTP transport is not enabled in this binary (link slimr::mcp_http).\n";
                return 1;
            }
            return httpRunner_(*this, argc, argv);
        }

        // CLI invocation of tool: <bin> <tool> ['{"arg":"val"}']
        const ToolDef* def = findTool(firstArg);
        if (!def) {
            std::cerr << "Unknown tool: " << firstArg << "\n";
            printHelp();
            return 1;
        }

        Json args = json_obj();
        if (argc >= 3) {
            auto parsed = parse_json<Json>(argv[2]);
            if (!parsed) {
                std::cerr << "Invalid JSON arguments: " << parsed.error() << "\n";
                return 1;
            }
            args = *parsed;
        }

        RequestContext cliCtx;
        McpResult res;
        try {
            res = def->handler(args, cliCtx);
        } catch (const std::exception& e) {
            std::cerr << redact_sensitive(e.what()) << "\n";
            return 1;
        }

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
        std::string prog = key.empty() ? "app" : key;
        std::cout << "Usage:\n";
        std::cout << "  " << prog << " mcp\n";
        if (httpRunner_) {
            std::cout << "  " << prog << " http [--host <addr>] [--port <n>]\n";
        }
        std::cout << "  " << prog << " <tool> ['{\"arg\":\"val\"}']\n\n";
        std::cout << "Available tools:\n";
        for (const auto& [name, def] : tools_) {
            (void)name;
            std::cout << "  " << def.name << " - " << def.description << "\n";
            std::cout << "    Schema: " << slimr::write_json(def.inputSchema) << "\n";
        }
    }

private:
    struct RuntimeState {
        std::shared_ptr<BugsinkReporter> bugsink;
        std::mutex execMutex;
    };

    Json serverInfoJson() const {
        Json info = json_obj();
        info["name"] = key.empty() ? "plugin-app" : key;
        info["version"] = version;
        return info;
    }

    void attachServerMeta(Json& resultObj) const {
        Json meta = json_obj();
        if (resultObj.contains("_meta") && resultObj["_meta"].is_object()) {
            meta = resultObj["_meta"];
        }
        meta["io.modelcontextprotocol/serverInfo"] = serverInfoJson();
        resultObj["_meta"] = std::move(meta);
    }

    Json makeResultResponse(const Json& id, Json resultObj) const {
        Json resp = json_obj();
        resp["jsonrpc"] = "2.0";
        resp["id"] = id;
        resp["result"] = std::move(resultObj);
        return resp;
    }

    Json buildDiscoverResult() const {
        Json caps = json_obj();
        if (!tools_.empty() || !resources.empty()) {
            caps["tools"] = json_obj();
        }
        if (!resources.empty()) {
            caps["resources"] = json_obj();
        }
        Json result = json_obj();
        result["resultType"] = "complete";
        result["supportedVersions"] = json_arr(Json::array_t{Json(std::string(kMcpProtocolVersion))});
        result["capabilities"] = std::move(caps);
        if (!instructions.empty()) {
            result["instructions"] = instructions;
        }
        result["ttlMs"] = cachePolicy.discoverTtlMs;
        result["cacheScope"] = cachePolicy.discoverCacheScope;
        attachServerMeta(result);
        return result;
    }

    std::optional<Json> checkAuthorization(const Principal& principal,
                                           std::span<const std::string> toolScopes,
                                           std::string_view operation,
                                           const std::optional<Json>& id) const {
        if (!principal.authenticated) {
            return std::nullopt;
        }
        std::vector<std::string> missing;
        for (const auto& s : defaultRequiredScopes) {
            if (!principal.hasScope(s)) missing.push_back(s);
        }
        for (const auto& s : toolScopes) {
            if (!principal.hasScope(s)) missing.push_back(s);
        }
        if (!missing.empty()) {
            Json data = json_obj();
            Json::array_t arr;
            for (const auto& m : missing) arr.emplace_back(m);
            data["requiredScopes"] = json_arr(std::move(arr));
            return makeErrorResponse(id, McpErrorCode::InsufficientScope,
                                     "Insufficient scope for " + std::string(operation),
                                     std::move(data));
        }
        if (authorizer) {
            std::string errMsg;
            if (!authorizer(principal, operation, errMsg)) {
                return makeErrorResponse(id, McpErrorCode::InsufficientScope,
                                         errMsg.empty() ? "Forbidden" : errMsg);
            }
        }
        return std::nullopt;
    }

    std::map<std::string, ToolDef> tools_;
    std::shared_ptr<RuntimeState> runtime_;
    HttpRunnerFn httpRunner_;
};

} // namespace slimr
