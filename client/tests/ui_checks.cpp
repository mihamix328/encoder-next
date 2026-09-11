#include "main_window.h"
#include "login_dialog.h"
#include "../../common/gui_theme.h"
#include <QApplication>
#include <QComboBox>
#include <QAbstractItemView>
#include <QDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <iostream>
#include <cstdlib>

void check(bool passed, const char* message) {
  if (!passed) { std::cerr << message << std::endl; std::exit(1); }
}

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  encoder::applyDarkTheme(app);
  QTemporaryDir isolated;
  check(isolated.isValid(), "temporary settings directory");
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, isolated.path());
  encoder::ClientConfig config;
  config.host = "127.0.0.1";
  for (unsigned rights = 0; rights < 4; ++rights) {
    config.permissions = rights;
    MainWindow window(config, "permission-test-user", "test-only-password");
    int found = 0;
    for (auto* button : window.findChildren<QPushButton*>()) {
      if (button->text() == QString::fromUtf8("Зашифровать")) {
        check(button->isEnabled() == ((rights & 1) != 0), "encrypt button follows permissions"); ++found;
      }
      if (button->text() == QString::fromUtf8("Расшифровать")) {
        check(button->isEnabled() == ((rights & 2) != 0), "decrypt button follows permissions"); ++found;
      }
    }
    check(found == 2, "permission buttons exist");
  }
  config.permissions = 3;
  // No authentication or encryption calls: this suite tests only local UI behavior.
  for (int attempt = 0; attempt < 6; ++attempt) {
    MainWindow window(config, "isolated-test-user", "test-only-password");
    window.show();
    app.processEvents();
    check(window.windowTitle() == "encoder", "window title");
    if (attempt == 0) {
      QComboBox* cipher = nullptr;
      for (auto* combo : window.findChildren<QComboBox*>()) {
        if (combo->count() > 10) cipher = combo;
      }
      check(cipher != nullptr, "cipher selector exists");
      cipher->showPopup();
      app.processEvents();
      check(cipher->view()->height() < 400, "cipher popup must be bounded");
      cipher->hidePopup();
      QPushButton* search = nullptr;
      for (auto* button : window.findChildren<QPushButton*>()) {
        if (button->text().startsWith(QString::fromUtf8("Поиск"))) search = button;
      }
      check(search != nullptr, "search button exists");
      QTimer::singleShot(0, [&]() {
        auto* dialog = qobject_cast<QDialog*>(app.activeModalWidget());
        check(dialog != nullptr, "search dialog opens");
        auto* query = dialog->findChild<QLineEdit*>();
        auto* list = dialog->findChild<QListWidget*>();
        check(query && list, "search widgets");
        query->setText("not-an-algorithm");
        check(list->count() == 0, "empty search result");
        query->setText("aes-256-gcm");
        check(list->count() == 1, "case-insensitive exact match");
        dialog->accept();
      });
      search->click();
      check(cipher->currentData().toString() == "aes-256-gcm", "search selects original cipher data");
    }
    QTimer::singleShot(0, [&]() {
      auto* dialog = qobject_cast<QDialog*>(app.activeModalWidget());
      check(dialog != nullptr, "password reminder opens");
      QPushButton* skip = nullptr;
      for (auto* button : dialog->findChildren<QPushButton*>()) {
        if (button->text() == QString::fromUtf8("Пропустить и выйти")) skip = button;
      }
      check((skip != nullptr) == (attempt < 5), "exactly five deferrals allowed");
      if (skip) skip->click(); else dialog->reject();
    });
    const bool closed = window.close();
    check(closed == (attempt < 5), "sixth close stays blocked after cancelling reminder");
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "encoeder", "client");
    const auto keys = settings.allKeys();
    check(keys.size() == 1, "only isolated reminder state is stored");
    check(settings.value(keys.front()).toInt() == std::min(attempt + 1, 5), "persistent deferral count");
  }
  std::cout << "UI checks passed: title, popup, search, five deferrals, mandatory sixth close\n";
  return 0;
}
