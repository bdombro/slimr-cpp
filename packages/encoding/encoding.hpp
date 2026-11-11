#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace slimr {

// --- Hex ---

inline std::string hex_encode(std::string_view in, bool uppercase = false) {
    static constexpr char hex_lower[] = "0123456789abcdef";
    static constexpr char hex_upper[] = "0123456789ABCDEF";
    const char* table = uppercase ? hex_upper : hex_lower;
    std::string out;
    out.reserve(in.size() * 2);
    for (unsigned char c : in) {
        out.push_back(table[(c >> 4) & 0x0F]);
        out.push_back(table[c & 0x0F]);
    }
    return out;
}

inline std::expected<std::string, std::string> hex_decode(std::string_view in) {
    if (in.size() % 2 != 0) {
        return std::unexpected("Odd number of hexadecimal digits");
    }
    auto hex_val = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::string out;
    out.reserve(in.size() / 2);
    for (size_t i = 0; i < in.size(); i += 2) {
        int hi = hex_val(in[i]);
        int lo = hex_val(in[i + 1]);
        if (hi < 0 || lo < 0) {
            return std::unexpected("Invalid hexadecimal character");
        }
        out.push_back(static_cast<char>((hi << 4) | lo));
    }
    return out;
}

// --- RFC 4648 Base64 & Base64url ---

inline std::string base64_encode(std::string_view in) {
    static constexpr char chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((in.size() + 2) / 3) * 4);
    size_t i = 0;
    while (i < in.size()) {
        size_t remaining = in.size() - i;
        uint32_t octet_a = static_cast<uint8_t>(in[i++]);
        uint32_t octet_b = remaining > 1 ? static_cast<uint8_t>(in[i++]) : 0;
        uint32_t octet_c = remaining > 2 ? static_cast<uint8_t>(in[i++]) : 0;
        uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;

        out.push_back(chars[(triple >> 18) & 0x3F]);
        out.push_back(chars[(triple >> 12) & 0x3F]);
        if (remaining == 1) {
            out.push_back('=');
            out.push_back('=');
        } else if (remaining == 2) {
            out.push_back(chars[(triple >> 6) & 0x3F]);
            out.push_back('=');
        } else {
            out.push_back(chars[(triple >> 6) & 0x3F]);
            out.push_back(chars[triple & 0x3F]);
        }
    }
    return out;
}

inline std::string base64url_encode(std::string_view in, bool pad = false) {
    static constexpr char chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    out.reserve(((in.size() + 2) / 3) * 4);
    size_t i = 0;
    while (i < in.size()) {
        size_t remaining = in.size() - i;
        uint32_t octet_a = static_cast<uint8_t>(in[i++]);
        uint32_t octet_b = remaining > 1 ? static_cast<uint8_t>(in[i++]) : 0;
        uint32_t octet_c = remaining > 2 ? static_cast<uint8_t>(in[i++]) : 0;
        uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;

        out.push_back(chars[(triple >> 18) & 0x3F]);
        out.push_back(chars[(triple >> 12) & 0x3F]);
        if (remaining > 1) {
            out.push_back(chars[(triple >> 6) & 0x3F]);
        } else if (pad) {
            out.push_back('=');
        }
        if (remaining > 2) {
            out.push_back(chars[triple & 0x3F]);
        } else if (pad) {
            out.push_back('=');
        }
    }
    return out;
}

namespace detail {
inline int b64_char_to_val(unsigned char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
}
} // namespace detail

inline std::expected<std::string, std::string> base64_decode(std::string_view in) {
    std::string out;
    out.reserve(in.size() * 3 / 4);
    uint32_t val = 0;
    int valb = -8;
    for (char c : in) {
        if (c == '=') break;
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        int d = detail::b64_char_to_val(static_cast<unsigned char>(c));
        if (d == -1) {
            return std::unexpected(std::string("Invalid base64 character: '") + c + "'");
        }
        val = (val << 6) | d;
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<char>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

inline std::expected<std::string, std::string> base64url_decode(std::string_view in) {
    return base64_decode(in);
}

} // namespace slimr
