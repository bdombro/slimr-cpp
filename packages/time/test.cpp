#define SLIMR_TEST_MAIN
#include "test.hpp"
#include "time.hpp"

using namespace slimr;

TEST_SUITE("slimr::time") {
    TEST_CASE("format_iso8601_utc and format_iso8601_utc_ms") {
        int64_t sec = 1768469400; // 2026-01-15T09:30:00Z
        CHECK(format_iso8601_utc(sec) == "2026-01-15T09:30:00Z");

        int64_t ms = sec * 1000 + 123;
        CHECK(format_iso8601_utc_ms(ms) == "2026-01-15T09:30:00.123Z");
    }

    TEST_CASE("parse_iso8601_utc") {
        auto parsed = parse_iso8601_utc("2026-01-15T09:30:00Z");
        REQUIRE(parsed.has_value());
        CHECK(*parsed == 1768469400);

        // With offset +01:00 -> 09:30:00+01:00 is 08:30:00 UTC
        auto parsed_off = parse_iso8601_utc("2026-01-15T09:30:00+01:00");
        REQUIRE(parsed_off.has_value());
        CHECK(*parsed_off == 1768469400 - 3600);

        CHECK(!parse_iso8601_utc("short").has_value());
    }

    TEST_CASE("civil_day_bounds Europe/Dublin winter and summer") {
        // Winter: UTC+0
        auto winter = civil_day_bounds("2026-01-15", "Europe/Dublin");
        REQUIRE(winter.has_value());
        CHECK(format_iso8601_utc(winter->start_sec) == "2026-01-15T00:00:00Z");
        CHECK(format_iso8601_utc(winter->end_sec) == "2026-01-16T00:00:00Z");
        CHECK(winter->start_ms == winter->start_sec * 1000);
        CHECK(winter->end_ms == winter->end_sec * 1000);

        // Summer: Irish Standard Time is UTC+1 (midnight is 23:00:00Z of previous UTC day)
        auto summer = civil_day_bounds("2026-07-01", "Europe/Dublin");
        REQUIRE(summer.has_value());
        CHECK(format_iso8601_utc(summer->start_sec) == "2026-06-30T23:00:00Z");
        CHECK(format_iso8601_utc(summer->end_sec) == "2026-07-01T23:00:00Z");

        // Error checking
        CHECK(!civil_day_bounds("invalid").has_value());
    }
}
