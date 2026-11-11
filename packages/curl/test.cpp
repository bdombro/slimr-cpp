#define SLIMR_TEST_MAIN
#include "test.hpp"
#include "curl.hpp"

using namespace slimr;

TEST_SUITE("slimr::curl") {
    TEST_CASE("url_encode") {
        CHECK(url_encode("hello world") == "hello%20world");
        CHECK(url_encode("a+b=c&d/e") == "a%2Bb%3Dc%26d%2Fe");
        CHECK(url_encode("simple_test_123") == "simple_test_123");
    }

    TEST_CASE("request fails gracefully on unreachable target") {
        HttpRequest req;
        req.url = "http://127.0.0.1:1/test";
        req.timeoutMs = 500;

        CHECK_THROWS_AS(request(req), std::runtime_error);
    }
}
