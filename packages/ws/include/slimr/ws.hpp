#pragma once

#include "slimr/json.hpp"

#include <curl/curl.h>
#include <curl/websockets.h>

#include <chrono>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

namespace slimr {

class WebSocket {
public:
    WebSocket() = default;

    ~WebSocket() {
        close();
    }

    WebSocket(const WebSocket&) = delete;
    WebSocket& operator=(const WebSocket&) = delete;

    WebSocket(WebSocket&& o) noexcept {
        move_from(std::move(o));
    }

    WebSocket& operator=(WebSocket&& o) noexcept {
        if (this != &o) {
            close();
            move_from(std::move(o));
        }
        return *this;
    }

    bool connect(const std::string& url,
                 const std::map<std::string, std::string>& headers = {},
                 long timeoutMs = 5000) {
        close();

        curl_ = curl_easy_init();
        if (!curl_) return false;

        curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl_, CURLOPT_CONNECT_ONLY, 2L); // 2L = WebSocket mode
        curl_easy_setopt(curl_, CURLOPT_TIMEOUT_MS, timeoutMs);
        curl_easy_setopt(curl_, CURLOPT_NOSIGNAL, 1L);

        for (const auto& [k, v] : headers) {
            std::string line = k + ": " + v;
            header_list_ = curl_slist_append(header_list_, line.c_str());
        }
        if (header_list_) {
            curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, header_list_);
        }

        CURLcode rc = curl_easy_perform(curl_);
        if (rc != CURLE_OK) {
            close();
            return false;
        }

        is_connected_ = true;
        return true;
    }

    bool sendText(std::string_view msg) {
        if (!curl_ || !is_connected_) return false;
        size_t sent = 0;
        CURLcode rc = curl_ws_send(curl_, msg.data(), msg.size(), &sent, 0, CURLWS_TEXT);
        return rc == CURLE_OK;
    }

    bool sendJson(const Json& j) {
        std::string s;
        (void)glz::write_json(j, s);
        return sendText(s);
    }

    std::optional<std::string> recvText(long timeoutMs = 5000) {
        if (!curl_ || !is_connected_) return std::nullopt;

        auto start = std::chrono::steady_clock::now();
        std::string accumulator;
        char buffer[4096];

        while (true) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            if (elapsed > timeoutMs) {
                return std::nullopt; // Timeout
            }

            size_t nread = 0;
            const struct curl_ws_frame* meta = nullptr;
            CURLcode rc = curl_ws_recv(curl_, buffer, sizeof(buffer), &nread, &meta);

            if (rc == CURLE_AGAIN) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }

            if (rc != CURLE_OK) {
                is_connected_ = false;
                return std::nullopt;
            }

            if (nread > 0) {
                accumulator.append(buffer, nread);
            }

            if (meta && (meta->flags & CURLWS_CLOSE)) {
                is_connected_ = false;
                return std::nullopt;
            }

            if (meta && meta->bytesleft == 0) {
                return accumulator;
            }
        }
    }

    std::optional<Json> recvJson(long timeoutMs = 5000) {
        auto txt = recvText(timeoutMs);
        if (!txt) return std::nullopt;
        auto parsed = parse_json<Json>(*txt);
        if (!parsed) return std::nullopt;
        return *parsed;
    }

    void close() noexcept {
        if (curl_) {
            if (is_connected_) {
                size_t sent = 0;
                (void)curl_ws_send(curl_, "", 0, &sent, 0, CURLWS_CLOSE);
            }
            curl_easy_cleanup(curl_);
            curl_ = nullptr;
        }
        if (header_list_) {
            curl_slist_free_all(header_list_);
            header_list_ = nullptr;
        }
        is_connected_ = false;
    }

    bool isConnected() const noexcept { return is_connected_; }

private:
    CURL* curl_ = nullptr;
    curl_slist* header_list_ = nullptr;
    bool is_connected_ = false;

    void move_from(WebSocket&& o) noexcept {
        curl_ = o.curl_;
        o.curl_ = nullptr;
        header_list_ = o.header_list_;
        o.header_list_ = nullptr;
        is_connected_ = o.is_connected_;
        o.is_connected_ = false;
    }
};

} // namespace slimr
