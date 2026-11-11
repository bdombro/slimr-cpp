#pragma once

#ifdef SLIMR_TEST_MAIN
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#endif

#include "doctest/doctest.h"

#include <filesystem>
#include <random>
#include <string>
#include <string_view>

namespace slimr::testing {

inline std::filesystem::path temp_dir(std::string_view prefix = "slimr_test_") {
    auto base = std::filesystem::temp_directory_path();
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dist;
    auto p = base / (std::string(prefix) + std::to_string(dist(gen)));
    std::filesystem::create_directories(p);
    return p;
}

struct TempDirGuard {
    std::filesystem::path path;
    explicit TempDirGuard(std::string_view prefix = "slimr_test_") : path(temp_dir(prefix)) {}
    ~TempDirGuard() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

} // namespace slimr::testing
