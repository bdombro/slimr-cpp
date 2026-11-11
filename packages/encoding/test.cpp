#define SLIMR_TEST_MAIN
#include "test.hpp"
#include "encoding.hpp"

using namespace slimr;

TEST_SUITE("slimr::encoding") {
    TEST_CASE("Hex encode and decode") {
        std::string raw = "Hello, World!";
        std::string hex = hex_encode(raw);
        CHECK(hex == "48656c6c6f2c20576f726c6421");

        std::string hex_up = hex_encode(raw, true);
        CHECK(hex_up == "48656C6C6F2C20576F726C6421");

        auto decoded = hex_decode(hex);
        REQUIRE(decoded.has_value());
        CHECK(*decoded == raw);

        auto decoded_up = hex_decode(hex_up);
        REQUIRE(decoded_up.has_value());
        CHECK(*decoded_up == raw);

        CHECK(!hex_decode("abc").has_value()); // odd length
        CHECK(!hex_decode("zz").has_value());  // invalid characters
    }

    TEST_CASE("RFC 4648 Base64 encode and decode") {
        CHECK(base64_encode("") == "");
        CHECK(base64_encode("f") == "Zg==");
        CHECK(base64_encode("fo") == "Zm8=");
        CHECK(base64_encode("foo") == "Zm9v");
        CHECK(base64_encode("foob") == "Zm9vYg==");
        CHECK(base64_encode("fooba") == "Zm9vYmE=");
        CHECK(base64_encode("foobar") == "Zm9vYmFy");

        auto d1 = base64_decode("Zm9vYmFy");
        REQUIRE(d1.has_value());
        CHECK(*d1 == "foobar");

        auto d2 = base64_decode("Zg==");
        REQUIRE(d2.has_value());
        CHECK(*d2 == "f");

        CHECK(!base64_decode("???").has_value());
    }

    TEST_CASE("Base64url encode and decode with and without padding") {
        std::string raw = "subjects?_d=1&foo+bar";
        std::string b64url_unpadded = base64url_encode(raw, false);
        CHECK(b64url_unpadded.find('+') == std::string::npos);
        CHECK(b64url_unpadded.find('/') == std::string::npos);
        CHECK(b64url_unpadded.find('=') == std::string::npos);

        auto d1 = base64url_decode(b64url_unpadded);
        REQUIRE(d1.has_value());
        CHECK(*d1 == raw);

        std::string b64url_padded = base64url_encode(raw, true);
        auto d2 = base64url_decode(b64url_padded);
        REQUIRE(d2.has_value());
        CHECK(*d2 == raw);
    }
}
