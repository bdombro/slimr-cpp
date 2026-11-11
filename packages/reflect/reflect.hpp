#pragma once

#include "json.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace slimr {

/// Compile-time fixed string for non-type template parameters (NTTP)
template <size_t N>
struct FixedString {
    char data[N]{};

    constexpr FixedString() = default;
    constexpr FixedString(const char (&str)[N]) {
        std::copy_n(str, N, data);
    }
    constexpr std::string_view view() const { return {data, N > 0 ? N - 1 : 0}; }
    constexpr operator std::string_view() const { return view(); }
};

template <size_t N>
FixedString(const char (&)[N]) -> FixedString<N>;

/// Trait to detect std::optional
template <typename T>
struct is_optional : std::false_type {};

template <typename T>
struct is_optional<std::optional<T>> : std::true_type {};

template <typename T>
inline constexpr bool is_optional_v = is_optional<T>::value;

/// Metadata-annotated field wrapper providing compile-time description and schema information
template <typename T, FixedString Description = "", bool Required = !is_optional_v<T>>
struct Field {
    using value_type = T;
    static constexpr std::string_view description = Description.view();
    static constexpr bool is_required = Required;

    T value{};

    constexpr Field() = default;
    constexpr Field(const Field&) = default;
    constexpr Field(Field&&) noexcept = default;
    constexpr Field& operator=(const Field&) = default;
    constexpr Field& operator=(Field&&) noexcept = default;

    template <typename U>
        requires std::is_constructible_v<T, U> && (!std::is_same_v<std::decay_t<U>, Field>)
    constexpr Field(U&& u) : value(std::forward<U>(u)) {}

    constexpr operator T&() noexcept { return value; }
    constexpr operator const T&() const noexcept { return value; }
    constexpr T& get() noexcept { return value; }
    constexpr const T& get() const noexcept { return value; }

    constexpr T& operator*() noexcept { return value; }
    constexpr const T& operator*() const noexcept { return value; }

    constexpr T* operator->() noexcept { return &value; }
    constexpr const T* operator->() const noexcept { return &value; }

    constexpr bool has_value() const noexcept requires is_optional_v<T> {
        return value.has_value();
    }

    template <typename U>
    auto value_or(U&& default_value) const requires requires(const T& v) { v.value_or(std::forward<U>(default_value)); } {
        return value.value_or(std::forward<U>(default_value));
    }

    template <typename U>
        requires (!std::is_same_v<std::decay_t<U>, Field>)
    Field& operator=(U&& u) {
        value = std::forward<U>(u);
        return *this;
    }

    bool operator==(const Field& o) const { return value == o.value; }
    bool operator==(const T& o) const { return value == o; }
};

/// Convenient alias for a field with a description (defaults to required if non-optional)
template <typename T, FixedString D = "">
using Desc = Field<T, D>;

/// Explicitly optional field (with optional description)
template <typename T, FixedString D = "">
using OptField = Field<T, D, false>;

/// Explicitly required field (with optional description)
template <typename T, FixedString D = "">
using ReqField = Field<T, D, true>;

// Type unwrapping traits
template <typename T>
struct unwrap_type {
    using type = T;
    static constexpr std::string_view description = "";
    static constexpr bool is_field = false;
    static constexpr bool is_required = !is_optional_v<T>;
};

template <typename T, FixedString D, bool Req>
struct unwrap_type<Field<T, D, Req>> {
    using type = T;
    static constexpr std::string_view description = D.view();
    static constexpr bool is_field = true;
    static constexpr bool is_required = Req;
};

// Forward declaration
template <typename T>
Json schema();

/// Generates JSON Schema type definition for a given C++ type
template <typename T>
Json type_schema() {
    using Raw = std::decay_t<T>;
    using Unwrapped = typename unwrap_type<Raw>::type;
    using U = std::decay_t<Unwrapped>;

    Json s = Json::object_t{};
    if constexpr (is_optional_v<U>) {
        return type_schema<typename U::value_type>();
    } else if constexpr (std::is_same_v<U, std::string> || std::is_same_v<U, std::string_view> || std::is_same_v<U, const char*>) {
        s["type"] = "string";
    } else if constexpr (std::is_same_v<U, bool>) {
        s["type"] = "boolean";
    } else if constexpr (std::is_integral_v<U>) {
        s["type"] = "integer";
    } else if constexpr (std::is_floating_point_v<U>) {
        s["type"] = "number";
    } else if constexpr (requires { typename U::key_type; typename U::mapped_type; }) {
        s["type"] = "object";
        s["additionalProperties"] = type_schema<typename U::mapped_type>();
    } else if constexpr (requires { typename U::value_type; typename U::iterator; } && !std::is_same_v<U, std::string>) {
        s["type"] = "array";
        s["items"] = type_schema<typename U::value_type>();
    } else if constexpr (std::is_same_v<U, Json>) {
        s["type"] = "object";
    } else if constexpr (std::is_enum_v<U>) {
        s["type"] = "string";
    } else if constexpr (std::is_class_v<U>) {
        return schema<U>();
    } else {
        s["type"] = "string";
    }
    return s;
}

/// Generates an MCP-compliant JSON Schema object for any C++ struct
template <typename T>
Json schema() {
    using U = std::decay_t<T>;
    Json s = Json::object_t{};
    s["type"] = "object";
    s["properties"] = Json::object_t{};
    s["additionalProperties"] = false;
    Json::array_t required_arr;

    if constexpr (std::is_class_v<U> && !std::is_same_v<U, Json>) {
        constexpr size_t N = glz::member_names<U>.size();
        if constexpr (N > 0) {
            [&]<size_t... Is>(std::index_sequence<Is...>) {
                ([&] {
                    std::string_view name = glz::member_names<U>[Is];
                    using MemberType = std::decay_t<decltype(glz::get<Is>(glz::to_tie(std::declval<U&>())))>;
                    using Unwrapped = typename unwrap_type<MemberType>::type;
                    constexpr std::string_view desc = unwrap_type<MemberType>::description;
                    constexpr bool req = unwrap_type<MemberType>::is_required;

                    Json prop = type_schema<Unwrapped>();
                    if (!desc.empty()) {
                        prop["description"] = std::string(desc);
                    }
                    s["properties"][std::string(name)] = prop;
                    if (req) {
                        required_arr.push_back(std::string(name));
                    }
                }(), ...);
            }(std::make_index_sequence<N>{});
        }
    }

    s["required"] = required_arr;
    return s;
}

/// Generates compact JSON string of schema
template <typename T>
std::string schema_string() {
    return write_json(schema<T>());
}

/// Convenience alias for schema<T>()
template <typename T>
inline Json schema_for() {
    return schema<T>();
}

} // namespace slimr

// Glaze meta specialization for transparent Field serialization/deserialization
template <typename T, slimr::FixedString Desc, bool Req>
struct glz::meta<slimr::Field<T, Desc, Req>> {
    static constexpr auto value = &slimr::Field<T, Desc, Req>::value;
};
