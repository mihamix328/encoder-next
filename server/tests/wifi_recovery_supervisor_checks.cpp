// Temporary fixture directories and child processes only. No network changes.
#include "wifi_recovery_supervisor.h"
#include "wifi_recovery_worker.h"
#include "wifi_managed_backend.h"
#include "wifi_process.h"
#include "netplan_files.h"
#include "encoder/wifi_config_draft.h"
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
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
  void pending(unsigned delay_ms, bool previous_boot = false, bool committed = false) {
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
    if (previous_boot) record.boot_id[0] = record.boot_id[0] == 'a' ? 'b' : 'a';
    record.deadline_ms = now + delay_ms;
    check(journal->begin(record, &error) && files->replace(candidate, &error), "durable pending fixture");
    if (committed) check(journal->commit(record.transaction, record.boot_id, now, &error), "committed fixture");
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
  Supervisor(Fixture& fixture, bool fail_once = false, bool hung_command = false) {
    int pipefd[2]; check(!pipe(pipefd), "supervisor stop pipe");
    const auto parent = getpid();
    pid = fork();
    if (pid < 0) { close(pipefd[0]); close(pipefd[1]); check(false, "fork supervisor"); }
    if (!pid) {
      close(pipefd[1]);
      if (prctl(PR_SET_PDEATHSIG, SIGKILL) || getppid() != parent) _exit(2);
      int attempts = 0; std::string error; WifiProcessRunner runner;
      const bool stopped = run_wifi_recovery_supervisor(fixture.state, fixture.netplan, [&] {
        ++attempts;
        if (!fixture.matches(fixture.original)) return false;
        const bool command_ok = !hung_command || runner.run({"/proc/self/exe",
          attempts == 1 ? "--command-fixture-hang" : "--command-fixture-ok"}, std::chrono::milliseconds(80)).outcome == WifiProcessOutcome::Success;
        const bool applied = command_ok && !(fail_once && attempts == 1);
        std::ofstream out(fixture.root + "/actions", std::ios::app);
        out << (applied ? 'R' : 'F'); out.close();
        return applied;
      }, [&] {
        pollfd item{pipefd[0], POLLIN, 0};
        return poll(&item, 1, 0) > 0; // Also stop if controlling parent closed pipe.
      }, &error, [&](RecoveryOutcome outcome) {
        std::ofstream out(fixture.root + "/status", std::ios::app);
        out << (outcome == RecoveryOutcome::Failed ? 'F' : outcome == RecoveryOutcome::Restored ? 'R' : outcome == RecoveryOutcome::Waiting ? 'W' : 'N');
      });
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
int main(int argc, char** argv) {
  if (argc == 2 && std::string(argv[1]) == "--command-fixture-ok") return 0;
  if (argc == 2 && std::string(argv[1]) == "--command-fixture-hang") for (;;) pause();
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
    {
      Fixture f; f.pending(90000, true); Supervisor watcher(f);
      check(eventually([&] { return f.restored(); }) && f.matches(f.original), "previous boot is recovered without waiting for obsolete deadline");
    }
    {
      Fixture f; f.pending(1); Supervisor watcher(f, false, true);
      check(eventually([&] { return f.restored(); }), "hung system action does not block future recovery attempts");
      check(wifi_recovery_supervisor_ready(f.state, f.netplan) && f.matches(f.original), "supervisor remains live after command timeout");
      watcher.shutdown();
      std::ifstream actions(f.root + "/actions"); std::string log; actions >> log;
      check(log == "FR", "timed-out action retried successfully exactly once");
      std::ifstream statuses(f.root + "/status"); std::string outcomes; statuses >> outcomes;
      check(outcomes.find('F') != std::string::npos && outcomes.find('R') != std::string::npos && outcomes.find("FF") == std::string::npos,
        "observer reports failure and recovery without duplicate status spam");
    }
    {
      Fixture f; f.pending(100, false, true); Supervisor watcher(f);
      check(eventually([&] { return wifi_recovery_supervisor_ready(f.state, f.netplan); }), "committed fixture supervisor ready");
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      check(f.matches(f.candidate) && !std::filesystem::exists(f.root + "/actions"), "confirmed operation never rolled back after deadline");
    }
    {
      Fixture f; Supervisor watcher(f);
      check(eventually([&] { return wifi_recovery_supervisor_ready(f.state, f.netplan); }), "directory identity fixture ready");
      std::filesystem::rename(f.netplan, f.root + "/previous-netplan");
      check(!mkdir(f.netplan.c_str(), 0700), "replacement directory fixture");
      check(!wifi_recovery_supervisor_ready(f.state, f.netplan), "replaced scope does not match live supervisor identity");
      check(eventually([&] { return !std::filesystem::exists(f.state + "/watchdog.sock"); }), "supervisor stops when protected directory identity changes");
      check(std::filesystem::is_empty(f.netplan), "replacement directory not modified");
    }
    {
      Fixture f; Supervisor watcher(f);
      check(eventually([&] { return wifi_recovery_supervisor_ready(f.state, f.netplan); }), "protocol fixture ready");
      const int client = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
      sockaddr_un addr{}; addr.sun_family = AF_UNIX;
      const auto path = f.state + "/watchdog.sock";
      std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);
      check(client >= 0 && !connect(client, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), "protocol fixture connection");
      const std::string oversized(1024, 'x');
      const auto sent = send(client, oversized.data(), oversized.size(), MSG_NOSIGNAL);
      pollfd item{client, POLLIN, 0}; const auto polled = poll(&item, 1, 500);
      char response[160]; const auto received = polled > 0 ? recv(client, response, sizeof(response), MSG_DONTWAIT) : -1;
      close(client);
      check(sent == static_cast<ssize_t>(oversized.size()) && received <= 0, "oversized command is not answered as readiness");
      check(wifi_recovery_supervisor_ready(f.state, f.netplan) && f.matches(f.original) && !std::filesystem::exists(f.root + "/actions"),
        "invalid protocol request cannot trigger network actions or stop supervisor");
    }
    std::cout << "Independent supervisor fixture checks passed; no real network changes\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
