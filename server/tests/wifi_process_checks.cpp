// Executes only this test binary with public fixture arguments, never commands
// that configure networking. Inherited output is intentionally discarded.
#include "wifi_process.h"
#include <sys/resource.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace encoder;
using namespace std::chrono_literals;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int helper(int argc, char** argv) {
  const std::string mode = argv[1];
  if (mode == "--ok") return 0;
  if (mode == "--fail") return 7;
  if (mode == "--inspect") {
    if (argc != 4 || std::string(argv[3]) != "literal ; $(not-a-command) *") return 10;
    if (getenv("ENCODER_FIXTURE_SECRET") || !getenv("LANG") || std::string(getenv("LANG")) != "C") return 11;
    if (fcntl(std::atoi(argv[2]), F_GETFD) != -1 || errno != EBADF) return 12;
    rlimit core{}; if (getrlimit(RLIMIT_CORE, &core) || core.rlim_cur != 0 || core.rlim_max != 0) return 13;
    if (umask(0077) != 0077) return 14;
    char cwd[8]; if (!getcwd(cwd, sizeof(cwd)) || std::string(cwd) != "/") return 15;
    char byte; if (read(0, &byte, 1) != 0) return 16;
    return 0;
  }
  if (mode == "--hang") {
    signal(SIGTERM, SIG_IGN); signal(SIGINT, SIG_IGN);
    for (;;) pause();
  }
  if (mode == "--descendant" && argc == 4) {
    const auto child = fork(); if (child < 0) return 17;
    if (!child) {
      usleep(400000);
      { std::ofstream marker(argv[2]); marker << "unexpected surviving fixture child"; }
      _exit(0);
    }
    if (std::string(argv[3]) == "wait") for (;;) pause();
    return 0;
  }
  return 18;
}
struct Fixture {
  std::string root;
  Fixture() { char path[] = "/tmp/encoder-process-XXXXXX"; check(mkdtemp(path), "private process fixture"); root = path; }
  ~Fixture() { std::filesystem::remove_all(root); }
};
int main(int argc, char** argv) {
  if (argc > 1) return helper(argc, argv);
  try {
    Fixture fixture; WifiProcessRunner runner;
    check(runner.run({"/proc/self/exe", "--ok"}, 1s).outcome == WifiProcessOutcome::Success, "successful child");
    auto failed = runner.run({"/proc/self/exe", "--fail"}, 1s);
    check(failed.outcome == WifiProcessOutcome::Failed && failed.exit_code == 7, "nonzero child status preserved");
    check(runner.run({"/encoder-fixture-does-not-exist"}, 1s).outcome == WifiProcessOutcome::Failed, "missing executable is not success");
    check(runner.run({"relative-command"}, 1s).outcome == WifiProcessOutcome::Failed, "relative executable refused");
    check(runner.run({"/proc/self/exe", std::string("--ok\0extra", 10)}, 1s).outcome == WifiProcessOutcome::Failed, "embedded NUL refused");
    check(runner.run({"/proc/self/exe", "--ok"}, 0ms).outcome == WifiProcessOutcome::Failed, "zero deadline refused");
    check(runner.run({"/proc/self/exe", "--ok"}, 31s).outcome == WifiProcessOutcome::Failed, "unbounded duration refused");
    const int low = open("/dev/null", O_RDONLY); check(low >= 0, "fixture descriptor");
    const int inherited = fcntl(low, F_DUPFD, 200); close(low); check(inherited >= 200, "non-CLOEXEC descriptor fixture");
    setenv("ENCODER_FIXTURE_SECRET", "public-fixture-only", 1);
    const auto isolated = runner.run({"/proc/self/exe", "--inspect", std::to_string(inherited), "literal ; $(not-a-command) *"}, 1s);
    unsetenv("ENCODER_FIXTURE_SECRET"); close(inherited);
    check(isolated.outcome == WifiProcessOutcome::Success, "environment, descriptors, umask, stdin, cwd and literal argv isolated");
    const auto begin = std::chrono::steady_clock::now();
    check(runner.run({"/proc/self/exe", "--hang"}, 80ms).outcome == WifiProcessOutcome::TimedOut, "hung child timed out");
    check(std::chrono::steady_clock::now() - begin < 1s, "timeout cleanup bounded");
    check(!runner.busy() && runner.run({"/proc/self/exe", "--ok"}, 1s).outcome == WifiProcessOutcome::Success, "runner reusable after timeout");
    for (const auto* mode : {"exit", "wait"}) {
      const auto marker = fixture.root + "/" + mode;
      const auto outcome = runner.run({"/proc/self/exe", "--descendant", marker, mode}, 100ms).outcome;
      check(outcome == (std::string(mode) == "exit" ? WifiProcessOutcome::Success : WifiProcessOutcome::TimedOut), "fixture leader outcome");
      std::this_thread::sleep_for(500ms);
      check(!std::filesystem::exists(marker), "descendant process group stopped on success and timeout");
    }
    std::cout << "Bounded process fixture checks passed; no network commands executed\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
