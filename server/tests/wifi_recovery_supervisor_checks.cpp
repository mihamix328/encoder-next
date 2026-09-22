// Temporary fixture directories and child processes only. No network changes.
#include "wifi_recovery_supervisor.h"
#include "wifi_recovery_worker.h"
#include "wifi_managed_backend.h"
#include "netplan_files.h"
#include "encoder/wifi_config_draft.h"
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <poll.h>
#include <signal.h>
#include <unistd.h>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace encoder;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct Fixture {
  std::string root, state, netplan, error;
  SecureBuffer original, candidate;
  Fixture() {
    char temporary[] = "/tmp/encoder-supervisor-XXXXXX";
    check(mkdtemp(temporary), "create private fixture"); root = temporary;
    state = root + "/state"; netplan = root + "/netplan";
    check(!mkdir(state.c_str(), 0700) && !mkdir(netplan.c_str(), 0700), "create fixture scope");
    auto first = WifiProfile::make("Original fixture", std::string(64, 'a'), &error);
    auto next = WifiProfile::make("Candidate fixture", std::string(64, 'b'), &error);
    check(first && next, "fixture profiles");
    original = std::move(make_wifi_config_draft(*first, &error)->netplan_yaml);
    candidate = std::move(make_wifi_config_draft(*next, &error)->netplan_yaml);
    auto files = NetplanFiles::open(netplan, &error);
    check(files && files->replace(original, &error), "original fixture file");
  }
  ~Fixture() { std::filesystem::remove_all(root); }
  bool matches(const SecureBuffer& expected) {
    auto files = NetplanFiles::open(netplan, &error); SecureBuffer actual; bool exists;
    return files && files->backup(&exists, &actual, &error) && exists && actual.size() == expected.size() &&
      !std::memcmp(actual.data(), expected.data(), actual.size());
  }
  void pending(unsigned delay_ms) {
    std::unique_ptr<WifiJournal> journal;
    for (int attempt = 0; attempt < 100 && !journal; ++attempt) {
      journal = WifiJournal::open(state, &error);
      if (!journal) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    check(bool(journal), "lock fixture journal");
    auto files = NetplanFiles::open(netplan, &error);
    RecoveryRecord record; record.transaction = std::string(32, 'a');
    check(files && files->backup(&record.previous_exists, &record.previous, &error), "fixture backup");
    uint64_t now; check(recovery_clock(&record.boot_id, &now, &error), "fixture clock");
    record.deadline_ms = now + delay_ms;
    check(journal->begin(record, &error) && files->replace(candidate, &error), "durable pending fixture");
  }
  bool restored() {
    auto journal = WifiJournal::open(state, &error); RecoveryRecord record; bool exists;
    return journal && journal->load(&record, &exists, &error) && exists && record.phase == RecoveryPhase::Restored;
  }
};
template<class F> bool eventually(F predicate) {
  for (int i = 0; i < 100; ++i) {
    if (predicate()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return false;
}
struct Supervisor {
  pid_t pid = -1; int stop = -1;
  Supervisor(Fixture& fixture, bool fail_once = false) {
    int pipefd[2]; check(!pipe(pipefd), "supervisor stop pipe");
    const auto parent = getpid();
    pid = fork();
    if (pid < 0) { close(pipefd[0]); close(pipefd[1]); check(false, "fork supervisor"); }
    if (!pid) {
      close(pipefd[1]);
      if (prctl(PR_SET_PDEATHSIG, SIGKILL) || getppid() != parent) _exit(2);
      int attempts = 0; std::string error;
      const bool stopped = run_wifi_recovery_supervisor(fixture.state, fixture.netplan, [&] {
        ++attempts;
        if (!fixture.matches(fixture.original)) return false;
        std::ofstream out(fixture.root + "/actions", std::ios::app);
        out << (fail_once && attempts == 1 ? 'F' : 'R'); out.close();
        return !(fail_once && attempts == 1);
      }, [&] {
        pollfd item{pipefd[0], POLLIN, 0};
        return poll(&item, 1, 0) > 0; // Also stop if controlling parent closed pipe.
      }, &error);
      close(pipefd[0]); _exit(stopped ? 0 : 1);
    }
    close(pipefd[0]); stop = pipefd[1];
  }
  void shutdown(bool force = false) {
    if (pid < 0) return;
    close(stop); stop = -1;
    if (force) kill(pid, SIGKILL);
    int status = 0;
    for (int i = 0; i < 100; ++i) {
      if (waitpid(pid, &status, WNOHANG) == pid) { pid = -1; return; }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    kill(pid, SIGKILL); waitpid(pid, &status, 0); pid = -1;
  }
  ~Supervisor() { shutdown(); }
};
struct ManagedPlatform final : WifiManagedPlatform {
  Fixture& fixture;
  explicit ManagedPlatform(Fixture& f) : fixture(f) {}
  bool ethernet_ready() noexcept override { return true; }
  bool preflight(const WifiProfile&) noexcept override { return true; }
  bool watchdog_ready(const std::string& state) noexcept override {
    return wifi_recovery_supervisor_ready(state, fixture.netplan);
  }
  bool apply_target(const WifiProfile&) noexcept override { return fixture.matches(fixture.candidate); }
  WifiLink probe_target(const WifiProfile&) noexcept override { return WifiLink::Ready; }
  bool restore_network() noexcept override { return fixture.matches(fixture.original); }
};
int main() {
  try {
    {
      Fixture f;
      check(!wifi_recovery_supervisor_ready(f.state, f.netplan), "no supervisor is not ready");
      Supervisor process(f, true);
      check(eventually([&] { return wifi_recovery_supervisor_ready(f.state, f.netplan); }), "live supervisor is ready");
      std::string error;
      check(!run_wifi_recovery_supervisor(f.state, f.netplan, [] { return true; }, [] { return true; }, &error),
        "second supervisor cannot take over the same journal");
      const auto other = f.root + "/other"; check(!mkdir(other.c_str(), 0700), "different fixture scope");
      check(!wifi_recovery_supervisor_ready(f.state, other), "wrong Netplan directory identity rejected");
      {
        auto journal = WifiJournal::open(f.state, &error);
        check(bool(journal), "transaction may hold journal lock briefly");
        check(wifi_recovery_supervisor_ready(f.state, f.netplan), "readiness does not deadlock behind transaction lock");
      }
      f.pending(700);
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      check(f.matches(f.candidate), "supervisor must not restore early");
      check(eventually([&] { return f.restored(); }), "failed reconfigure retried without losing pending journal");
      check(f.matches(f.original), "supervisor restored exact original");
      std::ifstream actions(f.root + "/actions"); std::string log; actions >> log;
      check(log == "FR", "reconfigure failed once then succeeded exactly once");
      process.shutdown();
      check(!wifi_recovery_supervisor_ready(f.state, f.netplan) && !std::filesystem::exists(f.state + "/watchdog.sock"),
        "graceful shutdown removes readiness socket");
    }
    {
      Fixture f;
      Supervisor process(f);
      check(eventually([&] { return wifi_recovery_supervisor_ready(f.state, f.netplan); }), "first supervisor ready");
      process.shutdown(true);
      check(std::filesystem::exists(f.state + "/watchdog.sock") && !wifi_recovery_supervisor_ready(f.state, f.netplan),
        "stale socket after process death cannot prove liveness");
      f.pending(1);
      Supervisor restarted(f);
      check(eventually([&] { return f.restored(); }), "restarted supervisor resumes durable recovery");
      check(wifi_recovery_supervisor_ready(f.state, f.netplan) && f.matches(f.original), "restart binds new live socket");
    }
    for (int mode = 0; mode < 3; ++mode) {
      Fixture f; const auto entry = f.state + "/watchdog.sock";
      if (mode == 0) { std::ofstream out(entry); out << "not a socket"; }
      if (mode == 1) check(!symlink((f.root + "/untouched").c_str(), entry.c_str()), "unsafe socket symlink fixture");
      if (mode == 2) chmod(f.state.c_str(), 0755);
      check(!wifi_recovery_supervisor_ready(f.state, f.netplan), "unsafe readiness location rejected");
      std::string error;
      check(!run_wifi_recovery_supervisor(f.state, f.netplan, [] { return true; }, [] { return true; }, &error), "unsafe supervisor location rejected");
      if (mode < 2) { struct stat s{}; check(!lstat(entry.c_str(), &s), "foreign entry is not deleted"); }
    }
    {
      Fixture f; Supervisor watcher(f);
      check(eventually([&] { return wifi_recovery_supervisor_ready(f.state, f.netplan); }), "integrated supervisor ready");
      auto target = read_wifi_config_draft(std::string_view(reinterpret_cast<const char*>(f.candidate.data()), f.candidate.size()), &f.error);
      check(bool(target), "integrated target fixture");
      ManagedPlatform platform(f); WifiManagedBackend backend(f.state, f.netplan, platform); WifiChange change(backend);
      check(change.start(*target), "managed transaction uses actual supervisor liveness");
      watcher.shutdown(true);
      change.tick();
      check(change.state() == WifiChangeState::RolledBack && f.matches(f.original), "live owner rolls back on supervisor death");
    }
    {
      Fixture f; Supervisor watcher(f);
      check(eventually([&] { return wifi_recovery_supervisor_ready(f.state, f.netplan); }), "crash integration supervisor ready");
      int completion[2]; check(!pipe(completion), "owner completion pipe");
      const auto parent = getpid(); const pid_t owner = fork(); check(owner >= 0, "fork transaction owner");
      if (!owner) {
        close(completion[0]); close(watcher.stop);
        if (prctl(PR_SET_PDEATHSIG, SIGKILL) || getppid() != parent) _exit(2);
        auto target = read_wifi_config_draft(std::string_view(reinterpret_cast<const char*>(f.candidate.data()), f.candidate.size()), &f.error);
        ManagedPlatform platform(f); WifiManagedBackend backend(f.state, f.netplan, platform);
        const char result = target && backend.prepare(*target, std::chrono::seconds(1)) && backend.activate() ? '1' : '0';
        if (write(completion[1], &result, 1) != 1) _exit(3);
        // _exit deliberately bypasses destructors; independent watcher must act.
        _exit(0);
      }
      close(completion[1]); char result = 0; const auto received = read(completion[0], &result, 1); close(completion[0]);
      int status = 0; check(waitpid(owner, &status, 0) == owner, "reap transaction owner");
      check(received == 1 && result == '1' && WIFEXITED(status) && WEXITSTATUS(status) == 0, "owner exited with pending durable operation");
      check(eventually([&] { return f.restored(); }) && f.matches(f.original), "running independent watcher automatically repairs owner death");
    }
    std::cout << "Independent supervisor fixture checks passed; no real network changes\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
