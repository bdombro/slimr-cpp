# slimr::time (`packages/time`)

Fast, robust C++26 timezone-aware date and time library supporting ISO-8601 formatting/parsing, epoch seconds/milliseconds conversions, and civil day bounds calculation across arbitrary IANA time zones.

Borrowed directly from the design of [`@slimr/time`](https://www.npmjs.com/package/@slimr/time).

## Why `slimr::time`?

- **Timezone-aware Civil Bounds:** Computes precise UTC start and end bounds (`civil_day_bounds`) for any calendar date (`YYYY-MM-DD`) in any IANA time zone (e.g. `Europe/Dublin`), correctly accounting for daylight saving transitions.
- **ISO-8601 Precision:** Format and parse UTC timestamps in seconds and milliseconds (`format_iso8601_utc`, `format_iso8601_utc_ms`, `parse_iso8601_utc`).
- **Thread-safe Timezone Handling:** Uses thread-safe mutex and environment guards (`TZ`) for correct timezone calculations.
- **`std::expected` Error Handling:** Clean result types for robust validation of date strings and offsets.

---

## Quick Start

```cpp
#include <slimr/time.hpp>
#include <iostream>

using namespace slimr;

int main() {
    // Current epoch timestamp in seconds
    int64_t now_sec = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    // Format ISO-8601 UTC
    std::string iso = format_iso8601_utc(now_sec);
    std::string iso_ms = format_iso8601_utc_ms(now_sec * 1000);

    std::cout << "ISO UTC: " << iso << "\n";

    // Parse ISO string
    auto parsed = parse_iso8601_utc("2026-11-10T16:00:00Z");
    if (parsed) {
        std::cout << "Parsed epoch sec: " << *parsed << "\n";
    }

    // Compute civil day bounds in a specific timezone
    auto bounds = civil_day_bounds("2026-11-10", "Europe/Dublin");
    if (bounds) {
        std::cout << "Day start UTC sec: " << bounds->start_sec << "\n";
        std::cout << "Day end UTC sec: " << bounds->end_sec << "\n";
    }

    return 0;
}
```

---

## API Cheat Sheet

| Function / Type | Signature | Description |
| --- | --- | --- |
| `CivilDayBounds` | `struct CivilDayBounds { int64_t start_sec = 0; int64_t end_sec = 0; int64_t start_ms = 0; int64_t end_ms = 0; };` | Container for civil day boundary epoch timestamps in seconds and milliseconds. |
| `format_iso8601_utc` | `inline std::string format_iso8601_utc(int64_t epoch_sec)` | Formats UTC epoch seconds to `YYYY-MM-DDTHH:MM:SSZ`. |
| `format_iso8601_utc_ms` | `inline std::string format_iso8601_utc_ms(int64_t epoch_ms)` | Formats UTC epoch milliseconds to `YYYY-MM-DDTHH:MM:SS.mmmZ`. |
| `parse_iso8601_utc` | `inline std::expected<int64_t, std::string> parse_iso8601_utc(std::string_view iso_str)` | Parses ISO-8601 date-time string into UTC epoch seconds within `std::expected`. |
| `civil_day_bounds` | `inline std::expected<CivilDayBounds, std::string> civil_day_bounds(std::string_view yyyy_mm_dd, std::string_view tz = "Europe/Dublin")` | Computes UTC start and end bounds for civil day `YYYY-MM-DD` in given timezone. |
