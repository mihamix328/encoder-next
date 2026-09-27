#include "wifi_connect_dialog.h"
#include "encoder/wifi_session.h"
#include <QApplication>
#include <QEventLoop>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <chrono>
void check(bool ok, const char* message) { if (!ok) { std::cerr << message << '\n'; std::exit(1); } }
void events(int ms) { QEventLoop loop; QTimer::singleShot(ms, &loop, &QEventLoop::quit); loop.exec(); }
QPushButton* button(WifiConnectDialog& d, const char* name) { return d.findChild<QPushButton*>(name); }
struct SessionFixture : encoder::WifiChangeBackend {
  encoder::WifiSession session{*this};
  encoder::SecureBuffer ticket;
  bool ethernet_recovery_available() noexcept override { return true; }
  bool prepare(const encoder::WifiProfile&, std::chrono::seconds) noexcept override { return true; }
  bool activate() noexcept override { return true; }
  encoder::WifiLink probe() noexcept override { return encoder::WifiLink::Ready; }
  bool commit() noexcept override { return true; }
  bool rollback() noexcept override { return true; }
};
int main(int argc, char** argv) {
  QApplication app(argc, argv);
  // Production transaction/session core behind an in-process transport, not TLS
  // or real network changes. The worker owns all its dependencies.
  auto fixture = std::make_shared<SessionFixture>();
  WifiConnectDialog integrated("Home", [fixture](WifiConnectAction action, auto profile) {
    if (action == WifiConnectAction::Start &&
        (!profile || !fixture->session.start(*profile, &fixture->ticket))) return WifiConnectReply{};
    std::string_view ticket(reinterpret_cast<const char*>(fixture->ticket.data()), fixture->ticket.size());
    auto state = encoder::WifiChangeState::Idle;
    bool ok = action == WifiConnectAction::Confirm ? fixture->session.confirm(ticket, &state) :
        action == WifiConnectAction::Cancel ? fixture->session.cancel(ticket, &state) :
        fixture->session.status(ticket, &state);
    if (!ok) return WifiConnectReply{};
    switch (state) {
      case encoder::WifiChangeState::AwaitingConfirmation: return WifiConnectReply{true, WifiConnectState::AwaitingConfirmation};
      case encoder::WifiChangeState::Committed: return WifiConnectReply{true, WifiConnectState::Committed};
      case encoder::WifiChangeState::RolledBack: return WifiConnectReply{true, WifiConnectState::RolledBack};
      default: return WifiConnectReply{};
    }
  });
  integrated.findChild<QLineEdit*>("connectPassword")->setText("password123");
  button(integrated, "connectStart")->click(); events(200);
  check(button(integrated, "connectConfirm")->isEnabled(), "production session reports readiness to UI");
  button(integrated, "connectConfirm")->click(); events(200);
  check(integrated.findChild<QLabel*>("connectStatus")->text().contains("сохранена"), "UI confirms through production session");
  WifiConnectDialog unavailable("Home", {});
  check(!button(unavailable, "connectStart")->isEnabled(), "no transport must disable changes");
  auto calls = std::make_shared<std::atomic<int>>(0);
  WifiConnectDialog dialog("Home", [calls](WifiConnectAction action, std::shared_ptr<const encoder::WifiProfile> profile) {
    ++*calls;
    if (action == WifiConnectAction::Start) {
      check(profile && profile->ssid_hex() == "486f6d65", "validated profile sent");
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      return WifiConnectReply{true, WifiConnectState::AwaitingConfirmation};
    }
    return WifiConnectReply{true, action == WifiConnectAction::Confirm ? WifiConnectState::Committed : WifiConnectState::RolledBack};
  });
  auto* secret = dialog.findChild<QLineEdit*>("connectPassword");
  check(secret->echoMode() == QLineEdit::Password, "secret masked");
  secret->setText("short");
  button(dialog, "connectStart")->click();
  check(calls->load() == 0 && secret->text().isEmpty(), "invalid password not submitted and cleared");
  secret->setText("password123");
  button(dialog, "connectStart")->click();
  check(!button(dialog, "connectStart")->isEnabled() && !button(dialog, "connectConfirm")->isEnabled(), "busy excludes repeat and premature confirm");
  check(secret->text().isEmpty(), "password field cleared after start");
  events(350);
  check(button(dialog, "connectConfirm")->isEnabled(), "only server readiness allows confirm");
  button(dialog, "connectConfirm")->click();
  events(200);
  check(dialog.findChild<QLabel*>("connectStatus")->text().contains("сохранена"), "commit displayed");
  check(!button(dialog, "connectCancel")->isEnabled() && calls->load() == 2, "terminal state stops requests");
  WifiConnectDialog lost("Home", [](WifiConnectAction action, auto) {
    if (action == WifiConnectAction::Cancel) return WifiConnectReply{true, WifiConnectState::RolledBack};
    return WifiConnectReply{};
  });
  lost.findChild<QLineEdit*>("connectPassword")->setText("password123");
  button(lost, "connectStart")->click(); events(200);
  check(!button(lost, "connectConfirm")->isEnabled() && button(lost, "connectCancel")->isEnabled(), "lost response never means success");
  button(lost, "connectCancel")->click(); events(200);
  check(lost.findChild<QLabel*>("connectStatus")->text().contains("восстановлены"), "cancel result rendered");
  auto done = std::make_shared<std::atomic<bool>>(false);
  {
    WifiConnectDialog closing("Home", [done](auto, auto) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100)); done->store(true);
      return WifiConnectReply{};
    });
    closing.findChild<QLineEdit*>("connectPassword")->setText("password123");
    button(closing, "connectStart")->click();
  }
  events(250);
  check(done->load(), "worker survives UI destruction without widgets");
  for (const auto outcome : {WifiConnectState::Rejected, WifiConnectState::RecoveryRequired,
                             static_cast<WifiConnectState>(999)}) {
    WifiConnectDialog edge("Home", [outcome](auto, auto) { return WifiConnectReply{true, outcome}; });
    edge.findChild<QLineEdit*>("connectPassword")->setText("password123");
    button(edge, "connectStart")->click(); events(200);
    check(!button(edge, "connectConfirm")->isEnabled(), "rejection, recovery or unknown state cannot confirm");
    check(button(edge, "connectCancel")->isEnabled() == (outcome != WifiConnectState::Rejected),
          "only definitive rejection ends pending transaction");
  }
  std::cout << "Wi-Fi connection UI checks passed\n";
}
