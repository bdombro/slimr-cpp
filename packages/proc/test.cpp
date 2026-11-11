#define SLIMR_TEST_MAIN
#include "test.hpp"
#include "proc.hpp"

using namespace slimr::proc;

TEST_SUITE("slimr::proc") {
    TEST_CASE("Basic command execution and stdout capture") {
        auto res = exec({"echo", "hello proc"});
        CHECK(res.exit_code == 0);
        CHECK(res.stdout_str == "hello proc\n");
        CHECK(!res.timed_out);
    }

    TEST_CASE("Stdin data piping") {
        ProcOptions opt;
        opt.stdin_data = "stdin payload line\n";
        auto res = exec({"cat"}, opt);
        CHECK(res.exit_code == 0);
        CHECK(res.stdout_str == "stdin payload line\n");
        CHECK(!res.timed_out);
    }

    TEST_CASE("Environment overrides") {
        ProcOptions opt;
        opt.env["SLIMR_TEST_VAR"] = "proc_val_42";
        auto res = exec({"sh", "-c", "echo $SLIMR_TEST_VAR"}, opt);
        CHECK(res.exit_code == 0);
        CHECK(res.stdout_str == "proc_val_42\n");
    }

    TEST_CASE("Working directory override") {
        ProcOptions opt;
        opt.cwd = "/tmp";
        auto res = exec({"pwd"}, opt);
        CHECK(res.exit_code == 0);
        // On macOS /tmp is often /private/tmp symlink
        CHECK(res.stdout_str.find("tmp") != std::string::npos);
    }

    TEST_CASE("Timeout handling") {
        ProcOptions opt;
        opt.timeout_ms = 150;
        auto res = exec({"sleep", "2"}, opt);
        CHECK(res.timed_out == true);
    }

    TEST_CASE("Non-zero exit code") {
        auto res = exec({"sh", "-c", "exit 7"});
        CHECK(res.exit_code == 7);
    }
}
