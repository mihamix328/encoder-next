// Manual opt-in check: uses the real dialog and TLS client, may scan real Wi-Fi.
// Not registered in CTest. No saved device settings are read or changed.
#include "network_dialog.h"
#include "admin_client.h"
#include "encoder/config.h"
#include "../../common/gui_theme.h"
#include <QApplication>
#include <QTimer>
#include <QElapsedTimer>
#include <QPushButton>
#include <QTableWidget>
#include <QLabel>
#include <iostream>

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  if (argc != 2) { std::cerr << "Usage: encoder-admin-live-check CONFIG\n"; return 2; }
  encoder::Config settings;
  if (!settings.load(argv[1])) return 2;
  encoder::AdminConfig config;
  config.ca_file = settings.get("ca_file");
  config.verify_peer = true;
  encoder::AdminDevice device;
  device.name = "Isolated live test";
  device.host = settings.get("host", "localhost");
  device.port = settings.get_int("port", 0);
  device.token = settings.get("token");
  if (device.port <= 0 || device.port > 65535 || device.token.empty() || config.ca_file.empty()) return 2;
  encoder::applyDarkTheme(app);
  encoder::AdminClient client(config);
  NetworkDialog dialog("ТЕСТ — рабочий сервер не используется",
      [client, device](const std::string& operation, std::string* text, std::string* error) mutable {
        encoder::Header request;
        request.set("op", operation);
        return client.user_command(device, request, text, error);
      });
  dialog.show();
  auto* scan = dialog.findChild<QPushButton*>("scanWifi");
  auto* status = dialog.findChild<QLabel*>("networkStatus");
  auto* table = dialog.findChild<QTableWidget*>("wifiNetworks");
  QElapsedTimer elapsed;
  elapsed.start();
  QTimer timer;
  int step = 0, rows = -1, result = 1;
  QObject::connect(&timer, &QTimer::timeout, &dialog, [&]() {
    if (elapsed.elapsed() > 85000) {
      std::cerr << "FAILED: GUI check deadline exceeded\n"; app.exit(1); return;
    }
    if (!scan || !status || !table || !scan->isEnabled()) return;
    if (step == 0) {
      if (status->text().contains("Ошибка")) {
        std::cerr << "FAILED: initial network request: " << status->text().toStdString() << '\n'; app.exit(1); return;
      }
      ++step;
      scan->click();
    } else if (step == 1) {
      if (!status->text().contains("завершения")) {
        std::cerr << "FAILED: scan did not complete; inspect dialog status\n"; app.exit(1); return;
      }
      rows = table->rowCount();
      std::cout << "OK: actual NetworkDialog + AdminClient TLS scan; " << rows << " displayed rows\n";
      ++step;
      scan->click();
    } else if (step == 2) {
      if (!status->text().contains("cooldown", Qt::CaseInsensitive) || table->rowCount() != rows) {
        std::cerr << "FAILED: repeat rejection or previous table retention\n"; app.exit(1); return;
      }
      std::cout << "OK: GUI shows cooldown and retains prior rows; no saved settings changed\n";
      result = 0;
      timer.stop();
      QTimer::singleShot(5000, &app, [&]() { app.exit(0); });
    }
  });
  timer.start(100);
  const int code = app.exec();
  return code == 0 ? result : code;
}
