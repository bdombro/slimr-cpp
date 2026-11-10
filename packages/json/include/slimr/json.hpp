#pragma once

#include <glaze/glaze.hpp>
#include <expected>
#include <string>
#include <string_view>

namespace slimr {

/// Dynamic JSON generic tree representation preserving 64-bit integer precision
using Json = glz::generic_i64;
using json_t = glz::generic_i64;

/// Safe JSON object constructor preventing initializer_list ambiguities
inline Json json_obj(Json::object_t obj = {}) { Json j; j.data = std::move(obj); return j; }

/// Safe JSON array constructor preventing initializer_list ambiguities
inline Json json_arr(Json::array_t arr = {}) { Json j; j.data = std::move(arr); return j; }

/// Serializes any supported C++ struct or value to JSON string
template <typename T>
inline auto write_json(const T& val, std::string& buffer) {
    return glz::write_json(val, buffer);
}

/// Serializes any supported C++ struct or value to a new JSON string
template <typename T>
inline std::string write_json(const T& val) {
    std::string out;
    (void)glz::write_json(val, out);
    return out;
}

/// Convenience alias for serializing to JSON string
template <typename T>
inline std::string to_json(const T& val) {
    std::string out;
    (void)glz::write_json(val, out);
    return out;
}

/// Serializes value to formatted indented JSON string
template <typename T>
inline std::string write_json_pretty(const T& val) {
    std::string out;
    (void)glz::write<glz::opts{.prettify = true}>(val, out);
    return out;
}

/// Deserializes JSON string into a target C++ struct or value
template <typename T>
inline auto read_json(T& val, std::string_view buffer) {
    return glz::read_json(val, buffer);
}

/// Parses JSON string into a newly constructed value of type T (defaulting to Json)
template <typename T = Json>
inline std::expected<T, std::string> parse_json(std::string_view buffer) {
    T val{};
    auto ec = glz::read_json(val, buffer);
    if (ec) {
        return std::unexpected(glz::format_error(ec, buffer));
    }
    return val;
}

} // namespace slimr
