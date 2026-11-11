#define SLIMR_TEST_MAIN
#include "test.hpp"
#include "text.hpp"

using namespace slimr;

TEST_SUITE("slimr::text") {
    TEST_CASE("clean_text removes zero-width and invisible characters") {
        // Zero-width space U+200B (\xe2\x80\x8b), BOM U+FEFF (\xef\xbb\xbf)
        std::string raw = "Hello\xe2\x80\x8b World\xef\xbb\xbf!";
        std::string cleaned = clean_text(raw);
        CHECK(cleaned == "Hello World!");

        // BiDi override U+202E (\xe2\x80\xae)
        std::string bidi = "Admin\xe2\x80\xae User";
        CHECK(clean_text(bidi) == "Admin User");

        // Preserves normal UTF-8 like é and standard whitespace
        std::string normal = "Café \t\nline";
        CHECK(clean_text(normal) == "Café \t\nline");
    }

    TEST_CASE("html_to_text basic conversion") {
        std::string html = "<h1>Title</h1><p>First paragraph with <a href=\"https://example.com\">link</a>.</p><p>Second paragraph.</p>";
        std::string text = html_to_text(html);
        CHECK(text.find("Title") != std::string::npos);
        CHECK(text.find("First paragraph with link (https://example.com).") != std::string::npos);
        CHECK(text.find("Second paragraph.") != std::string::npos);
    }

    TEST_CASE("html_to_text drops script, style, comments, and hidden elements") {
        std::string html = R"(
<div>
  <!-- Comment -->
  <style>body { color: red; }</style>
  <script>alert('xss');</script>
  <p>Visible content</p>
  <div style="display:none">Hidden secret text</div>
  <span hidden>Also hidden</span>
</div>
)";
        std::string text = html_to_text(html);
        CHECK(text.find("Visible content") != std::string::npos);
        CHECK(text.find("alert") == std::string::npos);
        CHECK(text.find("color: red") == std::string::npos);
        CHECK(text.find("Hidden secret text") == std::string::npos);
        CHECK(text.find("Also hidden") == std::string::npos);
    }

    TEST_CASE("html_to_text decodes entities") {
        std::string html = "<p>Tom &amp; Jerry &quot;Show&quot; &lt;1940&gt;</p>";
        std::string text = html_to_text(html);
        CHECK(text == "Tom & Jerry \"Show\" <1940>");
    }
}
