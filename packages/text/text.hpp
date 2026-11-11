#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <regex>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace slimr {

// ============================================================================
// UTF-8 Sanitization (clean_text)
// ============================================================================

/// Strips ASCII control characters (preserving \n, \r, \t), bidi overrides,
/// invisible zero-width characters, BOM, and Unicode tag plane injection characters.
inline std::string clean_text(std::string_view input) {
    std::string out;
    out.reserve(input.size());

    size_t i = 0;
    while (i < input.size()) {
        unsigned char c = static_cast<unsigned char>(input[i]);

        // 1. ASCII range (0x00..0x7F)
        if (c <= 0x7F) {
            // Preserve \t (0x09), \n (0x0A), \r (0x0D)
            if (c == 0x09 || c == 0x0A || c == 0x0D || (c >= 0x20 && c != 0x7F)) {
                out.push_back(static_cast<char>(c));
            }
            i++;
            continue;
        }

        // 2. Multi-byte UTF-8 sequence decoding
        uint32_t cp = 0;
        size_t len = 0;
        if ((c & 0xE0) == 0xC0) {
            len = 2;
            cp = c & 0x1F;
        } else if ((c & 0xF0) == 0xE0) {
            len = 3;
            cp = c & 0x0F;
        } else if ((c & 0xF8) == 0xF0) {
            len = 4;
            cp = c & 0x07;
        } else {
            // Invalid UTF-8 lead byte, skip
            i++;
            continue;
        }

        if (i + len > input.size()) {
            break; // Truncated sequence
        }

        bool valid = true;
        for (size_t k = 1; k < len; ++k) {
            unsigned char cont = static_cast<unsigned char>(input[i + k]);
            if ((cont & 0xC0) != 0x80) {
                valid = false;
                break;
            }
            cp = (cp << 6) | (cont & 0x3F);
        }

        if (!valid) {
            i++;
            continue;
        }

        // Filter out unwanted / malicious Unicode codepoints:
        // - Soft hyphen: U+00AD
        // - Combining grapheme joiner: U+034F
        // - Zero-width spaces & formatting: U+200B..U+200F
        // - BiDi control characters: U+202A..U+202E
        // - Invisible formatting & word joiner: U+2060..U+206F
        // - Byte Order Mark / ZWNBSP: U+FEFF
        // - Unicode tag plane (used for invisible prompt injection): U+E0000..U+E007F
        bool drop = (cp == 0x00AD || cp == 0x034F ||
                    (cp >= 0x200B && cp <= 0x200F) ||
                    (cp >= 0x202A && cp <= 0x202E) ||
                    (cp >= 0x2060 && cp <= 0x206F) ||
                    cp == 0xFEFF ||
                    (cp >= 0xE0000 && cp <= 0xE007F));

        if (!drop) {
            out.append(input.substr(i, len));
        }
        i += len;
    }
    return out;
}

// ============================================================================
// Lightweight HTML-to-Readable-Text (html_to_text)
// ============================================================================

namespace detail {

inline std::string to_lower_str(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
}

inline std::string trim_str(std::string_view s) {
    size_t start = 0;
    while (start < s.size() && (s[start] == ' ' || s[start] == '\t' || s[start] == '\r' || s[start] == '\n')) {
        start++;
    }
    size_t end = s.size();
    while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r' || s[end - 1] == '\n')) {
        end--;
    }
    return std::string(s.substr(start, end - start));
}

inline std::string codepoint_to_utf8(uint32_t cp) {
    std::string out;
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0x10FFFF) {
        out.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    return out;
}

inline std::string decode_html_entities(const std::string& text) {
    static const std::unordered_map<std::string, std::string> ENTITIES = {
        {"amp", "&"},
        {"apos", "'"},
        {"copy", "\xc2\xa9"},
        {"euro", "\xe2\x82\xac"},
        {"gt", ">"},
        {"hellip", "\xe2\x80\xa6"},
        {"ldquo", "\xe2\x80\x9c"},
        {"lsquo", "\xe2\x80\x98"},
        {"lt", "<"},
        {"mdash", "\xe2\x80\x94"},
        {"nbsp", " "},
        {"ndash", "\xe2\x80\x93"},
        {"pound", "\xc2\xa3"},
        {"quot", "\""},
        {"rdquo", "\xe2\x80\x9d"},
        {"rsquo", "\xe2\x80\x99"},
    };

    static const std::regex entity_re(R"RAW(&(#x?[0-9a-fA-F]+|\w+);)RAW");
    std::string out;
    auto begin = std::sregex_iterator(text.begin(), text.end(), entity_re);
    auto end = std::sregex_iterator();
    size_t last_pos = 0;

    for (auto it = begin; it != end; ++it) {
        std::smatch m = *it;
        out.append(text, last_pos, m.position() - last_pos);
        std::string raw_match = m.str();
        std::string ent = m[1].str();

        if (ent.front() == '#') {
            bool hex = (ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X'));
            size_t start_idx = hex ? 2 : 1;
            try {
                uint32_t code = std::stoul(ent.substr(start_idx), nullptr, hex ? 16 : 10);
                if (code <= 0x10ffff) {
                    out.append(codepoint_to_utf8(code));
                } else {
                    out.append(raw_match);
                }
            } catch (...) {
                out.append(raw_match);
            }
        } else {
            auto fit = ENTITIES.find(ent);
            if (fit != ENTITIES.end()) {
                out.append(fit->second);
            } else {
                out.append(raw_match);
            }
        }
        last_pos = m.position() + m.length();
    }
    out.append(text, last_pos, text.size() - last_pos);
    return out;
}

inline bool is_hidden_element(const std::string& attrs) {
    static const std::regex hidden_attr_re(R"RAW(\bhidden\b)RAW", std::regex::icase);
    if (std::regex_search(attrs, hidden_attr_re)) return true;

    static const std::regex style_re(R"RAW(style\s*=\s*["']([^"']+)["'])RAW", std::regex::icase);
    std::smatch m;
    if (std::regex_search(attrs, m, style_re)) {
        std::string style = to_lower_str(m[1].str());
        if (style.find("display:none") != std::string::npos ||
            style.find("display: none") != std::string::npos ||
            style.find("visibility:hidden") != std::string::npos ||
            style.find("visibility: hidden") != std::string::npos ||
            style.find("opacity:0") != std::string::npos ||
            style.find("font-size:0") != std::string::npos) {
            return true;
        }
    }
    return false;
}

