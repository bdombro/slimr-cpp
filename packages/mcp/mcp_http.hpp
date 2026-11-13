#pragma once
#include "mcp_oauth.hpp"

#include <hv/EventLoop.h>
#include <hv/HttpServer.h>
#include <hv/hlog.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <functional>
#include <future>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace slimr {

struct HttpServerOptions {
    std::string host{"127.0.0.1"};
    int port{0};
    bool allowEphemeralPort{false};
    bool allowNonLoopback{false};
    std::string mcpPath{"/mcp"};
    std::string publicBaseUrl;
    std::vector<std::string> allowedOrigins;
    std::vector<std::string> allowedHosts;
    size_t maxRequestBytes{4 * 1024 * 1024};
    uint32_t maxConnections{64};
    size_t workerThreads{4};
    size_t maxPendingJobs{32};
    size_t sseHighWaterBytes{32768};
    size_t sseMaxWriteBufBytes{65536};
    int shutdownTimeoutMs{5000};
    int keepaliveTimeoutMs{30000};
    OAuthResourceConfig oauth;
    std::vector<CustomHttpRoute> customRoutes;
};

namespace mcp_detail {

struct HttpListenSocket {
    int fd{-1};
    explicit HttpListenSocket(int value) : fd(value) {}
    HttpListenSocket(const HttpListenSocket&) = delete;
    HttpListenSocket& operator=(const HttpListenSocket&) = delete;
    ~HttpListenSocket() { if (fd >= 0) ::close(fd); }
};

inline bool is_valid_ip_literal(std::string_view host, bool* isIpv6 = nullptr, bool* isLoopback = nullptr) {
    if (host.empty() || host.size() >= 64) return false;
    std::string tmp(host);
    struct in_addr v4{};
    if (inet_pton(AF_INET, tmp.c_str(), &v4) == 1) {
        if (isIpv6) *isIpv6 = false;
        if (isLoopback) {
            uint32_t addr = ntohl(v4.s_addr);
            *isLoopback = ((addr >> 24U) == 127U);
        }
        return true;
    }
    struct in6_addr v6{};
    if (inet_pton(AF_INET6, tmp.c_str(), &v6) == 1) {
        if (isIpv6) *isIpv6 = true;
        if (isLoopback) {
            *isLoopback = IN6_IS_ADDR_LOOPBACK(&v6);
        }
        return true;
    }
    return false;
}

inline std::string format_authority_host(std::string_view host, bool isIpv6) {
    if (isIpv6 && !host.empty() && host.front() != '[') {
        return "[" + std::string(host) + "]";
    }
    return std::string(host);
}

inline std::string strip_http_params(std::string_view v) {
    auto semi = v.find(';');
    if (semi != std::string_view::npos) v = v.substr(0, semi);
    while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.remove_prefix(1);
    while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) v.remove_suffix(1);
    return ascii_lower(v);
}

inline bool is_json_content_type(std::string_view headerVal) {
    return strip_http_params(headerVal) == "application/json";
}

inline bool accepts_streamable_http(std::string_view acceptHeader) {
    bool hasJson = false;
    bool hasSse = false;
    size_t start = 0;
    while (start <= acceptHeader.size()) {
        auto comma = acceptHeader.find(',', start);
        std::string_view part = comma == std::string_view::npos
            ? acceptHeader.substr(start)
            : acceptHeader.substr(start, comma - start);
        std::string media = strip_http_params(part);
        if (media == "application/json") hasJson = true;
        if (media == "text/event-stream") hasSse = true;
        if (comma == std::string_view::npos) break;
        start = comma + 1;
    }
    return hasJson && hasSse;
}

/** Validates RFC 9110 field value bytes (0x21-0x7E, 0x20, 0x09) and decodes case-sensitive =?base64?<b64>?=. */
inline std::optional<std::string> decode_mcp_header_value(std::string_view raw) {
    for (unsigned char c : raw) {
        if (!((c >= 0x21 && c <= 0x7E) || c == 0x20 || c == 0x09)) {
            return std::nullopt;
        }
    }
    static constexpr std::string_view kPrefix = "=?base64?";
    static constexpr std::string_view kSuffix = "?=";
    if (raw.starts_with(kPrefix) && raw.ends_with(kSuffix) &&
        raw.size() >= kPrefix.size() + kSuffix.size()) {
        std::string_view inner = raw.substr(kPrefix.size(), raw.size() - kPrefix.size() - kSuffix.size());
        return base64_decode(inner);
    }
    return std::string(raw);
}

struct HeaderParamSpec {
    std::string paramPath;  // property name in arguments
    std::string headerName; // e.g., Mcp-Param-Region
    std::string schemaType; // "string", "integer", "number", "boolean"
};

inline std::vector<HeaderParamSpec> extract_x_mcp_headers(const Json& inputSchema) {
    std::vector<HeaderParamSpec> specs;
    if (!inputSchema.is_object() || !inputSchema.contains("properties") || !inputSchema["properties"].is_object()) {
        return specs;
    }
    for (const auto& [propName, propDef] : inputSchema["properties"].get_object()) {
        if (!propDef.is_object() || !propDef.contains("x-mcp-header")) continue;
        const Json& ext = propDef["x-mcp-header"];
        std::string suffix;
        if (ext.is_boolean() && ext.get<bool>()) {
            suffix = propName;
            if (!suffix.empty()) {
                suffix[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(suffix[0])));
            }
        } else if (ext.is_string()) {
            suffix = ext.get<std::string>();
        } else if (ext.is_object() && ext.contains("name") && ext["name"].is_string()) {
            suffix = ext["name"].get<std::string>();
        }
        if (suffix.empty()) continue;
        std::string headerName = suffix.starts_with("Mcp-Param-") ? suffix : ("Mcp-Param-" + suffix);
        std::string stype = propDef.contains("type") && propDef["type"].is_string()
            ? propDef["type"].get<std::string>()
            : "string";
        specs.push_back(HeaderParamSpec{
            .paramPath = propName,
            .headerName = std::move(headerName),
            .schemaType = std::move(stype),
        });
    }
    return specs;
}

