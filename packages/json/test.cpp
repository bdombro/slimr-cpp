#define SLIMR_TEST_MAIN
#include "test.hpp"
#include "json.hpp"
#include <vector>

using namespace slimr;

TEST_SUITE("slimr::json") {
    TEST_CASE("readers respect non-terminated input bounds") {
        std::vector<char> truncated{'{'};
        std::string_view incomplete(truncated.data(), truncated.size());
        CHECK_FALSE(parse_json(incomplete).has_value());
        Json value;
        CHECK(slimr::read_json(value, incomplete));

        std::vector<char> complete{'[', '1', ']'};
        auto parsed = parse_json(std::string_view(complete.data(), complete.size()));
        REQUIRE(parsed.has_value());
        REQUIRE(parsed->is_array());
        CHECK(parsed->get<Json::array_t>().size() == 1);
    }

    TEST_CASE("Basic JSON reading and writing") {
        Json j = Json::object_t{};
        j["hello"] = "world";
        j["num"] = 123;
        std::string s = slimr::write_json(j);
        CHECK(s.find("\"hello\":\"world\"") != std::string::npos);

        auto parsed = parse_json<Json>(s);
        CHECK(parsed.has_value());
        CHECK((*parsed)["hello"].get<std::string>() == "world");
        CHECK((*parsed)["num"].get<int64_t>() == 123);
    }

    TEST_CASE("json_obj and json_arr helpers") {
        auto obj = json_obj({{"key", "val"}});
        CHECK(obj.is_object());
        CHECK(obj["key"].get<std::string>() == "val");

        auto arr = json_arr({1, 2, 3});
        CHECK(arr.is_array());
        CHECK(arr.get<Json::array_t>().size() == 3);
    }

    TEST_CASE("64-bit integer precision") {
        int64_t large_val = 9007199254740993LL; // Beyond 53-bit IEEE double precision
        Json j = Json::object_t{};
        j["large"] = large_val;
        std::string s = to_json(j);

        auto parsed = parse_json<Json>(s);
        CHECK(parsed.has_value());
        CHECK((*parsed)["large"].get<int64_t>() == large_val);
    }

    TEST_CASE("Pretty printing") {
        Json j = Json::object_t{};
        j["key"] = "value";
        std::string pretty = write_json_pretty(j);
        CHECK(pretty.find("\n") != std::string::npos);
        CHECK(pretty.find("  \"key\": \"value\"") != std::string::npos);
    }

    TEST_CASE("Invalid JSON error handling") {
        auto parsed = parse_json<Json>("{ invalid json ");
        CHECK(!parsed.has_value());
        CHECK(!parsed.error().empty());
    }
}
