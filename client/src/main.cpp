#include "main_window.h"
#include "login_dialog.h"
#include "client_core.h"

#include "encoder/config.h"

#include <QApplication>
#include "../../common/gui_theme.h"
#include <QIcon>
#include <QMessageBox>
#include <QSettings>

#include <filesystem>
#include <vector>

namespace {


std::string resolve_relative_path(const std::string& value,
                                  const std::string& config_path,
                                  const std::filesystem::path& exe_path) {
  if (value.empty()) return value;
  std::filesystem::path p(value);
  if (p.is_absolute()) return p.string();
  std::vector<std::filesystem::path> bases;
  if (!config_path.empty()) {
    bases.push_back(std::filesystem::path(config_path).parent_path());
  }
  if (!exe_path.empty()) {
    bases.push_back(exe_path.parent_path());
  }
  bases.push_back(std::filesystem::current_path());

  std::error_code ec;
  for (const auto& base : bases) {
    if (base.empty()) continue;
    std::filesystem::path candidate = base / p;
    if (std::filesystem::exists(candidate, ec)) {
      return candidate.string();
    }
  }
  return value;
}


} // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  app.setWindowIcon(QIcon(":/app/assets/app_icon.svg"));
  encoder::applyDarkTheme(app);

  namespace fs = std::filesystem;
  fs::path exe_path = fs::absolute(argv[0]);

  encoder::Config config;
  std::string config_path = (exe_path.parent_path() / "config" / "client.conf").string();
  bool loaded = config.load(config_path);
  if (!loaded) {
    std::vector<fs::path> candidates = {
        exe_path.parent_path() / "config" / "client.conf",
        exe_path.parent_path() / ".." / "config" / "client.conf",
        fs::current_path() / ".." / "config" / "client.conf",
    };
    for (const auto& path : candidates) {
      if (config.load(path.string())) {
        loaded = true;
        config_path = path.string();
        break;
      }
    }
  }

  encoder::ClientConfig client_cfg;
  client_cfg.host = config.get("server_host", "127.0.0.1");
  client_cfg.port = config.get_int("server_port", 7443);
  client_cfg.ca_file = resolve_relative_path(config.get("ca_file"), config_path, exe_path);
  client_cfg.client_cert = resolve_relative_path(config.get("client_cert"), config_path, exe_path);
  client_cfg.client_key = resolve_relative_path(config.get("client_key"), config_path, exe_path);
  client_cfg.verify_peer = config.get_bool("verify_peer", true);
  client_cfg.default_key_storage = config.get("default_key_storage", "server");
  client_cfg.clipboard_max_bytes = static_cast<size_t>(config.get_int("clipboard_max_bytes", 0));
  client_cfg.decrypt_to_temp = config.get_bool("decrypt_to_temp", false);
  client_cfg.demo_mode = config.get_bool("demo_mode", false);
  QSettings connection(QSettings::IniFormat, QSettings::UserScope, "encoeder", "client");
  client_cfg.host = connection.value("connection/host", QString::fromStdString(client_cfg.host)).toString().toStdString();
  client_cfg.port = connection.value("connection/port", client_cfg.port).toInt();
  client_cfg.ca_file = connection.value("connection/certificate", QString::fromStdString(client_cfg.ca_file)).toString().toStdString();

  if (!loaded) {
    QMessageBox::warning(nullptr, "encoder",
                         "Файл config/client.conf не найден. Используются значения по умолчанию; TLS может не работать.");
  }

  QString session_user;
  QString session_pass;
  for (;;) {
    LoginDialog login;
    login.setDefaults(QString::fromStdString(client_cfg.host), client_cfg.port);
    login.setCertificate(QString::fromStdString(client_cfg.ca_file));
    if (login.exec() != QDialog::Accepted) {
      return 0;
    }

    client_cfg.host = login.host().toStdString();
    client_cfg.port = login.port();
    client_cfg.ca_file = login.certificate().toStdString();

    encoder::ClientCore auth_client(client_cfg);
    std::string auth_err;
    if (!auth_client.authenticate(login.username().toStdString(),
                                  login.password().toStdString(),
                                  &auth_err, &client_cfg.permissions)) {
      QMessageBox::warning(nullptr, "Вход в систему",
                           "Ошибка авторизации: " + QString::fromStdString(auth_err));
      continue;
    }

    session_user = login.username();
    session_pass = login.password();
    break;
  }

  connection.setValue("connection/host", QString::fromStdString(client_cfg.host));
  connection.setValue("connection/port", client_cfg.port);
  connection.setValue("connection/certificate", QString::fromStdString(client_cfg.ca_file));
  connection.sync();
  if (connection.status() != QSettings::NoError) {
    QMessageBox::warning(nullptr, "encoder", "Подключение выполнено, но сохранить настройки не удалось.");
  }

  MainWindow window(client_cfg, session_user, session_pass);
  window.show();

  return app.exec();
}
