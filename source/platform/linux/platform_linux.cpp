#include "platform/platform_abi.hpp"

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <spawn.h>
#include <fstream>
#include <sstream>
#include <thread>
#include <chrono>
#include <cstring>
#include <filesystem>

extern char **environ;

namespace platform {

SpawnResult spawn_process(const std::string &executable_path,
                          const std::vector<std::string> &arguments) {
    SpawnResult result;

    // Build argv array: [executable, arg1, arg2, ..., nullptr]
    std::vector<char *> argv_pointers;
    // We need mutable copies of strings for posix_spawn.
    std::vector<std::string> argv_strings;
    argv_strings.push_back(executable_path);
    for (const auto &argument : arguments) {
        argv_strings.push_back(argument);
    }

    for (auto &argument_string : argv_strings) {
        argv_pointers.push_back(argument_string.data());
    }
    argv_pointers.push_back(nullptr);

    pid_t child_pid = 0;
    int spawn_status = posix_spawn(&child_pid, executable_path.c_str(),
                                    nullptr, nullptr,
                                    argv_pointers.data(), environ);

    if (spawn_status != 0) {
        result.success = false;
        result.error_message = "posix_spawn failed: " + std::string(strerror(spawn_status));
        return result;
    }

    result.success = true;
    result.process_id = static_cast<int>(child_pid);
    return result;
}

bool read_file_contents(const std::string &file_path, std::string &output_contents) {
    std::ifstream file_stream(file_path);
    if (!file_stream.is_open()) {
        return false;
    }
    std::ostringstream string_stream;
    string_stream << file_stream.rdbuf();
    output_contents = string_stream.str();
    return true;
}

bool wait_for_file(const std::string &file_path, int timeout_milliseconds) {
    int elapsed_milliseconds = 0;
    int poll_interval_milliseconds = 100;
    int maximum_iterations = (timeout_milliseconds / poll_interval_milliseconds) + 1;
    // Safety limit: twice the expected max iterations to prevent infinite loop.
    int safety_limit = maximum_iterations * 2;

    for (int iteration = 0; iteration < safety_limit; iteration++) {
        if (std::filesystem::exists(file_path)) {
            // Verify the file is non-empty (Chrome may create it before writing).
            std::string contents;
            if (read_file_contents(file_path, contents) && !contents.empty()) {
                return true;
            }
        }

        if (elapsed_milliseconds >= timeout_milliseconds) {
            return false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(poll_interval_milliseconds));
        elapsed_milliseconds += poll_interval_milliseconds;
    }

    return false;
}

bool kill_process(int process_id) {
    if (process_id <= 0) {
        return false;
    }
    int kill_result = kill(static_cast<pid_t>(process_id), SIGTERM);
    return (kill_result == 0);
}

} // namespace platform
