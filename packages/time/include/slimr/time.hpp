#pragma once

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <expected>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace slimr {

struct CivilDayBounds {
    int64_t start_sec = 0;
    int64_t end_sec = 0;
    int64_t start_ms = 0;
    int64_t end_ms = 0;
};

namespace detail {

inline std::mutex g_time_tz_mutex;

struct ThreadSafeTzGuard {
    std::string old_tz;
    bool had_old = false;

    explicit ThreadSafeTzGuard(std::string_view tz) {
        g_time_tz_mutex.lock();
        const char* cur = std::getenv("TZ");
        if (cur) {
            old_tz = cur;
            had_old = true;
        }
        std::string tz_str(tz);
        setenv("TZ", tz_str.c_str(), 1);
        tzset();
    }

    ~ThreadSafeTzGuard() {
        if (had_old) {
            setenv("TZ", old_tz.c_str(), 1);
        } else {
            unsetenv("TZ");
        }
        tzset();
        g_time_tz_mutex.unlock();
    }
};

inline int64_t get_local_gmtoff_sec(time_t utc_sec, std::string_view tz) {
    ThreadSafeTzGuard guard(tz);
    struct tm tm_out{};
    localtime_r(&utc_sec, &tm_out);
    return static_cast<int64_t>(tm_out.tm_gmtoff);
}

} // namespace detail

/// Formats a UTC epoch timestamp (in seconds) to ISO-8601 string `YYYY-MM-DDTHH:MM:SSZ`
inline std::string format_iso8601_utc(int64_t epoch_sec) {
    time_t sec = static_cast<time_t>(epoch_sec);
    struct tm gm{};
    gmtime_r(&sec, &gm);
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
             gm.tm_year + 1900, gm.tm_mon + 1, gm.tm_mday,
             gm.tm_hour, gm.tm_min, gm.tm_sec);
    return std::string(buf);
}

/// Formats a UTC epoch timestamp (in milliseconds) to ISO-8601 string `YYYY-MM-DDTHH:MM:SS.mmmZ`
inline std::string format_iso8601_utc_ms(int64_t epoch_ms) {
    int64_t sec_val = epoch_ms / 1000;
    int64_t rem_ms = epoch_ms % 1000;
    if (rem_ms < 0) {
        rem_ms += 1000;
        sec_val -= 1;
    }
    time_t sec = static_cast<time_t>(sec_val);
    struct tm gm{};
    gmtime_r(&sec, &gm);
    char buf[36];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
             gm.tm_year + 1900, gm.tm_mon + 1, gm.tm_mday,
             gm.tm_hour, gm.tm_min, gm.tm_sec, static_cast<int>(rem_ms));
    return std::string(buf);
}

/// Parses an ISO-8601 string into UTC epoch seconds
inline std::expected<int64_t, std::string> parse_iso8601_utc(std::string_view iso_str) {
    if (iso_str.size() < 19) {
        return std::unexpected("ISO-8601 string too short: " + std::string(iso_str));
    }

    int y = 0, m = 0, d = 0, h = 0, min = 0, sec = 0;
    try {
        y = std::stoi(std::string(iso_str.substr(0, 4)));
        m = std::stoi(std::string(iso_str.substr(5, 2)));
        d = std::stoi(std::string(iso_str.substr(8, 2)));
        h = std::stoi(std::string(iso_str.substr(11, 2)));
        min = std::stoi(std::string(iso_str.substr(14, 2)));
        sec = std::stoi(std::string(iso_str.substr(17, 2)));
    } catch (...) {
        return std::unexpected("Failed to parse ISO-8601 digits: " + std::string(iso_str));
    }

    struct tm tm_utc{};
    tm_utc.tm_year = y - 1900;
    tm_utc.tm_mon = m - 1;
    tm_utc.tm_mday = d;
    tm_utc.tm_hour = h;
    tm_utc.tm_min = min;
    tm_utc.tm_sec = sec;
    time_t utc_sec = timegm(&tm_utc);
    if (utc_sec == -1 && (y != 1969 || m != 12 || d != 31)) {
        return std::unexpected("Invalid date components in ISO-8601: " + std::string(iso_str));
    }

    int64_t total_sec = static_cast<int64_t>(utc_sec);

    // Check timezone offset if present (e.g. +01:00 or -05:00)
    size_t tz_pos = iso_str.find_first_of("Zz+-", 19);
    if (tz_pos != std::string_view::npos && iso_str[tz_pos] != 'Z' && iso_str[tz_pos] != 'z') {
        char sign = iso_str[tz_pos];
        if (tz_pos + 5 < iso_str.size()) {
            try {
                int off_h = std::stoi(std::string(iso_str.substr(tz_pos + 1, 2)));
                int off_m = std::stoi(std::string(iso_str.substr(tz_pos + 4, 2)));
                int64_t offset_sec = off_h * 3600 + off_m * 60;
                if (sign == '+') total_sec -= offset_sec;
                else if (sign == '-') total_sec += offset_sec;
            } catch (...) {}
        }
    }

    return total_sec;
}

