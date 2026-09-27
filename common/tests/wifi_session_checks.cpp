#include "encoder/wifi_session.h"
#include <cstdlib>
#include <iostream>
using namespace encoder;
void check(bool ok) { if (!ok) { std::cerr << "session check failed\n"; std::exit(1); } }
struct Backend : WifiChangeBackend {
  int probes = 0, commits = 0, rollbacks = 0;
  bool ethernet_recovery_available() noexcept override { return true; }
  bool prepare(const WifiProfile&, std::chrono::seconds) noexcept override { return true; }
  bool activate() noexcept override { return true; }
  WifiLink probe() noexcept override { ++probes; return WifiLink::Ready; }
  bool commit() noexcept override { ++commits; return true; }
  bool rollback() noexcept override { ++rollbacks; return true; }
};
int main() {
  auto profile = WifiProfile::make("test", "password123", nullptr); check(bool(profile));
  Backend backend; WifiSession session(backend); SecureBuffer ticket;
  check(session.start(*profile, &ticket)); check(ticket.size() == 64);
  std::string_view bearer(reinterpret_cast<const char*>(ticket.data()), ticket.size());
  auto state = WifiChangeState::Idle;
  check(!session.status("wrong", &state)); check(!session.cancel("wrong", &state));
  check(!session.confirm("wrong", &state)); check(backend.probes == 0 && backend.rollbacks == 0);
  check(session.status(bearer, &state) && state == WifiChangeState::AwaitingConfirmation);
  check(session.confirm(bearer, &state) && state == WifiChangeState::Committed);
  check(session.confirm(bearer, &state) && backend.commits == 1);
  check(session.cancel(bearer, &state) && state == WifiChangeState::Committed && backend.rollbacks == 0);
  check(!session.start(*profile, &ticket));
  Backend other;
  { WifiSession second(other); SecureBuffer own; check(second.start(*profile, &own));
    check(!second.cancel(bearer, &state));
    std::string_view ownBearer(reinterpret_cast<const char*>(own.data()), own.size());
    check(second.cancel(ownBearer, &state) && state == WifiChangeState::RolledBack);
    check(second.status(ownBearer, &state) && state == WifiChangeState::RolledBack);
  }
  check(other.rollbacks == 1);
  Backend abandoned;
  { WifiSession third(abandoned); SecureBuffer own; check(third.start(*profile, &own)); }
  check(abandoned.rollbacks == 1);
  std::cout << "Wi-Fi session authorization and terminal status passed\n";
}
