#pragma once
#include <chrono>
#include <string>
#include <vector>
namespace encoder {
enum class WifiProcessOutcome { Success, Failed, TimedOut, Busy };
struct WifiProcessResult {
  WifiProcessOutcome outcome = WifiProcessOutcome::Failed;
  int exit_code = -1;
};
// Linux-only internal runner, NOT an RPC/command API. All executable paths and
// arguments must be fixed/trusted local adapter data; never passwords or remote
// commands. Absolute executable path, clean environment, no shell, no output
// capture, isolated process group, inherited descriptors closed before exec.
// Requires Linux close_range support; missing support fails closed (exit 126).
// Single owner/thread; no other SIGCHLD handler may reap this object's children.
// Keep one runner for the service lifetime. A timed-out child stuck in kernel I/O
// may not die immediately: retain it and refuse new work until it is reaped.
// Destructor does only bounded best-effort cleanup, not an unbounded waitpid.
class WifiProcessRunner {
 public:
  WifiProcessRunner() = default;
  ~WifiProcessRunner();
  WifiProcessRunner(const WifiProcessRunner&) = delete;
  WifiProcessRunner& operator=(const WifiProcessRunner&) = delete;
  WifiProcessResult run(const std::vector<std::string>& arguments, std::chrono::milliseconds timeout) noexcept;
  bool busy() noexcept;
 private:
  bool reap(int* exit_code) noexcept;
  void stop() noexcept;
  int pid_ = -1;
};
}
