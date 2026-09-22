// All files are private temporary fixtures. No real network or services touched.
#include "wifi_managed_backend.h"
#include "netplan_files.h"
#include "encoder/wifi_config_draft.h"
#include <sys/stat.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <thread>
using namespace encoder;
void check(bool value, const char* message) {
  if (!value) { std::cerr << message << '\n'; std::exit(1); }
}
struct Fixture {
  std::string root, state, netplan, error;
  WifiProfile original = std::move(*WifiProfile::make("Original", std::string(64, 'a'), nullptr));
  WifiProfile target = std::move(*WifiProfile::make("Candidate", std::string(64, 'b'), nullptr));
  Fixture() {
    char path[] = "/tmp/encoder-managed-backend-XXXXXX";
    check(mkdtemp(path), "create private fixture"); root = path;
    state = root + "/state"; netplan = root + "/netplan";
    check(!mkdir(state.c_str(), 0700) && !mkdir(netplan.c_str(), 0700), "fixture directories");
    auto files = NetplanFiles::open(netplan, &error);
    auto draft = make_wifi_config_draft(original, &error);
    check(files && draft && files->replace(draft->netplan_yaml, &error), "original fixture");
  }
  ~Fixture() { std::filesystem::remove_all(root); }
  bool matches(const WifiProfile& profile) {
    auto files = NetplanFiles::open(netplan, &error);
    SecureBuffer data; bool exists;
    auto draft = make_wifi_config_draft(profile, &error);
    return files && draft && files->backup(&exists, &data, &error) && exists &&
      data.size() == draft->netplan_yaml.size() && !std::memcmp(data.data(), draft->netplan_yaml.data(), data.size());
  }
  RecoveryRecord record(bool expected = true) {
    auto journal = WifiJournal::open(state, &error); RecoveryRecord value; bool exists;
    check(journal && journal->load(&value, &exists, &error) && exists == expected, "read fixture journal");
    return value;
  }
};
struct Platform final : WifiManagedPlatform {
  Fixture& fixture;
  bool ethernet = true, scope = true, watchdog = true, apply = true, restore = true;
  int applications = 0, restorations = 0, watchdog_checks = 0, fail_watchdog_at = 0;
  int apply_delay_ms = 0;
  WifiLink link = WifiLink::Ready;
  explicit Platform(Fixture& f) : fixture(f) {}
  bool ethernet_ready() noexcept override { return ethernet; }
  bool preflight(const WifiProfile&) noexcept override { return scope; }
  bool watchdog_ready(const std::string& directory) noexcept override {
    ++watchdog_checks;
    return directory == fixture.state && watchdog && (!fail_watchdog_at || watchdog_checks < fail_watchdog_at);
  }
  bool apply_target(const WifiProfile& target) noexcept override {
    ++applications; check(fixture.matches(target), "candidate persisted before network apply");
    if (apply_delay_ms) std::this_thread::sleep_for(std::chrono::milliseconds(apply_delay_ms));
    return apply;
  }
  WifiLink probe_target(const WifiProfile&) noexcept override { return link; }
  bool restore_network() noexcept override {
    ++restorations; check(fixture.matches(fixture.original), "original persisted before network restore"); return restore;
  }
};
int main() {
  {
    Fixture f; Platform p(f); WifiManagedBackend b(f.state, f.netplan, p); WifiChange change(b);
    check(change.start(f.target), "start integrated change");
    auto record = f.record();
    check(record.phase == RecoveryPhase::Pending && record.previous_exists && record.previous.size(), "durable backup remains pending");
    // Caller may release/move its input; coordinator owns its own secret profile.
    auto moved = std::move(f.target);
    const auto ticket = change.ticket();
    check(!change.confirm("wrong ticket"), "wrong bearer ticket rejected");
    check(change.confirm(ticket) && change.state() == WifiChangeState::Committed, "confirm integrated change");
    record = f.record();
    check(record.phase == RecoveryPhase::Committed && !record.previous.size(), "commit clears recovery secret");
    check(f.matches(moved) && p.applications == 1 && p.restorations == 0 && !b.rollback(), "committed candidate cannot be undone");
  }
  for (int failure = 0; failure < 3; ++failure) {
    Fixture f; Platform p(f);
    if (failure == 0) p.scope = false;
    if (failure == 1) p.watchdog = false;
    if (failure == 2) p.ethernet = false;
    WifiManagedBackend b(f.state, f.netplan, p); WifiChange change(b);
    check(!change.start(f.target) && !p.applications && !p.restorations && f.matches(f.original), "unsafe preparation changes nothing");
    f.record(false);
  }
  {
    Fixture f; Platform p(f); p.fail_watchdog_at = 3;
    WifiManagedBackend b(f.state, f.netplan, p); WifiChange change(b);
    check(!change.start(f.target) && !p.applications && p.restorations == 1, "watchdog lost between prepare and activate");
    check(f.record().phase == RecoveryPhase::Restored && f.matches(f.original), "failed activation resolves recovery");
  }
  {
    Fixture f; Platform p(f); p.apply = false;
    WifiManagedBackend b(f.state, f.netplan, p); WifiChange change(b);
    check(!change.start(f.target) && change.state() == WifiChangeState::RolledBack, "apply failure rolls back");
    check(p.applications == 1 && p.restorations == 1 && f.matches(f.original), "apply failure restores original");
    check(b.rollback() && p.restorations == 1, "rollback is idempotent");
  }
  {
    Fixture f; Platform p(f); p.apply = false; p.restore = false;
    WifiManagedBackend b(f.state, f.netplan, p); WifiChange change(b);
    check(!change.start(f.target) && change.state() == WifiChangeState::RecoveryRequired, "failed restore requires recovery");
    check(f.record().phase == RecoveryPhase::Pending, "failed restore preserves pending evidence");
    p.restore = true;
    check(b.rollback() && f.record().phase == RecoveryPhase::Restored, "recovery can retry");
  }
  {
    Fixture f; Platform p(f); WifiManagedBackend b(f.state, f.netplan, p);
    {
      WifiChange change(b); check(change.start(f.target), "pending owner");
      const auto identity = f.record().transaction;
      Platform other_platform(f); WifiManagedBackend other(f.state, f.netplan, other_platform); WifiChange second(other);
      check(!second.start(f.target) && !other_platform.applications && !other_platform.restorations, "second owner cannot alter first transaction");
      check(f.record().transaction == identity && f.record().phase == RecoveryPhase::Pending && f.matches(f.target), "first transaction preserved");
    }
    check(p.restorations == 1 && f.matches(f.original), "pending owner destructor rolls back");
  }
  {
    Fixture f; Platform p(f); WifiManagedBackend b(f.state, f.netplan, p); WifiChange change(b);
    check(change.start(f.target), "start before readiness loss");
    p.link = WifiLink::Failed; change.tick();
    check(change.state() == WifiChangeState::RolledBack && f.matches(f.original), "readiness loss restores original");
  }
  {
    Fixture f; Platform p(f); WifiManagedBackend b(f.state, f.netplan, p); WifiChange change(b);
    check(change.start(f.target), "start before scope conflict");
    const auto ticket = change.ticket(); p.scope = false;
    check(!change.confirm(ticket) && change.state() == WifiChangeState::RolledBack,
      "confirmation repeats scope checks even after readiness");
    check(f.matches(f.original) && f.record().phase == RecoveryPhase::Restored, "failed confirmation restores original");
  }
  {
    Fixture f; Platform p(f); p.apply_delay_ms = 1100;
    WifiManagedBackend b(f.state, f.netplan, p);
    check(b.prepare(f.target, std::chrono::seconds(1)), "short recovery lifetime");
    check(!b.activate() && f.record().phase == RecoveryPhase::Pending, "slow apply cannot extend recovery deadline");
    check(b.rollback() && f.matches(f.original), "expired application still rolls back");
  }
  {
    Fixture f; Platform p(f); p.fail_watchdog_at = 2;
    WifiManagedBackend b(f.state, f.netplan, p); WifiChange change(b);
    check(!change.start(f.target) && !p.applications && p.restorations == 1,
      "watchdog failure after durable begin is recoverable");
    check(f.record().phase == RecoveryPhase::Restored, "partial preparation is cancelled");
  }
  std::cout << "Managed backend fixture checks passed; no real network changes\n";
}
