#define SLIMR_TEST_MAIN
#include "test.hpp"
#include <fstream>

TEST_SUITE("slimr::testing") {
    TEST_CASE("temp_dir creates distinct valid paths") {
        auto p1 = slimr::testing::temp_dir("p1_");
        auto p2 = slimr::testing::temp_dir("p2_");
        CHECK(p1 != p2);
        CHECK(std::filesystem::exists(p1));
        CHECK(std::filesystem::exists(p2));
        std::error_code ec;
        std::filesystem::remove_all(p1, ec);
        std::filesystem::remove_all(p2, ec);
    }

    TEST_CASE("TempDirGuard creates and cleans up directory") {
        std::filesystem::path guard_path;
        {
            slimr::testing::TempDirGuard guard("guard_test_");
            guard_path = guard.path;
            CHECK(std::filesystem::exists(guard_path));
            CHECK(std::filesystem::is_directory(guard_path));

            // Create a file inside guard_path
            std::ofstream f(guard_path / "dummy.txt");
            f << "hello world";
            f.close();
            CHECK(std::filesystem::exists(guard_path / "dummy.txt"));
        }
        // After leaving scope, TempDirGuard cleans it up
        CHECK(!std::filesystem::exists(guard_path));
    }
}