/// Computes UTC start and end bounds (epoch seconds & milliseconds) for a given civil calendar day (YYYY-MM-DD)
inline std::expected<CivilDayBounds, std::string> civil_day_bounds(std::string_view yyyy_mm_dd, std::string_view tz = "Europe/Dublin") {
    if (yyyy_mm_dd.size() != 10 || yyyy_mm_dd[4] != '-' || yyyy_mm_dd[7] != '-') {
        return std::unexpected("Invalid date format, expected YYYY-MM-DD: " + std::string(yyyy_mm_dd));
    }

    int y = 0, m = 0, d = 0;
    try {
        y = std::stoi(std::string(yyyy_mm_dd.substr(0, 4)));
        m = std::stoi(std::string(yyyy_mm_dd.substr(5, 2)));
        d = std::stoi(std::string(yyyy_mm_dd.substr(8, 2)));
    } catch (...) {
        return std::unexpected("Non-numeric date components: " + std::string(yyyy_mm_dd));
    }

    auto get_midnight_utc = [](int year, int mon, int day, std::string_view timezone) -> int64_t {
        struct tm tm_utc{};
        tm_utc.tm_year = year - 1900;
        tm_utc.tm_mon = mon - 1;
        tm_utc.tm_mday = day;
        tm_utc.tm_hour = 0;
        tm_utc.tm_min = 0;
        tm_utc.tm_sec = 0;
        time_t guess_sec = timegm(&tm_utc);

        int64_t off1 = detail::get_local_gmtoff_sec(guess_sec, timezone);
        int64_t adjusted1 = static_cast<int64_t>(guess_sec) - off1;
        int64_t off2 = detail::get_local_gmtoff_sec(static_cast<time_t>(adjusted1), timezone);
        return static_cast<int64_t>(guess_sec) - off2;
    };

    int64_t start_sec = get_midnight_utc(y, m, d, tz);

    // Compute next day
    struct tm tm_next{};
    tm_next.tm_year = y - 1900;
    tm_next.tm_mon = m - 1;
    tm_next.tm_mday = d + 1;
    tm_next.tm_hour = 0;
    tm_next.tm_min = 0;
    tm_next.tm_sec = 0;
    time_t next_day_sec = timegm(&tm_next);
    struct tm tm_calc{};
    gmtime_r(&next_day_sec, &tm_calc);

    int64_t end_sec = get_midnight_utc(tm_calc.tm_year + 1900, tm_calc.tm_mon + 1, tm_calc.tm_mday, tz);

    return CivilDayBounds{
        .start_sec = start_sec,
        .end_sec = end_sec,
        .start_ms = start_sec * 1000LL,
        .end_ms = end_sec * 1000LL,
    };
}

} // namespace slimr
