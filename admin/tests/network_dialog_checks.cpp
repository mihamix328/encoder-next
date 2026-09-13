#include "network_dialog.h"
#include <QApplication>
#include <QEventLoop>
#include <QTimer>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QTableWidget>
#include <QLabel>
#include <QLineEdit>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <thread>

void check(bool ok, const char* message) {
  if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}
void events(int milliseconds) {
  QEventLoop loop;
  QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
  loop.exec();
}
int main(int argc, char** argv) {
  QApplication app(argc, argv);
  auto calls = std::make_shared<std::atomic<int>>(0);
  NetworkDialog dialog("test", [calls](const std::string& op, std::string* text, std::string* error) {
    const auto count = ++*calls;
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    if (op == "admin_network_status") { *text = "loopback snapshot"; return true; }
    if (op == "admin_wifi_status") {
      *text = count == 4 ? "wpa_state=COMPLETED\nssid=Test network\nip_address=10.0.0.59\n"
                         : "wpa_state=DISCONNECTED\n";
      return true;
    }
    if (count == 2) {
      *text = "bssid / frequency / signal level / flags / ssid\n"
          "aa:bb:cc:dd:ee:ff\t5180\t-66\t[WPA2]\tHome network\n"
          "aa:bb:cc:dd:ee:00\t2412\t-100\t[ESS]\t\n"
          "malformed row\n";
      return true;
    }
    *error = "diagnostics disabled"; return false;
  });
  auto* refresh = dialog.findChild<QPushButton*>("refreshNetwork");
  auto* wifi = dialog.findChild<QPushButton*>("refreshWifi");
  auto* view = dialog.findChild<QPlainTextEdit*>("networkResult");
  check(refresh && wifi && view, "network controls exist");
  check(!refresh->isEnabled() && !wifi->isEnabled(), "requests cannot overlap");
  events(400);
  check(view->toPlainText() == "loopback snapshot" && refresh->isEnabled(), "async success rendered");
  wifi->click();
  check(!wifi->isEnabled(), "wifi busy state");
  events(400);
  auto* networks = dialog.findChild<QTableWidget*>("wifiNetworks");
  check(networks && networks->rowCount() == 2, "wifi rows rendered");
  check(networks->item(0, 0)->text() == "Home network", "SSID column mapping");
  check(networks->item(1, 0)->text() == "(скрытая сеть)", "hidden SSID represented");
  auto* filter = dialog.findChild<QLineEdit*>("wifiFilter");
  auto* status = dialog.findChild<QLabel*>("networkStatus");
  check(filter && status, "filter and update status exist");
  check(view->toPlainText() == "loopback snapshot", "wifi does not erase IP snapshot");
  filter->setText("HOME");
  check(!networks->isRowHidden(0) && networks->isRowHidden(1), "case-insensitive SSID filter");
  filter->setText("ee:00");
  check(networks->isRowHidden(0) && !networks->isRowHidden(1), "BSSID filter");
  networks->sortItems(1, Qt::AscendingOrder);
  check(!networks->isRowHidden(0) && networks->isRowHidden(1), "filter follows rows after sorting");
  filter->clear();
  check(!networks->isRowHidden(0) && !networks->isRowHidden(1), "clear filter");
  networks->sortItems(1, Qt::AscendingOrder);
  check(networks->item(0, 1)->data(Qt::DisplayRole).toInt() == -100, "signal sorted numerically");
  wifi->click();
  check(networks->rowCount() == 2, "previous rows retained while refreshing");
  events(400);
  check(status->text().contains("diagnostics disabled"), "async failure rendered");
  check(networks->rowCount() == 2 && view->toPlainText() == "loopback snapshot", "failure preserves previous snapshots");
  check(calls->load() == 3, "one request per click");
  auto* current = dialog.findChild<QPushButton*>("refreshWifiStatus");
  check(current, "connection status button exists");
  current->click();
  events(400);
  auto* connection = dialog.findChild<QLabel*>("wifiConnection");
  check(connection && connection->text().contains("Подключено") && connection->text().contains("Test network"),
        "current Wi-Fi status rendered");
  current->click();
  events(400);
  check(connection->text().contains("Отключено") && !connection->text().contains("Test network"),
        "disconnected status removes previous SSID");
  auto finished = std::make_shared<std::atomic<bool>>(false);
  {
    NetworkDialog closing("close test", [finished](const std::string&, std::string*, std::string*) {
      std::this_thread::sleep_for(std::chrono::milliseconds(150));
      finished->store(true); return true;
    });
  }
  events(400);
  check(finished->load(), "worker completes safely after dialog destruction");
  std::cout << "Admin network UI async success, error, busy state and early close passed\n";
}