inline bool header_matches_json_param(std::string_view decodedHeader,
                                      const Json& jsonVal,
                                      std::string_view schemaType) {
    if (jsonVal.is_string()) {
        return decodedHeader == jsonVal.get<std::string>();
    }
    if (jsonVal.is_boolean()) {
        return (jsonVal.get<bool>() && decodedHeader == "true") ||
               (!jsonVal.get<bool>() && decodedHeader == "false");
    }
    if (jsonVal.is_number() && !jsonVal.is_boolean()) {
        std::string hdrStr(decodedHeader);
        char* endPtr = nullptr;
        double hNum = std::strtod(hdrStr.c_str(), &endPtr);
        if (endPtr == hdrStr.c_str() || *endPtr != '\0' || !std::isfinite(hNum)) {
            return false;
        }
        if (jsonVal.is_int64() && schemaType == "integer") {
            return hNum == static_cast<double>(jsonVal.get<int64_t>());
        }
        return std::abs(hNum - jsonVal.get<double>()) <= 1e-9;
    }
    return false;
}

inline std::optional<std::string> normalize_origin(std::string_view origin) {
    auto schemeEnd = origin.find("://");
    if (schemeEnd == std::string_view::npos) return std::nullopt;
    std::string scheme = ascii_lower(origin.substr(0, schemeEnd));
    if (scheme != "http" && scheme != "https") return std::nullopt;
    std::string_view rest = origin.substr(schemeEnd + 3);
    if (rest.empty() || rest.find('/') != std::string_view::npos ||
        rest.find('?') != std::string_view::npos ||
        rest.find('#') != std::string_view::npos) {
        return std::nullopt;
    }
    return scheme + "://" + ascii_lower(rest);
}

inline std::string extract_url_origin(std::string_view url) {
    auto schemeEnd = url.find("://");
    if (schemeEnd == std::string_view::npos) return "";
    auto slash = url.find('/', schemeEnd + 3);
    std::string_view originPart = slash == std::string_view::npos ? url : url.substr(0, slash);
    return ascii_lower(originPart);
}

inline std::string extract_url_host(std::string_view url) {
    auto schemeEnd = url.find("://");
    if (schemeEnd == std::string_view::npos) return "";
    auto rest = url.substr(schemeEnd + 3);
    auto slash = rest.find('/');
    return ascii_lower(slash == std::string_view::npos ? rest : rest.substr(0, slash));
}

inline std::map<std::string, std::string> parse_query_params(std::string_view rawUrl) {
    std::map<std::string, std::string> out;
    auto qpos = rawUrl.find('?');
    if (qpos == std::string_view::npos) return out;
    std::string_view qs = rawUrl.substr(qpos + 1);
    auto hash = qs.find('#');
    if (hash != std::string_view::npos) qs = qs.substr(0, hash);

    auto urlDecode = [](std::string_view s) {
        std::string res;
        res.reserve(s.size());
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '+') {
                res.push_back(' ');
            } else if (s[i] == '%' && i + 2 < s.size()) {
                auto hexVal = [](char c) -> int {
                    if (c >= '0' && c <= '9') return c - '0';
                    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                    return -1;
                };
                int hi = hexVal(s[i + 1]);
                int lo = hexVal(s[i + 2]);
                if (hi >= 0 && lo >= 0) {
                    res.push_back(static_cast<char>((hi << 4) | lo));
                    i += 2;
                    continue;
                }
                res.push_back(s[i]);
            } else {
                res.push_back(s[i]);
            }
        }
        return res;
    };

    size_t pos = 0;
    while (pos <= qs.size()) {
        auto amp = qs.find('&', pos);
        std::string_view pair = amp == std::string_view::npos ? qs.substr(pos) : qs.substr(pos, amp - pos);
        if (!pair.empty()) {
            auto eq = pair.find('=');
            if (eq == std::string_view::npos) {
                out[urlDecode(pair)] = "";
            } else {
                out[urlDecode(pair.substr(0, eq))] = urlDecode(pair.substr(eq + 1));
            }
        }
        if (amp == std::string_view::npos) break;
        pos = amp + 1;
    }
    return out;
}

} // namespace mcp_detail

/** Bounded asynchronous Streamable HTTP and OAuth resource server for PluginApp. */
class McpHttpServer {
public:
    explicit McpHttpServer(PluginApp& app, HttpServerOptions opts = {})
        : app_(app), opts_(std::move(opts)) {}

    McpHttpServer(const McpHttpServer&) = delete;
    McpHttpServer& operator=(const McpHttpServer&) = delete;

    ~McpHttpServer() { stop(); }

    uint16_t boundPort() const noexcept { return boundPort_.load(std::memory_order_acquire); }

    std::string baseUrl() const {
        if (!opts_.publicBaseUrl.empty()) {
            std::string b = opts_.publicBaseUrl;
            while (b.size() > 1 && b.back() == '/') b.pop_back();
            return b;
        }
        bool isV6 = false;
        mcp_detail::is_valid_ip_literal(opts_.host, &isV6, nullptr);
        return "http://" + mcp_detail::format_authority_host(opts_.host, isV6) + ":" + std::to_string(boundPort());
    }

    std::string mcpEndpointUrl() const {
        return baseUrl() + normalizedMcpPath();
    }

    std::string resourceMetadataUrl() const {
        return baseUrl() + normalizedMetadataPath();
    }

