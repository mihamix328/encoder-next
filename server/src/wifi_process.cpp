#include "wifi_process.h"
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <poll.h>
#include <unistd.h>
#include <cerrno>
namespace encoder {
bool WifiProcessRunner::reap(int* exit_code) noexcept {
  if (pid_ < 0) return true;
  siginfo_t info{};
  if (waitid(P_PID, pid_, &info, WEXITED | WNOHANG | WNOWAIT)) {
    if (errno == ECHILD) { pid_ = -1; *exit_code = -1; return true; }
    return false;
  }
  if (!info.si_pid) return false;
  // Keep the leader as an unreaped child until its group has been terminated:
  // its PID cannot be reused for an unrelated process group in this interval.
  kill(-pid_, SIGKILL);
  int status;
  if (waitpid(pid_, &status, WNOHANG) != pid_) return false;
  pid_ = -1;
  *exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  return true;
}
void WifiProcessRunner::stop() noexcept {
  int ignored = -1;
  if (reap(&ignored)) return;
  kill(-pid_, SIGKILL); kill(pid_, SIGKILL);
  for (int attempt = 0; attempt < 20 && !reap(&ignored); ++attempt)
    poll(nullptr, 0, 5);
}
WifiProcessRunner::~WifiProcessRunner() { stop(); }
bool WifiProcessRunner::busy() noexcept { int ignored = -1; return !reap(&ignored); }
WifiProcessResult WifiProcessRunner::run(const std::vector<std::string>& arguments,
    std::chrono::milliseconds timeout) noexcept {
  try {
    if (busy()) return {WifiProcessOutcome::Busy, -1};
    if (arguments.empty() || arguments.size() > 16 || arguments.front().empty() || arguments.front()[0] != '/' ||
        timeout.count() < 1 || timeout > std::chrono::seconds(30)) return {};
    size_t total = 0; std::vector<char*> argv;
    for (const auto& arg : arguments) {
      if (arg.find('\0') != std::string::npos || arg.size() > 4096 || total + arg.size() > 4096) return {};
      total += arg.size(); argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);
    char lang[] = "LANG=C", locale[] = "LC_ALL=C", path[] = "PATH=/usr/sbin:/usr/bin:/sbin:/bin";
    char* env[] = {lang, locale, path, nullptr};
    // Prepare signal state before fork. Child uses only system/async-safe calls.
    sigset_t signals; sigemptyset(&signals);
    struct sigaction default_signal{}; default_signal.sa_handler = SIG_DFL; sigemptyset(&default_signal.sa_mask);
    const auto parent = getpid();
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    const auto child = fork();
    if (child < 0) return {};
    if (!child) {
      if (setpgid(0, 0) || prctl(PR_SET_PDEATHSIG, SIGKILL) || getppid() != parent) _exit(126);
      const rlimit no_core{0, 0};
      if (setrlimit(RLIMIT_CORE, &no_core) || chdir("/")) _exit(126);
      umask(0077);
      if (sigprocmask(SIG_SETMASK, &signals, nullptr)) _exit(126);
      for (int signal = 1; signal < NSIG; ++signal)
        if (signal != SIGKILL && signal != SIGSTOP) sigaction(signal, &default_signal, nullptr);
      const int null = open("/dev/null", O_RDWR);
      if (null < 0 || dup2(null, 0) < 0 || dup2(null, 1) < 0 || dup2(null, 2) < 0) _exit(126);
#ifdef SYS_close_range
      if (syscall(SYS_close_range, 3u, ~0u, 0)) _exit(126);
#else
      _exit(126);
#endif
      execve(argv[0], argv.data(), env); _exit(127);
    }
    pid_ = child;
    // The child also sets its group before exec; this closes the timeout race.
    setpgid(child, child);
    for (;;) {
      if (std::chrono::steady_clock::now() >= deadline) {
        stop(); return {WifiProcessOutcome::TimedOut, -1};
      }
      int code = -1;
      if (reap(&code)) return {code == 0 ? WifiProcessOutcome::Success : WifiProcessOutcome::Failed, code};
      poll(nullptr, 0, 5);
    }
  } catch (...) { stop(); return {}; }
}
}
