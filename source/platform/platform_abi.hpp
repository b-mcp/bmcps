#ifndef BMCPS_PLATFORM_ABI_HPP
#define BMCPS_PLATFORM_ABI_HPP

// Platform abstraction interface.
// Each OS-specific implementation lives under platform/<os>/ and provides
// definitions for the functions declared here.

#include <string>
#include <vector>
#include <functional>

namespace platform {

// Result of spawning a child process.
struct SpawnResult {
    bool success = false;
    int process_id = -1;
    std::string error_message;
};

// Spawn a child process with the given executable path and arguments.
// The process runs detached (not waited on immediately).
SpawnResult spawn_process(const std::string &executable_path,
                          const std::vector<std::string> &arguments);

// Read the entire contents of a text file into a string.
// Returns true on success, false on failure (file not found, permission, etc.).
bool read_file_contents(const std::string &file_path, std::string &output_contents);

// Wait (poll) until a file exists, up to timeout_milliseconds.
// Returns true if the file appeared, false if timed out.
bool wait_for_file(const std::string &file_path, int timeout_milliseconds);

// Kill a process by its process ID.
bool kill_process(int process_id);

} // namespace platform

#endif // BMCPS_PLATFORM_ABI_HPP
