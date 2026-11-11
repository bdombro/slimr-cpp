#pragma once

#include <curl/curl.h>

#include <algorithm>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>

namespace slimr {

struct HttpResponse {
    long status = 0;
    std::map<std::string, std::string> headers;
    std::string body;
};

struct HttpRequest {
    std::string method = "GET";
    std::string url;
    std::map<std::string, std::string> headers;
    std::string body;
    std::string unixSocketPath;
    long timeoutMs = 10000;
};

inline std::string url_encode(std::string_view input) {
    CURL* curl = curl_easy_init();
    if (!curl) return std::string(input);
    char* output = curl_easy_escape(curl, input.data(), static_cast<int>(input.size()));
    std::string res;
    if (output) {
        res = output;
        curl_free(output);
    }
    curl_easy_cleanup(curl);
    return res;
}

inline HttpResponse request(const HttpRequest& req) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("curl_easy_init failed");
    }

    struct SListGuard {
        curl_slist* list = nullptr;
        ~SListGuard() {
            if (list) curl_slist_free_all(list);
        }
    } header_guard;

    for (const auto& [k, v] : req.headers) {
        std::string header_line = k + ": " + v;
        header_guard.list = curl_slist_append(header_guard.list, header_line.c_str());
    }

    HttpResponse res;

    curl_easy_setopt(curl, CURLOPT_URL, req.url.c_str());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, req.timeoutMs);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    if (header_guard.list) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_guard.list);
    }

    if (!req.unixSocketPath.empty()) {
        curl_easy_setopt(curl, CURLOPT_UNIX_SOCKET_PATH, req.unixSocketPath.c_str());
    }

    if (req.method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req.body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(req.body.size()));
    } else if (req.method == "PUT") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req.body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(req.body.size()));
    } else if (req.method == "DELETE") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
        if (!req.body.empty()) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req.body.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(req.body.size()));
        }
    } else if (req.method != "GET") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, req.method.c_str());
        if (!req.body.empty()) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req.body.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(req.body.size()));
        }
    }

    // Write callback
    auto write_cb = [](char* ptr, size_t size, size_t nmemb, void* userdata) -> size_t {
        auto* str = static_cast<std::string*>(userdata);
        size_t total = size * nmemb;
        str->append(ptr, total);
        return total;
    };
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, +write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &res.body);

    // Header callback
    auto header_cb = [](char* buffer, size_t size, size_t nitems, void* userdata) -> size_t {
        auto* headers = static_cast<std::map<std::string, std::string>*>(userdata);
        size_t total = size * nitems;
        std::string_view line(buffer, total);
        auto colon = line.find(':');
        if (colon != std::string_view::npos) {
            std::string key(line.substr(0, colon));
            std::transform(key.begin(), key.end(), key.begin(), ::tolower);
            std::string_view val = line.substr(colon + 1);
            while (!val.empty() && (val.front() == ' ' || val.front() == '\t')) val.remove_prefix(1);
            while (!val.empty() && (val.back() == '\r' || val.back() == '\n' || val.back() == ' ')) val.remove_suffix(1);
            (*headers)[key] = std::string(val);
        }
        return total;
    };
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, +header_cb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &res.headers);

    CURLcode code = curl_easy_perform(curl);
    if (code != CURLE_OK) {
        std::string err = curl_easy_strerror(code);
        curl_easy_cleanup(curl);
        throw std::runtime_error("curl request failed (" + std::to_string(code) + "): " + err);
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &res.status);
    curl_easy_cleanup(curl);

    return res;
}

} // namespace slimr
