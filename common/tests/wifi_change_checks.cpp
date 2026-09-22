#include "encoder/wifi_change.h"
#include <cstdlib>
#include <iostream>
#include <vector>
using namespace encoder;
void check(bool value, const char* message) { if (!value) { std::cerr << message << '\n'; std::exit(1); } }
struct Fake : WifiChangeBackend {
  bool ethernet = true, prepared = true, activated = true, committed = true, restored = true;
  WifiLink link = WifiLink::Pending;
  std::vector<std::string> calls;
  bool ethernet_recovery_available() noexcept override { return ethernet; }
  bool prepare(const WifiProfile&, std::chrono::seconds lifetime) noexcept override {
    calls.push_back("prepare"); return prepared && lifetime.count() == 90;
  }
  bool activate() noexcept override { calls.push_back("activate"); return activated; }
  WifiLink probe() noexcept override { return link; }
  bool commit() noexcept override { calls.push_back("commit"); return committed; }
  bool rollback() noexcept override { calls.push_back("rollback"); return restored; }
};
int main() {
  std::string error;
  auto profile = WifiProfile::make("IEEE", "password", &error);
  check(profile.has_value() && error.empty(), "valid profile");
  check(profile->ssid_hex() == "49454545", "SSID encoded, not interpolated");
  const std::string vector = "f42c6fc52df0ebef9ebb4b90b38a5f902e83fe1b135a70e23aed762e9710a12e";
  auto raw = WifiProfile::make("IEEE", vector, &error);
  check(raw && raw->psk().size() == 32, "raw PSK accepted");
  for (size_t i = 0; i < 32; ++i) check(raw->psk().data()[i] == profile->psk().data()[i], "PBKDF2 known vector");
  for (const auto& name : {std::string(), std::string(33, 'x'), std::string("bad\nssid"), std::string("a\0b", 3)})
    check(!WifiProfile::make(name, "password", &error), "invalid SSID rejected");
  for (const auto& pass : {std::string("short"), std::string(65, 'x'), std::string(64, 'z'), std::string("bad\npassword")}) {
    check(!WifiProfile::make("Test", pass, &error), "invalid password rejected");
    check(error.find(pass) == std::string::npos, "error does not echo password");
  }
  check(WifiProfile::make(" quote\";$(x) ", " password ", &error).has_value(), "quotes and spaces encoded without shell");
  for (const auto& name : {std::string("\x80"), std::string("\xc0\xaf"), std::string("\xe0\x80\xaf"),
       std::string("\xed\xa0\x80"), std::string("\xf4\x90\x80\x80"), std::string("\xf5\x80\x80\x80"),
       std::string("\xe2\x82"), std::string("\xc2 "), std::string("\xc2\x85"),
       std::string("\xe2\x80\xa8"), std::string("\xe2\x80\xa9"), std::string("\xef\xbf\xbe"),
       std::string("\xef\xbf\xbf"), std::string("\xef\xb7\x90"), std::string("\xf4\x8f\xbf\xbf")}) {
    check(!WifiProfile::make(name, "password", &error), "malformed UTF-8 and Unicode controls rejected");
    check(!error.empty(), "invalid text has an actionable error");
  }
  auto unicode = WifiProfile::make("\xd0\x94\xd0\xbe\xd0\xbc \xf0\x9f\x8f\xa0", "password", &error);
  check(unicode && error.empty() && unicode->ssid_hex() == "d094d0bed0bc20f09f8fa0", "Unicode SSID preserved byte for byte");
  std::string boundary;
  for (int i = 0; i < 16; ++i) boundary += "\xd0\x94";
  check(WifiProfile::make(boundary, "password", &error).has_value(), "32-byte Unicode SSID accepted");
  boundary += "a";
  check(!WifiProfile::make(boundary, "password", &error), "SSID limit is bytes, not characters");
  const auto start = WifiChange::Clock::now();
  {
    auto source = WifiProfile::make("Test", "password", &error);
    auto destination = std::move(*source);
    Fake backend; WifiChange change(backend);
    check(!change.start(*source, start) && backend.calls.empty(), "consumed profile never activates");
  }
  {
    Fake backend; WifiChange change(backend);
    backend.ethernet = false;
    check(!change.start(*profile, start) && backend.calls.empty(), "no activation without Ethernet recovery");
  }
  {
    Fake backend; WifiChange change(backend);
    check(change.start(*profile, start), "start change");
    const std::string ticket = change.ticket();
    check(ticket.size() == 64 && !change.start(*profile, start), "single transaction and random ticket");
    check(!change.confirm(ticket, start), "cannot confirm without usable target link");
    backend.link = WifiLink::Ready;
    change.tick(start + std::chrono::seconds(1));
    check(change.state() == WifiChangeState::AwaitingConfirmation, "requires confirmation after association and IP");
    check(!change.confirm(std::string(64, 'x'), start), "wrong ticket rejected");
    check(change.confirm(ticket, start + std::chrono::seconds(2)), "matching confirmation commits");
    change.cancel(); change.tick(start + std::chrono::seconds(100));
    check(change.state() == WifiChangeState::Committed && change.ticket().empty(), "committed state terminal and ticket wiped");
    check(backend.calls == std::vector<std::string>({"prepare", "activate", "commit"}), "prepare before activate before persist");
  }
  for (int fault = 0; fault < 8; ++fault) {
    Fake backend;
    {
      WifiChange change(backend);
      if (fault == 0) backend.prepared = false;
      if (fault == 1) backend.activated = false;
      const bool started = change.start(*profile, start);
      check(started == (fault > 1), "prepare/activation failure surfaced");
      const std::string ticket = change.ticket();
      if (fault == 2) change.tick(start + std::chrono::seconds(90));
      if (fault == 3) { backend.link = WifiLink::Failed; change.tick(start); }
      if (fault == 4) { backend.ethernet = false; change.tick(start); }
      if (fault == 5) change.cancel();
      if (fault == 6) { backend.link = WifiLink::Ready; backend.committed = false; check(!change.confirm(ticket, start), "commit failure"); }
      if (fault != 7) check(change.state() == WifiChangeState::RolledBack && change.ticket().empty(), "failure rolls back and invalidates ticket");
      // fault 7 exercises destructor rollback on an abandoned pending operation.
    }
    check(backend.calls.back() == "rollback", "all failed or abandoned changes rollback");
  }
  {
    Fake backend; WifiChange change(backend); change.start(*profile, start);
    backend.restored = false; change.cancel();
    check(change.state() == WifiChangeState::RecoveryRequired && !change.start(*profile, start), "rollback failure is explicit, no new operation");
  }
  {
    Fake backend; WifiChange change(backend); change.start(*profile, start);
    const auto ticket = change.ticket(); backend.link = WifiLink::Ready;
    check(!change.confirm(ticket, start + std::chrono::seconds(90)), "deadline wins over confirmation");
  }
  {
    Fake backend; WifiChange change(backend); change.start(*profile, start);
    const auto ticket = change.ticket(); backend.link = WifiLink::Ready; change.tick(start);
    backend.link = WifiLink::Pending;
    check(!change.confirm(ticket, start), "connection is rechecked at confirmation");
    change.tick(start + std::chrono::seconds(90));
    check(change.state() == WifiChangeState::RolledBack, "loss of target cannot extend deadline");
  }
  std::cout << "Wi-Fi change core passed: validation, PSK vector, confirmation, deadline, cancellation, rollback and recovery failure\n";
}
