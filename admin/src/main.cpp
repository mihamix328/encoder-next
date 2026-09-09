#include "admin_window.h"

#include "cipheator/config.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include "../../common/gui_theme.h"

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  cipheator::applyDarkTheme(app);

  cipheator::Config config;
  const QDir config_dir(QCoreApplication::applicationDirPath() + "/config");
  config.load(config_dir.filePath("admin.conf").toStdString());
  auto resolve = [&config_dir](const std::string& value) {
    if (value.empty() || QFileInfo(QString::fromStdString(value)).isAbsolute()) return value;
    return config_dir.filePath(QString::fromStdString(value)).toStdString();
  };

  cipheator::AdminConfig admin_cfg;
  admin_cfg.ca_file = resolve(config.get("ca_file"));
  admin_cfg.client_cert = resolve(config.get("client_cert"));
  admin_cfg.client_key = resolve(config.get("client_key"));
  admin_cfg.verify_peer = config.get_bool("verify_peer", true);

  AdminWindow window(admin_cfg);
  window.show();

  return app.exec();
}