    bool start(std::string* errOut = nullptr) {
        if (running_.load(std::memory_order_acquire)) {
            return true;
        }
        bool isV6 = false;
        bool isLoopback = false;
        if (!mcp_detail::is_valid_ip_literal(opts_.host, &isV6, &isLoopback)) {
            if (errOut) *errOut = "Invalid bind host IP literal: " + opts_.host;
            return false;
        }
        if (!isLoopback && !opts_.allowNonLoopback) {
            if (errOut) *errOut = "Non-loopback bind host refused unless allowNonLoopback=true: " + opts_.host;
            return false;
        }
        isLoopbackBind_ = isLoopback;

        if (opts_.port < 0 || opts_.port > 65535 || (opts_.port == 0 && !opts_.allowEphemeralPort)) {
            if (errOut) {
                *errOut = opts_.port == 0
                    ? "Port 0 requires allowEphemeralPort=true (use an explicit fixed port 1-65535 for services)"
                    : ("Invalid port: " + std::to_string(opts_.port));
            }
            return false;
        }
        if (opts_.mcpPath.empty() || opts_.mcpPath.front() != '/') {
            if (errOut) *errOut = "mcpPath must start with '/'";
            return false;
        }

        hlog_disable();
        service_ = std::make_unique<hv::HttpService>();
        service_->document_root.clear();
        service_->home_page.clear();
        service_->error_page.clear();
        service_->index_of.clear();
        service_->staticDirs.clear();
        service_->proxies.clear();
        service_->trustProxies.clear();
        service_->noProxies.clear();
        service_->keepalive_timeout = opts_.keepaliveTimeoutMs;
        service_->staticHandler = ::http_sync_handler([](::HttpRequest*, ::HttpResponse*) { return 404; });

        // Reject oversized Content-Length before reading large bodies.
        service_->headerHandler = http_ctx_handler([this](const HttpContextPtr& c) -> int {
            try {
                std::erase_if(responseWriters_, [](const auto& w) { return w.expired(); });
                if (std::ranges::none_of(responseWriters_, [&](const auto& w) {
                        return w.lock() == c->writer;
                    })) {
                    responseWriters_.push_back(c->writer);
                }
                if (draining_.load(std::memory_order_acquire)) {
                    sendResponse(c->writer, 503, "application/json", "{\"error\":\"server_stopping\"}",
                                 {{"Connection", "close"}});
                    return HTTP_STATUS_UNFINISHED;
                }
                std::string cl = c->header("Content-Length");
                if (!cl.empty()) {
                    char* endPtr = nullptr;
                    unsigned long long len = std::strtoull(cl.c_str(), &endPtr, 10);
                    if (endPtr == cl.c_str() || *endPtr != '\0' || len > opts_.maxRequestBytes) {
                        sendJsonText(c->writer, 413, "{\"error\":\"payload_too_large\"}");
                        return HTTP_STATUS_UNFINISHED;
                    }
                }
            } catch (...) {
                sendJsonText(c->writer, 400, "{\"error\":\"bad_request\"}");
                return HTTP_STATUS_UNFINISHED;
            }
            return HTTP_STATUS_NEXT;
        });

        // Protected Resource Metadata (RFC 9728)
        std::string metaPrimary = normalizedMetadataPath();
        std::string metaSuffixed = metaPrimary + normalizedMcpPath();
        auto metaHandler = http_ctx_handler([this](const HttpContextPtr& c) -> int {
            try {
                if (!validateOriginAndHost(c, true)) return HTTP_STATUS_UNFINISHED;
                Json doc = opts_.oauth.metadata.toJson(mcpEndpointUrl());
                sendJsonText(c->writer, 200, slimr::write_json(doc));
            } catch (const std::exception& e) {
                sendJsonText(c->writer, 500, "{\"error\":\"internal_error\",\"message\":\"" + redact_sensitive(e.what()) + "\"}");
            } catch (...) {
                sendJsonText(c->writer, 500, "{\"error\":\"internal_error\"}");
            }
            return HTTP_STATUS_UNFINISHED;
        });
        service_->GET(metaPrimary.c_str(), metaHandler);
        if (metaSuffixed != metaPrimary) {
            service_->GET(metaSuffixed.c_str(), metaHandler);
        }

        // Consumer-mounted custom routes (AS, pairing UI, health, etc.)
        for (const auto& route : opts_.customRoutes) {
            registerCustomRoute(route);
        }

        // Single Streamable HTTP endpoint: POST /mcp; 405 on other methods.
        std::string path = normalizedMcpPath();
        service_->POST(path.c_str(), http_ctx_handler([this](const HttpContextPtr& c) -> int {
            try {
                handleMcpPost(c);
            } catch (const std::exception& e) {
                sendJsonText(c->writer, 500,
                             slimr::write_json(*app_.makeErrorResponse(
                                 std::nullopt, McpErrorCode::InternalError, redact_sensitive(e.what()))));
            } catch (...) {
                sendJsonText(c->writer, 500, "{\"error\":\"internal_error\"}");
            }
            return HTTP_STATUS_UNFINISHED;
        }));
        auto methodNotAllowed = http_ctx_handler([this](const HttpContextPtr& c) -> int {
            if (!validateOriginAndHost(c, true)) return HTTP_STATUS_UNFINISHED;
            sendResponse(c->writer, 405, "application/json", "{\"error\":\"method_not_allowed\"}",
                         {{"Allow", "POST"}});
            return HTTP_STATUS_UNFINISHED;
        });
        service_->GET(path.c_str(), methodNotAllowed);
        service_->Delete(path.c_str(), methodNotAllowed);
        service_->PUT(path.c_str(), methodNotAllowed);

        server_ = std::make_unique<hv::HttpServer>(service_.get());
        server_->setHost(opts_.host.c_str());
        server_->setPort(opts_.port);
        server_->setThreadNum(1);
        server_->setMaxWorkerConnectionNum(opts_.maxConnections);

        // libhv only binds positive ports itself. Transfer an ephemeral listener
        // to its event loop on successful start; retain ownership on failure.
        mcp_detail::HttpListenSocket listener(opts_.port == 0 ? Listen(0, opts_.host.c_str()) : -1);
        if (opts_.port == 0) {
            if (listener.fd < 0) {
                server_.reset();
                service_.reset();
                if (errOut) *errOut = "Failed to bind ephemeral HTTP listener on " + opts_.host;
                return false;
            }
            server_->setListenFD(listener.fd);
        }

        draining_.store(false, std::memory_order_release);
        startWorkers();

        if (server_->start() != 0) {
            stopWorkers(true);
            server_.reset();
            service_.reset();
            if (errOut) {
                *errOut = "Failed to bind HTTP server on " + opts_.host + ":" + std::to_string(opts_.port);
            }
            return false;
        }
        listener.fd = -1;

        std::shared_ptr<hv::EventLoop> loop;
        while (!(loop = server_->loop(0)) || !loop->isRunning()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        loop_ = loop;

        uint16_t actualPort = 0;
        if (server_->listenfd[0] >= 0) {
            struct sockaddr_storage ss{};
            socklen_t len = sizeof(ss);
            if (getsockname(server_->listenfd[0], reinterpret_cast<struct sockaddr*>(&ss), &len) == 0) {
                if (ss.ss_family == AF_INET) {
                    actualPort = ntohs(reinterpret_cast<struct sockaddr_in*>(&ss)->sin_port);
                } else if (ss.ss_family == AF_INET6) {
                    actualPort = ntohs(reinterpret_cast<struct sockaddr_in6*>(&ss)->sin6_port);
                }
            }
        }
        if (actualPort == 0 && opts_.port > 0) {
            actualPort = static_cast<uint16_t>(opts_.port);
        }
        boundPort_.store(actualPort, std::memory_order_release);
        running_.store(true, std::memory_order_release);
        return true;
    }

    void stop() {
        bool wasRunning = running_.exchange(false, std::memory_order_acq_rel);
        if (!wasRunning) {
            stopWorkers(true);
            return;
        }

        draining_.store(true, std::memory_order_release);
        {
            std::lock_guard lock(workerState_->mu);
            workerState_->rejectNew = true;
        }
        auto loop = loop_.lock();
        callOnLoop(loop, [this, loop]() {
            // This server has one I/O loop. hio_close releases its listener;
            // http_server_stop owns loop/thread teardown, not a second fd close.
            for (int& fd : server_->listenfd) {
                if (fd >= 0) {
                    if (hio_close(hio_get(loop->loop(), fd)) != 0) {
                        throw std::runtime_error("Failed to close HTTP listener");
                    }
                    fd = -1;
                }
            }
        });

        // Wait up to shutdownTimeoutMs for natural completion of in-flight jobs before cancelling.
        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(std::max(0, opts_.shutdownTimeoutMs));
        {
            std::unique_lock lock(workerState_->mu);
            workerState_->idleCv.wait_until(lock, deadline, [&]() {
                return workerState_->queue.empty() && workerState_->activeCount == 0;
            });
            for (auto& wk : workerState_->activeCancels) {
                if (auto src = wk.lock()) src->cancel();
            }
        }

        stopWorkers(false);

        if (server_) {
            const auto flushDeadline = std::chrono::steady_clock::now() +
                std::chrono::milliseconds(std::clamp(opts_.shutdownTimeoutMs, 50, 500));
            // The first loop barrier also executes final-response callbacks
            // queued by workers before they exited, even with no buffered data.
            while (!callOnLoop(loop, [this]() {
                return std::ranges::none_of(responseWriters_, [](const auto& weak) {
                    auto w = weak.lock();
                    return w && w->id() == hio_id(w->io()) &&
                           hio_is_opened(w->io()) && !w->isWriteComplete();
                });
            })) {
                if (std::chrono::steady_clock::now() >= flushDeadline) {
                    std::cerr << "MCP HTTP shutdown: response flush timed out; closing pending connections\n";
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            server_->stop();
            server_.reset();
        }
        responseWriters_.clear();
        loop_.reset();
        service_.reset();
        boundPort_.store(0, std::memory_order_release);
    }

    int runUntilSignal() {
        static std::atomic<bool>* sStopFlag = nullptr;
        std::atomic<bool> stopRequested{false};
        sStopFlag = &stopRequested;

        auto sigHandler = [](int) {
            if (sStopFlag) sStopFlag->store(true, std::memory_order_release);
        };
        auto prevInt = std::signal(SIGINT, sigHandler);
        auto prevTerm = std::signal(SIGTERM, sigHandler);

        while (running_.load(std::memory_order_acquire) &&
               !stopRequested.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        std::signal(SIGINT, prevInt);
        std::signal(SIGTERM, prevTerm);
        sStopFlag = nullptr;
        stop();
        return 0;
    }

private:
    template<class Fn>
    static std::invoke_result_t<Fn> callOnLoop(const std::shared_ptr<hv::EventLoop>& loop, Fn fn) {
        auto task = std::make_shared<std::packaged_task<std::invoke_result_t<Fn>()>>(std::move(fn));
        auto result = task->get_future();
        loop->queueInLoop([task]() { (*task)(); });
        return result.get();
    }

    struct WorkerState {
        std::mutex mu;
        std::condition_variable cv;
        std::condition_variable idleCv;
        std::deque<std::function<void()>> queue;
        std::vector<std::weak_ptr<CancellationSource>> activeCancels;
        size_t activeCount{0};
        bool rejectNew{false};
        bool stopping{false};
    };

    std::string normalizedMcpPath() const {
        return opts_.mcpPath.empty() ? "/mcp" : opts_.mcpPath;
    }

    std::string normalizedMetadataPath() const {
        if (!opts_.oauth.metadataPath.empty()) return opts_.oauth.metadataPath;
        return "/.well-known/oauth-protected-resource";
    }

    static void sendResponse(const HttpResponseWriterPtr& w,
                             int status,
                             std::string_view contentType,
                             const std::string& body,
                             std::vector<std::pair<std::string, std::string>> extraHeaders = {}) {
        if (!w || !w->isOpened()) return;
        w->Begin();
        w->WriteStatus(static_cast<http_status>(status));
        std::string ct(contentType);
        w->WriteHeader("Content-Type", ct.c_str());
        for (const auto& [k, v] : extraHeaders) {
            w->WriteHeader(k.c_str(), v.c_str());
        }
        if (body.empty()) {
            w->End("", 0);
        } else if (std::in_range<int>(body.size())) {
            w->End(body.c_str(), static_cast<int>(body.size()));
        } else {
            w->End(body);
        }
    }

    static void sendJsonText(const HttpResponseWriterPtr& w,
                             int status,
                             const std::string& jsonStr,
                             std::vector<std::pair<std::string, std::string>> extraHeaders = {}) {
        sendResponse(w, status, "application/json", jsonStr, std::move(extraHeaders));
    }

    static HttpRouteRequest toRouteRequest(const HttpContextPtr& c) {
        HttpRouteRequest req;
        req.method = http_method_str(c->method());
        req.path = c->path();
        req.rawQuery = c->url();
        req.queryParams = mcp_detail::parse_query_params(c->url());
        for (const auto& [k, v] : c->headers()) {
            req.headers[mcp_detail::ascii_lower(k)] = v;
        }
        req.body = c->body();
        req.remoteAddr = c->ip() + ":" + std::to_string(c->port());
        return req;
    }

    bool isHostAllowed(std::string_view hostHeader) const {
        if (hostHeader.empty()) return true;
        std::string lower = mcp_detail::ascii_lower(hostHeader);
        for (const auto& allowed : opts_.allowedHosts) {
            if (mcp_detail::ascii_lower(allowed) == lower) return true;
        }
        if (!opts_.publicBaseUrl.empty()) {
            std::string pubHost = mcp_detail::extract_url_host(opts_.publicBaseUrl);
            if (!pubHost.empty() && pubHost == lower) return true;
        }
        uint16_t p = boundPort();
        std::string pSuffix = p > 0 ? (":" + std::to_string(p)) : "";
        bool isV6 = false;
        mcp_detail::is_valid_ip_literal(opts_.host, &isV6, nullptr);
        std::string boundAuthority = mcp_detail::ascii_lower(mcp_detail::format_authority_host(opts_.host, isV6));
        if (lower == boundAuthority || (!pSuffix.empty() && lower == boundAuthority + pSuffix)) {
            return true;
        }
        if (isLoopbackBind_) {
            if (lower == "localhost" || lower == "127.0.0.1" || lower == "[::1]") return true;
            if (!pSuffix.empty() &&
                (lower == "localhost" + pSuffix || lower == "127.0.0.1" + pSuffix || lower == "[::1]" + pSuffix)) {
                return true;
            }
        }
        return false;
    }

    bool isOriginAllowed(std::string_view originHeader) const {
        if (originHeader == "null") return false;
        auto normOpt = mcp_detail::normalize_origin(originHeader);
        if (!normOpt.has_value()) return false;
        const std::string& norm = *normOpt;

        for (const auto& allowed : opts_.allowedOrigins) {
            auto aNorm = mcp_detail::normalize_origin(allowed);
            if (aNorm.has_value() && *aNorm == norm) return true;
        }
        if (!opts_.publicBaseUrl.empty() && mcp_detail::extract_url_origin(opts_.publicBaseUrl) == norm) {
            return true;
        }
        uint16_t p = boundPort();
        if (p > 0) {
            bool isV6 = false;
            mcp_detail::is_valid_ip_literal(opts_.host, &isV6, nullptr);
            std::string auth = mcp_detail::ascii_lower(mcp_detail::format_authority_host(opts_.host, isV6));
            std::string boundOrigin = "http://" + auth + ":" + std::to_string(p);
            if (norm == boundOrigin) return true;
            if (isLoopbackBind_) {
                if (norm == ("http://127.0.0.1:" + std::to_string(p)) ||
                    norm == ("http://localhost:" + std::to_string(p)) ||
                    norm == ("http://[::1]:" + std::to_string(p))) {
                    return true;
                }
            }
        }
        return false;
    }

    bool validateOriginAndHost(const HttpContextPtr& c, bool checkOrigin) const {
        std::string hostHdr = c->header("Host");
        if (!isHostAllowed(hostHdr)) {
            sendJsonText(c->writer, 403, "{\"error\":\"forbidden\",\"error_description\":\"Invalid Host header\"}");
            return false;
        }
        if (checkOrigin) {
            std::string originHdr = c->header("Origin");
            if (!originHdr.empty() && !isOriginAllowed(originHdr)) {
                sendJsonText(c->writer, 403, "{\"error\":\"forbidden\",\"error_description\":\"Forbidden Origin\"}");
                return false;
            }
        }
        return true;
    }

    void registerCustomRoute(const CustomHttpRoute& route) {
        if (!route.handler || route.path.empty()) return;
        auto wrap = http_ctx_handler([this, route](const HttpContextPtr& c) -> int {
            try {
                if (!validateOriginAndHost(c, route.enforceSameOrigin)) {
                    return HTTP_STATUS_UNFINISHED;
                }
                if (c->body().size() > opts_.maxRequestBytes) {
                    sendJsonText(c->writer, 413, "{\"error\":\"payload_too_large\"}");
                    return HTTP_STATUS_UNFINISHED;
                }
                HttpRouteRequest rreq = toRouteRequest(c);
                HttpRouteResponse rresp = route.handler(rreq);
                std::vector<std::pair<std::string, std::string>> hdrs;
                hdrs.reserve(rresp.headers.size());
                for (const auto& [k, v] : rresp.headers) {
                    hdrs.emplace_back(k, v);
                }
                sendResponse(c->writer, rresp.status, rresp.contentType, rresp.body, std::move(hdrs));
            } catch (const std::exception& e) {
                sendJsonText(c->writer, 500, "{\"error\":\"internal_error\",\"message\":\"" + redact_sensitive(e.what()) + "\"}");
            } catch (...) {
                sendJsonText(c->writer, 500, "{\"error\":\"internal_error\"}");
            }
            return HTTP_STATUS_UNFINISHED;
        });

        std::string m = mcp_detail::ascii_lower(route.method);
        if (m == "get") service_->GET(route.path.c_str(), wrap);
        else if (m == "post") service_->POST(route.path.c_str(), wrap);
        else if (m == "delete") service_->Delete(route.path.c_str(), wrap);
        else if (m == "put") service_->PUT(route.path.c_str(), wrap);
        else if (m == "*") service_->Any(route.path.c_str(), wrap);
    }

    void sendOAuthUnauthorized(const HttpResponseWriterPtr& w,
                               std::span<const std::string> scopes,
                               std::string_view desc) const {
        std::string www = mcp_detail::build_www_authenticate(
            resourceMetadataUrl(), "", scopes, "");
        Json body = json_obj();
        body["error"] = "unauthorized";
        body["error_description"] = std::string(desc);
        sendJsonText(w, 401, slimr::write_json(body), {{"WWW-Authenticate", std::move(www)}});
    }

    void sendOAuthInsufficientScope(const HttpResponseWriterPtr& w,
                                    std::span<const std::string> scopes,
                                    std::string_view desc) const {
        std::string www = mcp_detail::build_www_authenticate(
            resourceMetadataUrl(), "insufficient_scope", scopes, desc);
        Json body = json_obj();
        body["error"] = "insufficient_scope";
        body["error_description"] = std::string(desc);
        sendJsonText(w, 403, slimr::write_json(body), {{"WWW-Authenticate", std::move(www)}});
    }

    std::optional<std::string> validateHttpMcpHeaders(const Json& req,
                                                      const HttpContextPtr& c,
                                                      const std::string& method) const {
        std::string protoHdr = c->header("MCP-Protocol-Version");
        if (protoHdr.empty()) {
            return "Missing required MCP-Protocol-Version header";
        }
        std::string bodyVersion;
        if (req.contains("params") && req["params"].is_object() &&
            req["params"].contains("_meta") && req["params"]["_meta"].is_object() &&
            req["params"]["_meta"].contains("io.modelcontextprotocol/protocolVersion") &&
            req["params"]["_meta"]["io.modelcontextprotocol/protocolVersion"].is_string()) {
            bodyVersion = req["params"]["_meta"]["io.modelcontextprotocol/protocolVersion"].get<std::string>();
        }
        if (bodyVersion != protoHdr) {
            return "Header mismatch: MCP-Protocol-Version '" + protoHdr + "' does not match body '_meta.io.modelcontextprotocol/protocolVersion'";
        }

        std::string methodHdr = c->header("Mcp-Method");
        if (methodHdr.empty()) {
            return "Missing required Mcp-Method header";
        }
        if (methodHdr != method) {
            return "Header mismatch: Mcp-Method '" + methodHdr + "' does not match body method '" + method + "'";
        }

        if (method == "tools/call" || method == "resources/read" || method == "prompts/get") {
            std::string rawNameHdr = c->header("Mcp-Name");
            if (rawNameHdr.empty()) {
                return "Missing required Mcp-Name header for method '" + method + "'";
            }
            auto decodedName = mcp_detail::decode_mcp_header_value(rawNameHdr);
            if (!decodedName.has_value()) {
                return "Malformed Mcp-Name header";
            }
            std::string bodyName;
            if (req.contains("params") && req["params"].is_object()) {
                const Json& params = req["params"];
                if (method == "resources/read" && params.contains("uri") && params["uri"].is_string()) {
                    bodyName = params["uri"].get<std::string>();
                } else if (params.contains("name") && params["name"].is_string()) {
                    bodyName = params["name"].get<std::string>();
                }
            }
            if (*decodedName != bodyName) {
                return "Header mismatch: Mcp-Name '" + *decodedName + "' does not match body value '" + bodyName + "'";
            }

            if (method == "tools/call") {
                const ToolDef* def = app_.findTool(bodyName);
                if (def != nullptr) {
                    auto headerSpecs = mcp_detail::extract_x_mcp_headers(def->inputSchema);
                    Json args = json_obj();
                    if (req["params"].contains("arguments") && req["params"]["arguments"].is_object()) {
                        args = req["params"]["arguments"];
                    }
                    for (const auto& spec : headerSpecs) {
                        std::string hdrVal = c->header(spec.headerName.c_str());
                        bool inBody = args.contains(spec.paramPath) && !args[spec.paramPath].is_null();
                        if (inBody && hdrVal.empty()) {
                            return "Missing required header " + spec.headerName + " for parameter '" + spec.paramPath + "'";
                        }
                        if (!hdrVal.empty()) {
                            auto decodedParam = mcp_detail::decode_mcp_header_value(hdrVal);
                            if (!decodedParam.has_value()) {
                                return "Malformed header " + spec.headerName;
                            }
                            if (!inBody) {
                                return "Header mismatch: unexpected " + spec.headerName + " when argument is absent or null";
                            }
                            if (!mcp_detail::header_matches_json_param(*decodedParam, args[spec.paramPath], spec.schemaType)) {
                                return "Header mismatch: " + spec.headerName + " does not match body parameter '" + spec.paramPath + "'";
                            }
                        }
                    }
                }
            }
        }
        return std::nullopt;
    }

    static int httpStatusForRpcError(int code) noexcept {
        switch (code) {
            case McpErrorCode::ParseError:
            case McpErrorCode::InvalidRequest:
            case McpErrorCode::HeaderMismatch:
            case McpErrorCode::MissingRequiredClientCapability:
            case McpErrorCode::UnsupportedProtocolVersion:
                return 400;
            case McpErrorCode::MethodNotFound:
                return 404;
            case McpErrorCode::InsufficientScope:
                return 403;
            default:
                return 200;
        }
    }

    void handleMcpPost(const HttpContextPtr& c) {
        if (!validateOriginAndHost(c, true)) return;

        if (!mcp_detail::is_json_content_type(c->header("Content-Type"))) {
            sendJsonText(c->writer, 415, "{\"error\":\"unsupported_media_type\"}");
            return;
        }

        if (c->body().size() > opts_.maxRequestBytes) {
            sendJsonText(c->writer, 413, "{\"error\":\"payload_too_large\"}");
            return;
        }

        auto parsed = parse_json<Json>(c->body());
        if (!parsed) {
            sendJsonText(c->writer, 400,
                         slimr::write_json(*app_.makeErrorResponse(std::nullopt, McpErrorCode::ParseError, "Parse error")));
            return;
        }
        const Json& req = *parsed;
        if (!req.is_object()) {
            sendJsonText(c->writer, 400,
                         slimr::write_json(*app_.makeErrorResponse(std::nullopt, McpErrorCode::InvalidRequest, "Invalid Request: expected JSON object")));
            return;
        }
        if (!req.contains("jsonrpc") || !req["jsonrpc"].is_string() || req["jsonrpc"].get<std::string>() != "2.0") {
            std::optional<Json> errId;
            if (req.contains("id") && mcp_detail::is_valid_rpc_id(req["id"])) errId = req["id"];
            sendJsonText(c->writer, 400,
                         slimr::write_json(*app_.makeErrorResponse(errId, McpErrorCode::InvalidRequest, "Invalid Request")));
            return;
        }

        const bool hasId = req.contains("id");
        if (hasId && !mcp_detail::is_valid_rpc_id(req["id"])) {
            sendJsonText(c->writer, 400,
                         slimr::write_json(*app_.makeErrorResponse(std::nullopt, McpErrorCode::InvalidRequest, "Invalid Request: id must be a non-null string or number")));
            return;
        }
        std::optional<Json> id = hasId ? std::optional<Json>(req["id"]) : std::nullopt;

        if (!req.contains("method") || !req["method"].is_string() || req["method"].get<std::string>().empty()) {
            sendJsonText(c->writer, 400,
                         slimr::write_json(*app_.makeErrorResponse(id, McpErrorCode::InvalidRequest, "Invalid Request: method must be a non-empty string")));
            return;
        }
        std::string method = req["method"].get<std::string>();

        if (hasId && !mcp_detail::accepts_streamable_http(c->header("Accept"))) {
            sendJsonText(c->writer, 406, "{\"error\":\"not_acceptable\",\"error_description\":\"Accept must include application/json and text/event-stream\"}");
            return;
        }

        Principal principal;
        const bool isDiscover = hasId && method == "server/discover";
        if (opts_.oauth.enabled && !(isDiscover && !opts_.oauth.protectDiscover)) {
            HttpRouteRequest rreq = toRouteRequest(c);
            TokenValidationResult authRes = mcp_detail::evaluate_bearer_auth(opts_.oauth, mcpEndpointUrl(), rreq);
            if (authRes.status == TokenValidationResult::Status::ServiceUnavailable) {
                sendJsonText(c->writer, 503, "{\"error\":\"service_unavailable\",\"error_description\":\"" + authRes.errorDescription + "\"}");
                return;
            }
            if (authRes.status == TokenValidationResult::Status::InvalidToken) {
                sendOAuthUnauthorized(c->writer, authRes.requiredScopes, authRes.errorDescription);
                return;
            }
            if (authRes.status == TokenValidationResult::Status::InsufficientScope) {
                sendOAuthInsufficientScope(c->writer, authRes.requiredScopes, authRes.errorDescription);
                return;
            }
            principal = std::move(authRes.principal);
        }

        if (!hasId) {
            // Notification accepted (no header-mirroring requirements in MCP 2026-07-28).
            sendResponse(c->writer, 202, "application/json", "");
            return;
        }

        if (method == "initialize") {
            Json err = app_.dispatchMcpJson(req);
            sendJsonText(c->writer, 404, slimr::write_json(err));
            return;
        }

        // Validate _meta envelope cleanly so UnsupportedProtocolVersion (-32022) and InvalidParams (-32602) precede/clarify header checks when needed.
        if (!req.contains("params") || !req["params"].is_object() ||
            !req["params"].contains("_meta") || !req["params"]["_meta"].is_object() ||
            !req["params"]["_meta"].contains("io.modelcontextprotocol/protocolVersion") ||
            !req["params"]["_meta"]["io.modelcontextprotocol/protocolVersion"].is_string() ||
            !req["params"]["_meta"].contains("io.modelcontextprotocol/clientCapabilities") ||
            !req["params"]["_meta"]["io.modelcontextprotocol/clientCapabilities"].is_object()) {
            Json err = app_.dispatchMcpJson(req);
            sendJsonText(c->writer, 400, slimr::write_json(err));
            return;
        }
        if (auto hdrErr = validateHttpMcpHeaders(req, c, method)) {
            Json err = *app_.makeErrorResponse(id, McpErrorCode::HeaderMismatch, *hdrErr);
            sendJsonText(c->writer, 400, slimr::write_json(err));
            return;
        }
        std::string reqVer = req["params"]["_meta"]["io.modelcontextprotocol/protocolVersion"].get<std::string>();
        if (reqVer != kMcpProtocolVersion) {
            Json err = app_.dispatchMcpJson(req);
            sendJsonText(c->writer, 400, slimr::write_json(err));
            return;
        }

        // Fast metadata lane: never blocked behind slow tool workers.
        if (method == "server/discover" || method == "tools/list" || method == "resources/list") {
            RequestContext baseCtx;
            baseCtx.principal = principal;
            Json resp = app_.dispatchMcpJson(req, baseCtx);
            sendJsonText(c->writer, 200, slimr::write_json(resp));
            return;
        }

        if (method != "tools/call" && method != "resources/read") {
            Json err = app_.dispatchMcpJson(req);
            sendJsonText(c->writer, 404, slimr::write_json(err));
            return;
        }

        dispatchToolOrResourceAsync(c, req, std::move(principal));
    }

    void dispatchToolOrResourceAsync(const HttpContextPtr& c,
                                     const Json& req,
                                     Principal principal) {
        auto loop = loop_.lock();
        if (!loop) {
            sendJsonText(c->writer, 503, "{\"error\":\"server_stopping\"}");
            return;
        }

        struct StreamSession {
            HttpResponseWriterPtr writer;
            std::shared_ptr<hv::EventLoop> loop;
            CancellationSource cancelSrc;
            std::mutex sseMu;
            bool sseStarted{false};
            bool finished{false};
            size_t droppedNotifications{0};
        };

        auto sess = std::make_shared<StreamSession>();
        sess->writer = c->writer;
        sess->loop = loop;
        if (std::in_range<int>(opts_.sseMaxWriteBufBytes)) {
            c->writer->setMaxWriteBufsize(opts_.sseMaxWriteBufBytes);
        }

        c->writer->onclose = [sess]() {
            sess->cancelSrc.cancel();
            std::lock_guard lk(sess->sseMu);
            sess->finished = true;
            if (sess->writer) {
                sess->writer->onclose = nullptr;
            }
        };

        auto startSseOnLoop = [sess]() {
            if (!sess->writer || !sess->writer->isOpened() || sess->cancelSrc.isCancelled()) return false;
            if (!sess->sseStarted) {
                sess->sseStarted = true;
                sess->writer->Begin();
                sess->writer->WriteStatus(HTTP_STATUS_OK);
                sess->writer->WriteHeader("Content-Type", "text/event-stream");
                sess->writer->WriteHeader("Cache-Control", "no-cache");
                sess->writer->WriteHeader("X-Accel-Buffering", "no");
                sess->writer->EndHeaders("Transfer-Encoding", "chunked");
            }
            return true;
        };

        const size_t highWater = opts_.sseHighWaterBytes;
        auto notifyChannel = std::make_shared<McpNotificationChannel>(
            [sess, startSseOnLoop, highWater](const Json& notif) {
                if (sess->cancelSrc.isCancelled()) return;
                std::string raw = slimr::write_json(notif);
                std::erase(raw, '\n');
                std::erase(raw, '\r');
                std::string eventPayload = "data: " + raw + "\n\n";
                sess->loop->queueInLoop([sess, startSseOnLoop, highWater, payload = std::move(eventPayload)]() {
                    if (sess->cancelSrc.isCancelled() || !sess->writer || !sess->writer->isOpened()) return;
                    std::lock_guard lk(sess->sseMu);
                    if (sess->finished) return;
                    if (!startSseOnLoop()) return;
                    if (sess->writer->writeBufsize() >= highWater) {
                        ++sess->droppedNotifications;
                        return;
                    }
                    if (std::in_range<int>(payload.size())) {
                        sess->writer->WriteChunked(payload.c_str(), static_cast<int>(payload.size()));
                    }
                });
            });

        auto job = [this, sess, req, principal = std::move(principal), notifyChannel]() mutable {
            RequestContext ctx;
            ctx.principal = std::move(principal);
            ctx.cancellation = sess->cancelSrc.token();
            ctx.notificationChannel = notifyChannel;

            Json resp;
            try {
                resp = app_.dispatchMcpJson(req, ctx);
            } catch (const std::exception& e) {
                std::optional<Json> rid;
                if (req.contains("id") && mcp_detail::is_valid_rpc_id(req["id"])) rid = req["id"];
                resp = *app_.makeErrorResponse(rid, McpErrorCode::InternalError, redact_sensitive(e.what()));
            } catch (...) {
                std::optional<Json> rid;
                if (req.contains("id") && mcp_detail::is_valid_rpc_id(req["id"])) rid = req["id"];
                resp = *app_.makeErrorResponse(rid, McpErrorCode::InternalError, "Internal error");
            }

            notifyChannel->close();

            sess->loop->queueInLoop([this, sess, resp = std::move(resp)]() {
                if (sess->cancelSrc.isCancelled() || !sess->writer || !sess->writer->isOpened()) {
                    if (sess->writer) sess->writer->onclose = nullptr;
                    sess->writer.reset();
                    return;
                }

                std::lock_guard lk(sess->sseMu);
                sess->finished = true;

                if (sess->sseStarted) {
                    std::string raw = slimr::write_json(resp);
                    std::erase(raw, '\n');
                    std::erase(raw, '\r');
                    std::string frame = "data: " + raw + "\n\n";
                    if (std::in_range<int>(frame.size())) {
                        sess->writer->WriteChunked(frame.c_str(), static_cast<int>(frame.size()));
                    }
                    sess->writer->End();
                    sess->writer->onclose = nullptr;
                    sess->writer.reset();
                    return;
                }

                int httpCode = 200;
                if (resp.contains("error") && resp["error"].is_object() &&
                    resp["error"].contains("code") && resp["error"]["code"].is_int64()) {
                    int rpcCode = static_cast<int>(resp["error"]["code"].get<int64_t>());
                    if (rpcCode == McpErrorCode::InsufficientScope) {
                        std::vector<std::string> reqScopes;
                        if (resp["error"].contains("data") && resp["error"]["data"].is_object() &&
                            resp["error"]["data"].contains("requiredScopes") &&
                            resp["error"]["data"]["requiredScopes"].is_array()) {
                            for (const auto& sc : resp["error"]["data"]["requiredScopes"].get_array()) {
                                if (sc.is_string()) reqScopes.push_back(sc.get<std::string>());
                            }
                        }
                        std::string desc = resp["error"].contains("message") && resp["error"]["message"].is_string()
                            ? resp["error"]["message"].get<std::string>()
                            : "Insufficient scope";
                        sendOAuthInsufficientScope(sess->writer, reqScopes, desc);
                        sess->writer->onclose = nullptr;
                        sess->writer.reset();
                        return;
                    }
                    httpCode = httpStatusForRpcError(rpcCode);
                }

                sendJsonText(sess->writer, httpCode, slimr::write_json(resp));
                sess->writer->onclose = nullptr;
                sess->writer.reset();
            });
        };

        {
            std::lock_guard lock(workerState_->mu);
            if (workerState_->rejectNew || workerState_->stopping ||
                workerState_->queue.size() >= opts_.maxPendingJobs) {
                notifyChannel->close();
                sendResponse(c->writer, 503, "application/json",
                             "{\"error\":\"server_busy\",\"error_description\":\"Worker queue full\"}",
                             {{"Retry-After", "1"}});
                c->writer->onclose = nullptr;
                return;
            }
            // Note: track cancelSrc weakly so stop() can cooperatively cancel long-running tools past timeout.
            std::erase_if(workerState_->activeCancels, [](const auto& w) { return w.expired(); });
            // Keep a shared alias inside the queued task so weak_ptr stays valid while executing.
            auto cancelHandle = std::make_shared<CancellationSource>(sess->cancelSrc);
            workerState_->activeCancels.push_back(cancelHandle);
            workerState_->queue.push_back([job = std::move(job), cancelHandle]() mutable {
                (void)cancelHandle;
                job();
            });
        }
        workerState_->cv.notify_one();
    }

    void startWorkers() {
        workerState_ = std::make_shared<WorkerState>();
        size_t n = std::max<size_t>(1, opts_.workerThreads);
        workers_.reserve(n);
        for (size_t i = 0; i < n; ++i) {
            workers_.emplace_back([state = workerState_]() {
                for (;;) {
                    std::function<void()> fn;
                    {
                        std::unique_lock lock(state->mu);
                        state->cv.wait(lock, [&]() {
                            return state->stopping || !state->queue.empty();
                        });
                        if (state->queue.empty() && state->stopping) return;
                        fn = std::move(state->queue.front());
                        state->queue.pop_front();
                        ++state->activeCount;
                    }
                    if (fn) fn();
                    {
                        std::lock_guard lock(state->mu);
                        if (state->activeCount > 0) --state->activeCount;
                        if (state->queue.empty() && state->activeCount == 0) {
                            state->idleCv.notify_all();
                        }
                    }
                }
            });
        }
    }

    void stopWorkers(bool cancelQueued) {
        if (!workerState_) return;
        {
            std::lock_guard lock(workerState_->mu);
            workerState_->stopping = true;
            if (cancelQueued) {
                workerState_->queue.clear();
            }
        }
        workerState_->cv.notify_all();
        for (auto& t : workers_) {
            if (t.joinable()) t.join();
        }
        workers_.clear();
    }

    PluginApp& app_;
    HttpServerOptions opts_;
    bool isLoopbackBind_{true};
    std::atomic<bool> running_{false};
    std::atomic<bool> draining_{false};
    std::atomic<uint16_t> boundPort_{0};
    std::unique_ptr<hv::HttpService> service_;
    std::unique_ptr<hv::HttpServer> server_;
    std::weak_ptr<hv::EventLoop> loop_;
    std::shared_ptr<WorkerState> workerState_;
    std::vector<std::thread> workers_;
    std::vector<std::weak_ptr<hv::HttpResponseWriter>> responseWriters_;
};

/** Attaches standalone `http [--host <addr>] [--port <n>] [--allow-ephemeral-port]` subcommand to PluginApp. */
inline void enable_http(PluginApp& app, HttpServerOptions defaults = {}) {
    app.setHttpRunner([defaults = std::move(defaults)](PluginApp& target, int argc, char** argv) -> int {
        HttpServerOptions opts = defaults;
        for (int i = 2; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "--host" && i + 1 < argc) {
                opts.host = argv[++i];
            } else if (arg == "--port" && i + 1 < argc) {
                std::string portStr = argv[++i];
                char* endPtr = nullptr;
                long parsedPort = std::strtol(portStr.c_str(), &endPtr, 10);
                if (endPtr == portStr.c_str() || *endPtr != '\0' || parsedPort < 0 || parsedPort > 65535) {
                    std::cerr << "Invalid --port value: " << portStr << "\n";
                    return 2;
                }
                opts.port = static_cast<int>(parsedPort);
            } else if (arg == "--allow-ephemeral-port") {
                opts.allowEphemeralPort = true;
            } else {
                std::cerr << "Unknown or incomplete HTTP option: " << arg << "\n";
                std::cerr << "Usage: " << (target.key.empty() ? "app" : target.key)
                          << " http [--host <addr>] [--port <n>] [--allow-ephemeral-port]\n";
                return 2;
            }
        }

        McpHttpServer server(target, std::move(opts));
        std::string errMsg;
        if (!server.start(&errMsg)) {
            std::cerr << "HTTP server failed to start: " << errMsg << "\n";
            return 1;
        }
        std::cerr << (target.key.empty() ? "app" : target.key)
                  << " listening on " << server.mcpEndpointUrl() << "\n" << std::flush;
        return server.runUntilSignal();
    });
}

} // namespace slimr
