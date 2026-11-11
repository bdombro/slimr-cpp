#pragma once
#include "mcp.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace slimr {

namespace mcp_detail {

inline std::optional<std::string> base64_decode(std::string_view input) {
    static constexpr int kDec[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,62,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,63,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
    };

    std::string out;
    out.reserve((input.size() / 4) * 3);
    int val = 0;
    int valb = -8;
    for (size_t i = 0; i < input.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(input[i]);
        if (c == '=') {
            for (size_t j = i + 1; j < input.size(); ++j) {
                if (input[j] != '=') return std::nullopt;
            }
            break;
        }
        int d = kDec[c];
        if (d == -1) return std::nullopt;
        val = (val << 6) + d;
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<char>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

inline std::string base64_encode(std::string_view input) {
    static constexpr char kEnc[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((input.size() + 2) / 3) * 4);
    int val = 0;
    int valb = -6;
    for (unsigned char c : input) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(kEnc[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) {
        out.push_back(kEnc[((val << 8) >> (valb + 8)) & 0x3F]);
    }
    while (out.size() % 4 != 0) {
        out.push_back('=');
    }
    return out;
}

inline std::string join_scopes(const std::vector<std::string>& scopes) {
    std::string out;
    for (size_t i = 0; i < scopes.size(); ++i) {
        if (i > 0) out.push_back(' ');
        out += scopes[i];
    }
    return out;
}

} // namespace mcp_detail

/** Normalized HTTP request view exposed to bearer validators and custom consumer routes. */
struct HttpRouteRequest {
    std::string method;
    std::string path;
    std::string rawQuery;
    std::map<std::string, std::string> queryParams;
    std::map<std::string, std::string> headers; // lowercased header names
    std::string body;
    std::string remoteAddr;

    std::optional<std::string> header(std::string_view name) const {
        auto it = headers.find(mcp_detail::ascii_lower(name));
        if (it == headers.end()) return std::nullopt;
        return it->second;
    }

    std::optional<std::string> query(std::string_view name) const {
        auto it = queryParams.find(std::string(name));
        if (it == queryParams.end()) return std::nullopt;
        return it->second;
    }
};

/** HTTP response emitted by custom consumer routes (for example OAuth AS or QR UI endpoints). */
struct HttpRouteResponse {
    int status{200};
    std::string contentType{"application/json"};
    std::map<std::string, std::string> headers;
    std::string body;

    static HttpRouteResponse json(int statusCode, const Json& value) {
        HttpRouteResponse r;
        r.status = statusCode;
        r.contentType = "application/json";
        r.body = slimr::write_json(value);
        return r;
    }

    static HttpRouteResponse html(int statusCode, std::string htmlBody) {
        HttpRouteResponse r;
        r.status = statusCode;
        r.contentType = "text/html; charset=utf-8";
        r.body = std::move(htmlBody);
        return r;
    }

    static HttpRouteResponse redirect(std::string location, int statusCode = 302) {
        HttpRouteResponse r;
        r.status = statusCode;
        r.contentType = "text/plain; charset=utf-8";
        r.headers["Location"] = std::move(location);
        return r;
    }
};

/** Custom HTTP route mounted alongside /mcp on the same server instance. */
struct CustomHttpRoute {
    std::string method; // GET, POST, DELETE, etc. (or "*" for any)
    std::string path;
    bool enforceSameOrigin{true};
    std::function<HttpRouteResponse(const HttpRouteRequest&)> handler;
};

/** RFC 9728 Protected Resource Metadata published by the MCP resource server. */
struct OAuthResourceMetadata {
    std::string resource;
    std::vector<std::string> authorizationServers;
    std::vector<std::string> scopesSupported;
    std::vector<std::string> bearerMethodsSupported{"header"};
    std::string resourceName;
    std::string resourceDocumentation;

    Json toJson(std::string_view effectiveResource) const {
        Json j = json_obj();
        j["resource"] = resource.empty() ? std::string(effectiveResource) : resource;
        Json::array_t asArr;
        for (const auto& s : authorizationServers) asArr.emplace_back(s);
        j["authorization_servers"] = json_arr(std::move(asArr));

        if (!scopesSupported.empty()) {
            Json::array_t scArr;
            for (const auto& s : scopesSupported) scArr.emplace_back(s);
            j["scopes_supported"] = json_arr(std::move(scArr));
        }
        Json::array_t bmArr;
        for (const auto& b : bearerMethodsSupported) bmArr.emplace_back(b);
        j["bearer_methods_supported"] = json_arr(std::move(bmArr));

        if (!resourceName.empty()) j["resource_name"] = resourceName;
        if (!resourceDocumentation.empty()) j["resource_documentation"] = resourceDocumentation;
        return j;
    }
};

struct TokenValidationContext {
    std::string_view token;
    std::string_view expectedResource;
    const HttpRouteRequest& request;
};

struct TokenValidationResult {
    enum class Status {
        Valid,
        InvalidToken,
        InsufficientScope,
        ServiceUnavailable
    };

    Status status{Status::InvalidToken};
    Principal principal;
    std::string errorDescription;
    std::vector<std::string> requiredScopes;
};

using TokenValidatorFn = std::function<TokenValidationResult(const TokenValidationContext&)>;

/** Standard MCP OAuth 2.0 resource-server configuration (RFC 9728 + RFC 6750 Bearer). */
struct OAuthResourceConfig {
    bool enabled{false};
    bool protectDiscover{true};
    OAuthResourceMetadata metadata;
    std::vector<std::string> expectedIssuers;
    std::vector<std::string> defaultScopes;
    std::string metadataPath{"/.well-known/oauth-protected-resource"};
    TokenValidatorFn validateToken;
};

namespace mcp_detail {

inline std::optional<std::string> extract_bearer_token(std::string_view authHeader) {
    while (!authHeader.empty() && authHeader.front() == ' ') authHeader.remove_prefix(1);
    while (!authHeader.empty() && authHeader.back() == ' ') authHeader.remove_suffix(1);
    if (authHeader.size() < 8) return std::nullopt;
    std::string prefix = ascii_lower(authHeader.substr(0, 7));
    if (prefix != "bearer ") return std::nullopt;
    std::string_view tok = authHeader.substr(7);
    while (!tok.empty() && tok.front() == ' ') tok.remove_prefix(1);
    if (tok.empty()) return std::nullopt;
    for (unsigned char c : tok) {
        if (c < 0x21 || c > 0x7E) return std::nullopt;
    }
    return std::string(tok);
}

inline std::string quote_header_param(std::string_view val) {
    std::string out;
    out.reserve(val.size() + 2);
    out.push_back('"');
    for (char c : val) {
        if (c == '"' || c == '\\') out.push_back('\\');
        if (c != '\r' && c != '\n') out.push_back(c);
    }
    out.push_back('"');
    return out;
}

inline std::string build_www_authenticate(std::string_view resourceMetadataUrl,
                                          std::string_view error,
                                          std::span<const std::string> scopes,
                                          std::string_view errorDescription = "") {
    std::string h = "Bearer ";
    bool first = true;
    auto addField = [&](std::string_view k, std::string_view v) {
        if (v.empty()) return;
        if (!first) h += ", ";
        first = false;
        h += k;
        h.push_back('=');
        h += quote_header_param(v);
    };
    addField("resource_metadata", resourceMetadataUrl);
    if (!error.empty()) addField("error", error);
    if (!scopes.empty()) {
        std::vector<std::string> tmp(scopes.begin(), scopes.end());
        addField("scope", join_scopes(tmp));
    }
    if (!errorDescription.empty()) {
        addField("error_description", redact_sensitive(errorDescription));
    }
    return h;
}

/** Fail-closed token validation enforcing issuer, audience/resource, expiry, and default scopes. */
inline TokenValidationResult evaluate_bearer_auth(const OAuthResourceConfig& cfg,
                                                  std::string_view expectedResource,
                                                  const HttpRouteRequest& req) {
    TokenValidationResult fail;
    fail.status = TokenValidationResult::Status::InvalidToken;

    if (!cfg.validateToken) {
        fail.status = TokenValidationResult::Status::ServiceUnavailable;
        fail.errorDescription = "OAuth token validator not configured";
        return fail;
    }

    // Reject query-parameter tokens (only Bearer Authorization header is allowed).
    if (req.query("access_token").has_value()) {
        fail.errorDescription = "Query-string bearer tokens are forbidden; use Authorization header";
        return fail;
    }

    auto authHdr = req.header("authorization");
    if (!authHdr.has_value()) {
        fail.errorDescription = "Authentication required";
        fail.requiredScopes = cfg.defaultScopes;
        return fail;
    }
    auto bearer = extract_bearer_token(*authHdr);
    if (!bearer.has_value()) {
        fail.errorDescription = "Invalid Authorization header; expected Bearer token";
        fail.requiredScopes = cfg.defaultScopes;
        return fail;
    }

    TokenValidationResult res;
    try {
        res = cfg.validateToken(TokenValidationContext{
            .token = *bearer,
            .expectedResource = expectedResource,
            .request = req,
        });
    } catch (const std::exception& e) {
        fail.status = TokenValidationResult::Status::ServiceUnavailable;
        fail.errorDescription = "Token validator failed: " + redact_sensitive(e.what());
        return fail;
    } catch (...) {
        fail.status = TokenValidationResult::Status::ServiceUnavailable;
        fail.errorDescription = "Token validator failed";
        return fail;
    }

    if (res.status != TokenValidationResult::Status::Valid) {
        if (res.errorDescription.empty()) {
            res.errorDescription = res.status == TokenValidationResult::Status::InsufficientScope
                ? "Insufficient scope"
                : "Invalid or expired token";
        } else {
            res.errorDescription = redact_sensitive(res.errorDescription);
        }
        return res;
    }

    if (!res.principal.authenticated) {
        fail.errorDescription = "Validator returned unauthenticated principal";
        return fail;
    }

    if (res.principal.expiresAt.has_value() &&
        *res.principal.expiresAt <= std::chrono::system_clock::now()) {
        fail.errorDescription = "Token expired";
        return fail;
    }

    if (!cfg.expectedIssuers.empty()) {
        bool issuerOk = false;
        for (const auto& iss : cfg.expectedIssuers) {
            if (res.principal.issuer == iss) {
                issuerOk = true;
                break;
            }
        }
        if (!issuerOk) {
            fail.errorDescription = "Token issuer mismatch";
            return fail;
        }
    }

    if (!expectedResource.empty() &&
        res.principal.audience != expectedResource) {
        fail.errorDescription = "Token audience does not match resource";
        return fail;
    }

    // Check configured default scopes before accepting request.
    std::vector<std::string> missing;
    for (const auto& reqScope : cfg.defaultScopes) {
        if (!res.principal.hasScope(reqScope)) {
            missing.push_back(reqScope);
        }
    }
    if (!missing.empty()) {
        TokenValidationResult insuff;
        insuff.status = TokenValidationResult::Status::InsufficientScope;
        insuff.principal = std::move(res.principal);
        insuff.requiredScopes = std::move(missing);
        insuff.errorDescription = "Token lacks required scope";
        return insuff;
    }

    return res;
}

} // namespace mcp_detail

} // namespace slimr
