#pragma once

#include <chrono>
#include <cstring>
#include <map>
#include <optional>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

extern char** environ;

namespace slimr::proc {

struct ProcOptions {
    std::string cwd;
    std::map<std::string, std::string> env;
    std::string stdin_data;
    int timeout_ms = 0; // 0 or <= 0 means no timeout
};

struct ProcResult {
    int exit_code = -1;
    std::string stdout_str;
    std::string stderr_str;
    bool timed_out = false;
};

inline ProcResult exec(const std::vector<std::string>& args, const ProcOptions& options = {}) {
    if (args.empty()) {
        return ProcResult{.exit_code = -1, .stderr_str = "No command specified"};
    }

    int stdin_pipe[2] = {-1, -1};
    int stdout_pipe[2] = {-1, -1};
    int stderr_pipe[2] = {-1, -1};

    if (pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
        if (stdout_pipe[0] >= 0) { close(stdout_pipe[0]); close(stdout_pipe[1]); }
        if (stderr_pipe[0] >= 0) { close(stderr_pipe[0]); close(stderr_pipe[1]); }
        return ProcResult{.exit_code = -1, .stderr_str = std::string("pipe failed: ") + strerror(errno)};
    }

    bool has_stdin = !options.stdin_data.empty();
    if (has_stdin) {
        if (pipe(stdin_pipe) != 0) {
            close(stdout_pipe[0]); close(stdout_pipe[1]);
            close(stderr_pipe[0]); close(stderr_pipe[1]);
            return ProcResult{.exit_code = -1, .stderr_str = std::string("stdin pipe failed: ") + strerror(errno)};
        }
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);

    if (has_stdin) {
        posix_spawn_file_actions_addclose(&actions, stdin_pipe[1]);
        posix_spawn_file_actions_adddup2(&actions, stdin_pipe[0], STDIN_FILENO);
        posix_spawn_file_actions_addclose(&actions, stdin_pipe[0]);
    }

    posix_spawn_file_actions_addclose(&actions, stdout_pipe[0]);
    posix_spawn_file_actions_adddup2(&actions, stdout_pipe[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&actions, stdout_pipe[1]);

    posix_spawn_file_actions_addclose(&actions, stderr_pipe[0]);
    posix_spawn_file_actions_adddup2(&actions, stderr_pipe[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, stderr_pipe[1]);

#if defined(__APPLE__) || defined(_GNU_SOURCE)
    if (!options.cwd.empty()) {
        posix_spawn_file_actions_addchdir(&actions, options.cwd.c_str());
    }
#endif

    std::vector<char*> argv_ptrs;
    for (const auto& a : args) {
        argv_ptrs.push_back(const_cast<char*>(a.c_str()));
    }
    argv_ptrs.push_back(nullptr);

    // Environment setup: merge options.env with current environ
    std::vector<std::string> env_strings;
    std::vector<char*> env_ptrs;
    if (!options.env.empty()) {
        std::map<std::string, std::string> merged_env;
        for (char** ep = environ; ep && *ep; ++ep) {
            std::string entry = *ep;
            auto eq = entry.find('=');
            if (eq != std::string::npos) {
                merged_env[entry.substr(0, eq)] = entry.substr(eq + 1);
            }
        }
        for (const auto& [k, v] : options.env) {
            merged_env[k] = v;
        }
        for (const auto& [k, v] : merged_env) {
            env_strings.push_back(k + "=" + v);
        }
        for (const auto& s : env_strings) {
            env_ptrs.push_back(const_cast<char*>(s.c_str()));
        }
        env_ptrs.push_back(nullptr);
    }

    char* const* actual_env = options.env.empty() ? environ : env_ptrs.data();

    pid_t pid;
    int spawn_ret = posix_spawnp(&pid, args[0].c_str(), &actions, nullptr, argv_ptrs.data(), actual_env);
    posix_spawn_file_actions_destroy(&actions);

    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    if (has_stdin) {
        close(stdin_pipe[0]);
        ssize_t written = 0;
        size_t to_write = options.stdin_data.size();
        const char* p = options.stdin_data.data();
        while (to_write > 0) {
            ssize_t n = write(stdin_pipe[1], p + written, to_write);
            if (n <= 0) break;
            written += n;
            to_write -= n;
        }
        close(stdin_pipe[1]);
    }

    if (spawn_ret != 0) {
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        return ProcResult{.exit_code = -1, .stderr_str = "Could not spawn " + args[0] + ": " + strerror(spawn_ret)};
    }

    std::string stdout_str;
    std::string stderr_str;
    struct pollfd fds[2];
    fds[0].fd = stdout_pipe[0];
    fds[0].events = POLLIN;
    fds[1].fd = stderr_pipe[0];
    fds[1].events = POLLIN;

    char buf[16384];
    int open_fds = 2;
    auto start_time = std::chrono::steady_clock::now();
    bool timed_out = false;

    while (open_fds > 0) {
        int poll_timeout = -1;
        if (options.timeout_ms > 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time).count();
            if (elapsed >= options.timeout_ms) {
                timed_out = true;
                break;
            }
            poll_timeout = static_cast<int>(options.timeout_ms - elapsed);
        }

        int poll_res = poll(fds, 2, poll_timeout);
        if (poll_res < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (poll_res == 0 && options.timeout_ms > 0) {
            timed_out = true;
            break;
        }

        for (int i = 0; i < 2; ++i) {
            if (fds[i].fd >= 0 && (fds[i].revents & (POLLIN | POLLHUP | POLLERR))) {
                ssize_t n = read(fds[i].fd, buf, sizeof(buf));
                if (n > 0) {
                    if (i == 0) stdout_str.append(buf, n);
                    else stderr_str.append(buf, n);
                } else {
                    close(fds[i].fd);
                    fds[i].fd = -1;
                    open_fds--;
                }
            }
        }
    }

    if (fds[0].fd >= 0) close(fds[0].fd);
    if (fds[1].fd >= 0) close(fds[1].fd);

    if (timed_out) {
        kill(pid, SIGKILL);
        int status = 0;
        waitpid(pid, &status, 0);
        return ProcResult{
            .exit_code = -1,
            .stdout_str = std::move(stdout_str),
            .stderr_str = std::move(stderr_str),
            .timed_out = true
        };
    }

    int status = 0;
    waitpid(pid, &status, 0);

    int exit_code = -1;
    if (WIFEXITED(status)) {
        exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        exit_code = 128 + WTERMSIG(status);
    }

    return ProcResult{
        .exit_code = exit_code,
        .stdout_str = std::move(stdout_str),
        .stderr_str = std::move(stderr_str),
        .timed_out = false
    };
}

} // namespace slimr::proc
