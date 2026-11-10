#pragma once

#ifndef RYML_SINGLE_HDR_DEFINE_NOW
#define RYML_SINGLE_HDR_DEFINE_NOW
#endif
#ifndef RYML_WITH_LEGACY_OPERATORS
#define RYML_WITH_LEGACY_OPERATORS
#endif
#if __has_include(<ryml/ryml_all.hpp>)
#include <ryml/ryml_all.hpp>
#elif __has_include("ryml/ryml_all.hpp")
#include "ryml/ryml_all.hpp"
#elif __has_include("../../../vendor/ryml/ryml_all.hpp")
#include "../../../vendor/ryml/ryml_all.hpp"
#elif __has_include("../../../../vendor/ryml/ryml_all.hpp")
#include "../../../../vendor/ryml/ryml_all.hpp"
#elif __has_include("../../../../../vendor/ryml/ryml_all.hpp")
#include "../../../../../vendor/ryml/ryml_all.hpp"
#else
#include "ryml_all.hpp"
#endif

#if __has_include("slimr/json.hpp")
#include "slimr/json.hpp"
#else
#include "json.hpp"
#endif
#include <expected>
#include <optional>
#include <string>
#include <string_view>

namespace slimr {

namespace detail {

inline c4::csubstr tree_copy_str(ryml::NodeRef node, std::string_view s) {
    return node.tree()->copy_to_arena(c4::csubstr(s.data(), s.size()));
}

inline Json ryml_node_to_json(ryml::ConstNodeRef node) {
    if (node.invalid()) {
        return Json{nullptr};
    }
    if (node.is_stream()) {
        if (node.num_children() == 0) return Json{nullptr};
        return ryml_node_to_json(node.first_child());
    }
    if (node.is_val() || node.is_keyval()) {
        c4::csubstr v = node.val();
        std::string_view sv(v.str ? v.str : "", v.len);
        Json res;
        if (node.is_val_quoted()) {
            res.data = std::string(sv);
            return res;
        }
        if (sv == "null" || sv == "~" || sv.empty()) {
            res.data = nullptr;
            return res;
        }
        if (sv == "true") {
            res.data = true;
            return res;
        }
        if (sv == "false") {
            res.data = false;
            return res;
        }
        char* endptr = nullptr;
        std::string s(sv);
        long long iv = std::strtoll(s.c_str(), &endptr, 10);
        if (endptr && *endptr == '\0' && !s.empty()) {
            res.data = static_cast<int64_t>(iv);
            return res;
        }
        double dv = std::strtod(s.c_str(), &endptr);
        if (endptr && *endptr == '\0' && !s.empty()) {
            res.data = dv;
            return res;
        }
        res.data = std::move(s);
        return res;
    }
    if (node.is_map()) {
        Json::object_t obj;
        for (ryml::ConstNodeRef child : node.children()) {
            std::string key(child.key().str, child.key().len);
            obj.emplace(std::move(key), ryml_node_to_json(child));
        }
        Json res;
        res.data = std::move(obj);
        return res;
    }
    if (node.is_seq()) {
        Json::array_t arr;
        for (ryml::ConstNodeRef child : node.children()) {
            arr.push_back(ryml_node_to_json(child));
        }
        Json res;
        res.data = std::move(arr);
        return res;
    }
    return Json{nullptr};
}

inline void json_to_ryml_node(const Json& j, ryml::NodeRef node) {
    if (j.is_null()) {
        node |= ryml::VAL;
        node.set_val("null");
    } else if (j.is_boolean()) {
        node |= ryml::VAL;
        node.set_val(j.get<bool>() ? "true" : "false");
    } else if (j.is_number()) {
        node |= ryml::VAL;
        std::string s = to_json(j);
        node.set_val(tree_copy_str(node, s));
    } else if (j.is_string()) {
        node |= ryml::VAL;
        const auto& str = j.get<std::string>();
        node.set_val(tree_copy_str(node, str));
        if (str.empty() || str == "null" || str == "true" || str == "false" || str == "~" ||
            str.find(':') != std::string::npos || str.find('#') != std::string::npos ||
            str.find('\n') != std::string::npos) {
            node |= ryml::VALQUO;
        }
    } else if (j.is_array()) {
        node |= ryml::SEQ;
        for (const auto& elem : j.get<Json::array_t>()) {
            ryml::NodeRef child = node.append_child();
            json_to_ryml_node(elem, child);
        }
    } else if (j.is_object()) {
        node |= ryml::MAP;
        for (const auto& [k, v] : j.get<Json::object_t>()) {
            ryml::NodeRef child = node.append_child();
            child.set_key(tree_copy_str(child, k));
            json_to_ryml_node(v, child);
        }
    }
}

} // namespace detail

/// Parses a YAML string into a slimr::Json value (or typed C++ struct T via Glaze)
template <typename T = Json>
inline std::expected<T, std::string> parse_yaml(std::string_view yaml_str) {
    try {
        ryml::Tree tree = ryml::parse_in_arena(c4::csubstr(yaml_str.data(), yaml_str.size()));
        Json j = detail::ryml_node_to_json(tree.crootref());
        if constexpr (std::is_same_v<T, Json>) {
            return j;
        } else {
            return parse_json<T>(to_json(j));
        }
    } catch (const std::exception& e) {
        return std::unexpected(std::string("YAML parse error: ") + e.what());
    }
}

/// Serializes a slimr::Json value or C++ struct T to a YAML string
template <typename T>
inline std::string to_yaml(const T& val) {
    Json j;
    if constexpr (std::is_same_v<std::decay_t<T>, Json>) {
        j = val;
    } else {
        auto parsed = parse_json<Json>(to_json(val));
        if (parsed) j = std::move(*parsed);
    }
    ryml::Tree tree;
    ryml::NodeRef root = tree.rootref();
    detail::json_to_ryml_node(j, root);
    return ryml::emitrs_yaml<std::string>(tree);
}

/// Splits Markdown with `---\n...\n---\n` YAML frontmatter into `{ frontmatter, body }`
struct FrontmatterSplit {
    std::optional<Json> frontmatter;
    std::string raw_frontmatter;
    std::string body;
};

inline std::expected<FrontmatterSplit, std::string> parse_frontmatter(std::string_view markdown) {
    if (!markdown.starts_with("---\n") && !markdown.starts_with("---\r\n")) {
        return FrontmatterSplit{std::nullopt, "", std::string(markdown)};
    }
    size_t start = markdown.starts_with("---\r\n") ? 5 : 4;
    size_t close = markdown.find("\n---", start);
    if (close == std::string_view::npos) {
        return std::unexpected("Unclosed YAML frontmatter delimiter");
    }
    size_t after_close = close + 4;
    if (after_close < markdown.size() && markdown[after_close] == '\r') after_close++;
    if (after_close < markdown.size() && markdown[after_close] == '\n') after_close++;
    std::string_view yaml_part = markdown.substr(start, close - start);
    auto parsed = parse_yaml<Json>(yaml_part);
    if (!parsed) {
        return std::unexpected(parsed.error());
    }
    return FrontmatterSplit{
        std::move(*parsed),
        std::string(yaml_part),
        std::string(markdown.substr(after_close)),
    };
}

} // namespace slimr