inline std::string drop_hidden_elements(const std::string& html) {
    static const std::regex open_tag_re(R"RAW(<([a-z][a-z0-9]*)\b((?:"[^"]*"|'[^']*'|[^>"'])*)>)RAW", std::regex::icase);
    static const std::set<std::string> void_tags = {
        "area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta", "param", "source", "track", "wbr"
    };

    std::string out;
    size_t pos = 0;

    auto it = std::sregex_iterator(html.begin(), html.end(), open_tag_re);
    auto end = std::sregex_iterator();

    for (; it != end; ++it) {
        std::smatch m = *it;
        size_t m_index = m.position();
        if (m_index < pos) continue;

        std::string name = to_lower_str(m[1].str());
        std::string attrs = m[2].str();

        if (void_tags.contains(name) || !is_hidden_element(attrs)) {
            continue;
        }

        // Find matching close tag
        out.append(html, pos, m_index - pos);
        std::string close_tag = "</" + name + ">";
        size_t close_pos = html.find(close_tag, m_index + m.length());
        if (close_pos != std::string::npos) {
            pos = close_pos + close_tag.size();
        } else {
            pos = html.size();
        }
    }
    out.append(html, pos, html.size() - pos);
    return out;
}

} // namespace detail

/// Converts HTML into clean readable text:
/// - Strips comments, script, style, head
/// - Strips hidden/display:none elements
/// - Preserves block linebreaks (<p>, <div>, <br>, <li>, <tr>, <h1>..<h6>)
/// - Converts hyperlinks to `Anchor Text (URL)`
/// - Decodes HTML entities and normalizes whitespace
/// - Sanitizes control/bidi/invisible Unicode characters via `clean_text`
inline std::string html_to_text(std::string_view html_sv) {
    std::string s(html_sv);

    // 1. Drop comments <!--...-->
    static const std::regex comment_re(R"RAW(<!--[\s\S]*?-->)RAW");
    s = std::regex_replace(s, comment_re, "");

    // 2. Drop head|style|script|title
    static const std::regex head_style_re(R"RAW(<(head|style|script|title)\b[\s\S]*?<\/\1>)RAW", std::regex::icase);
    s = std::regex_replace(s, head_style_re, "");

    // 3. Drop hidden elements
    s = detail::drop_hidden_elements(s);

    // 4. Links: <a href="...">text</a> -> text (url)
    static const std::regex link_re(R"RAW(<a\b[^>]*href=["']([^"']+)["'][^>]*>([\s\S]*?)<\/a>)RAW", std::regex::icase);
    static const std::regex strip_tags_re(R"RAW(<[^>]+>)RAW");

    std::string link_processed;
    auto l_begin = std::sregex_iterator(s.begin(), s.end(), link_re);
    auto l_end = std::sregex_iterator();
    size_t last_pos = 0;

    for (auto it = l_begin; it != l_end; ++it) {
        std::smatch m = *it;
        link_processed.append(s, last_pos, m.position() - last_pos);
        std::string href = m[1].str();
        std::string inner = m[2].str();
        std::string text = detail::trim_str(std::regex_replace(inner, strip_tags_re, ""));

        if (text.empty() || href.starts_with("mailto:") || href.size() > 300) {
            link_processed.append(text);
        } else {
            link_processed.append(text + " (" + href + ")");
        }
        last_pos = m.position() + m.length();
    }
    link_processed.append(s, last_pos, s.size() - last_pos);
    s = std::move(link_processed);

    // 5. Block tags to newline
    static const std::regex block_re(R"RAW(<(br|\/p|\/div|\/tr|\/h[1-6]|\/li|\/table)\b[^>]*>)RAW", std::regex::icase);
    s = std::regex_replace(s, block_re, "\n");

    // 6. </t[dh]> to space
    static const std::regex td_re(R"RAW(<\/t[dh]>)RAW", std::regex::icase);
    s = std::regex_replace(s, td_re, " ");

    // 7. Strip remaining HTML tags
    s = std::regex_replace(s, strip_tags_re, "");

    // 8. Decode HTML entities
    s = detail::decode_html_entities(s);

    // 9. Clean Unicode control/bidi/invisible characters
    s = clean_text(s);

    // 10. Strip \r
    std::string no_r;
    no_r.reserve(s.size());
    for (char c : s) if (c != '\r') no_r.push_back(c);
    s = std::move(no_r);

    // 11. Normalize spaces and tabs
    static const std::regex space_re(R"RAW([ \t]+)RAW");
    s = std::regex_replace(s, space_re, " ");

    // 12. Normalize newlines
    static const std::regex nl_space_re(R"RAW( *\n *)RAW");
    s = std::regex_replace(s, nl_space_re, "\n");

    static const std::regex multi_nl_re(R"RAW(\n{3,})RAW");
    s = std::regex_replace(s, multi_nl_re, "\n\n");

    return detail::trim_str(s);
}

} // namespace slimr
