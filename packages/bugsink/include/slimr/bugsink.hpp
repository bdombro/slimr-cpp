#pragma once

#include "slimr/curl.hpp"
#include "slimr/json.hpp"

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <unistd.h>

namespace slimr {

namespace detail {

inline std::string generate_uuid_hex() {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dist;
    uint64_t part1 = dist(gen);
    uint64_t part2 = dist(gen);
    std::ostringstream ss;
    ss << std::hex << std::setfill('0')
       << std::setw(16) << part1
       << std::setw(16) << part2;
    return ss.str();
}

inline std::string iso_timestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
    gmtime_r(&tt, &tm_buf);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);
    return std::string(buf);
}

inline std::string get_hostname() {
    char h[256];
    if (gethostname(h, sizeof(h)) == 0) {
        h[sizeof(h) - 1] = '\0';
        return std::string(h);
    }
    return "localhost";
}

} // namespace detail

class BugsinkReporter {
public:
    struct ParsedDsn {
        std::string protocol;
        std::string publicKey;
        std::string host;
        std::string projectId;
        bool valid = false;
    };

    explicit BugsinkReporter(std::string dsn = "", std::string envVar = "BUGSINK_DSN")
        : default_dsn_(std::move(dsn)), env_var_(std::move(envVar)) {}

    static ParsedDsn parseDsn(std::string_view dsn) {
        ParsedDsn res;
        if (dsn.empty()) return res;

        // Pattern: https://<publicKey>@<host>/<projectId>
        auto proto_end = dsn.find("://");
        if (proto_end == std::string_view::npos) return res;
        res.protocol = std::string(dsn.substr(0, proto_end));

        std::string_view rest = dsn.substr(proto_end + 3);
        auto at_pos = rest.find('@');
        if (at_pos == std::string_view::npos) return res;
        res.publicKey = std::string(rest.substr(0, at_pos));

        rest = rest.substr(at_pos + 1);
        auto slash_pos = rest.find('/');
        if (slash_pos == std::string_view::npos) return res;
        res.host = std::string(rest.substr(0, slash_pos));

        res.projectId = std::string(rest.substr(slash_pos + 1));
        if (!res.publicKey.empty() && !res.host.empty() && !res.projectId.empty()) {
            res.valid = true;
        }
        return res;
    }

    std::string effectiveDsn() const {
        if (!env_var_.empty()) {
            const char* val = std::getenv(env_var_.c_str());
            if (val != nullptr) {
                return std::string(val);
            }
        }
        return default_dsn_;
    }

    std::string envelopeUrl() const {
        std::string dsn = effectiveDsn();
        auto parsed = parseDsn(dsn);
        if (!parsed.valid) return "";
        return parsed.protocol + "://" + parsed.host + "/api/" + parsed.projectId +
               "/envelope/?sentry_key=" + parsed.publicKey + "&sentry_version=7";
    }

    std::string formatEnvelope(const Json& payload, const std::string& eventId, const std::string& timestamp) const {
        std::string line1 = "{\"event_id\":\"" + eventId + "\",\"sent_at\":\"" + timestamp + "\"}";
        std::string line2 = "{\"type\":\"event\",\"content_type\":\"application/json\"}";
        std::string line3 = slimr::write_json(payload);
        return line1 + "\n" + line2 + "\n" + line3 + "\n";
    }

    bool reportError(const std::exception& err, const Json& extra = Json::object_t{}) {
        std::string url = envelopeUrl();
        if (url.empty()) return false;

        std::string eventId = detail::generate_uuid_hex();
        std::string ts = detail::iso_timestamp();

        Json payload = Json::object_t{};
        payload["event_id"] = eventId;
        payload["timestamp"] = ts;
        payload["platform"] = "native";
        payload["server_name"] = detail::get_hostname();
        payload["level"] = "error";

        Json val = Json::object_t{};
        val["type"] = "Error";
        val["value"] = std::string(err.what());

        Json::array_t vals;
        vals.push_back(val);

        Json exc = Json::object_t{};
        exc["values"] = vals;
        payload["exception"] = exc;

        if (!extra.empty()) {
            payload["extra"] = extra;
        }

        std::string body = formatEnvelope(payload, eventId, ts);

        try {
            HttpRequest req;
            req.method = "POST";
            req.url = url;
            req.headers["Content-Type"] = "text/plain;charset=UTF-8";
            req.body = std::move(body);
            req.timeoutMs = 5000;
            (void)request(req);
            return true;
        } catch (...) {
            // Reporting must never fail the caller
            return false;
        }
    }

    bool reportMessage(std::string_view message, std::string_view level = "warning", const Json& extra = Json::object_t{}) {
        std::string url = envelopeUrl();
        if (url.empty()) return false;

        std::string eventId = detail::generate_uuid_hex();
        std::string ts = detail::iso_timestamp();

        Json payload = Json::object_t{};
        payload["event_id"] = eventId;
        payload["timestamp"] = ts;
        payload["platform"] = "native";
        payload["server_name"] = detail::get_hostname();
        payload["level"] = std::string(level);
        payload["message"]["formatted"] = std::string(message);

        if (!extra.empty()) {
            payload["extra"] = extra;
        }

        std::string body = formatEnvelope(payload, eventId, ts);

        try {
            HttpRequest req;
            req.method = "POST";
            req.url = url;
            req.headers["Content-Type"] = "text/plain;charset=UTF-8";
            req.body = std::move(body);
            req.timeoutMs = 5000;
            (void)request(req);
            return true;
        } catch (...) {
            return false;
        }
    }

private:
    std::string default_dsn_;
    std::string env_var_;
};

} // namespace slimr
