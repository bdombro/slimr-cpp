# slimr::text (`packages/text`)

Fast, clean C++26 text utility library supporting robust UTF-8 sanitization (cleaning invisible zero-width and prompt injection characters), HTML-to-plain-text conversion, HTML entity decoding, and text truncation/wrapping.

Borrowed directly from the design of [`@slimr/text`](https://www.npmjs.com/package/@slimr/text).

## Why `slimr::text`?

- **UTF-8 Sanitization (`clean_text`):** Strips hidden zero-width spaces, BiDi control overrides, BOMs, and malicious Unicode tag plane injection characters while preserving standard whitespace (`\n`, `\r`, `\t`).
- **Lightweight HTML Extractor (`html_to_text`):** Strips HTML tags, decodes common entities, formats paragraphs cleanly, and removes unwanted `<script>`, `<style>`, and `<nav>` blocks.
- **Entity Decoding:** Decodes named (`&amp;`, `&quot;`) and numeric/hex HTML entities (`&#39;`, `&#x27;`).
- **Truncation & Wrapping:** Clean text truncation and word wrapping utilities.

---

## Quick Start

```cpp
#include <slimr/text.hpp>
#include <iostream>

using namespace slimr;

int main() {
    // 1. Sanitize potentially malicious or dirty UTF-8 text (e.g. prompt injection guards)
    std::string dirty = "Hello\u200B World\U000E0001! \n\tTest";
    std::string clean = clean_text(dirty);
    std::cout << "Cleaned: " << clean << "\n";

    // 2. Convert HTML to readable plain text
    std::string html = R"(<html><body><h1>Hello World</h1><p>Welcome to <b>slimr::text</b> &amp; friends.</p></body></html>)";
    std::string text = html_to_text(html);
    std::cout << "Plain Text:\n" << text << "\n";

    // 3. Truncate text with ellipsis
    std::string truncated = truncate("Long sentence that needs shortening", 15);
    std::cout << "Truncated: " << truncated << "\n";

    return 0;
}
```

---

## API Cheat Sheet

| Function | Signature | Description |
| --- | --- | --- |
| `clean_text` | `inline std::string clean_text(std::string_view input)` | Strips ASCII control chars (preserving `\n`, `\r`, `\t`), bidi overrides, zero-width spaces, BOM, and Unicode tag plane injection. |
| `html_to_text` | `inline std::string html_to_text(std::string_view html, bool strip_links = false)` | Strips HTML tags, removes `<script>`/`<style>`, decodes entities, and formats plain text. |
| `truncate` | `inline std::string truncate(std::string_view s, size_t max_len, std::string_view ellipsis = "...")` | Truncates string to `max_len` characters, appending `ellipsis` if truncated. |
| `word_wrap` | `inline std::string word_wrap(std::string_view s, size_t max_width = 80)` | Word-wraps text to specified maximum line width. |
