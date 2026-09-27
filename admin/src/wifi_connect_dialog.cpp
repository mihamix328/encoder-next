#include "wifi_connect_dialog.h"
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <atomic>
#include <thread>

WifiConnectDialog::WifiConnectDialog(const QString& ssid, WifiConnectRequest request, QWidget* parent)
    : QDialog(parent) {
  setWindowTitle("Подключение к Wi-Fi");
  resize(480, 300);
  auto* layout = new QVBoxLayout(this);
  auto* note = new QLabel("Поддерживается WPA2-Personal (CCMP). Подключите ПК к плате по Ethernet. "
      "После проверки новой сети подтвердите подключение. Без подтверждения сервер должен вернуть прежнюю сеть. "
      "Закрытие окна не отменяет операцию: восстановлением управляет сервер.", this);
  note->setWordWrap(true);
  layout->addWidget(note);
  auto* network = new QLineEdit(ssid, this);
  network->setObjectName("connectSsid");
  network->setPlaceholderText("Название сети (SSID)");
  network->setMaxLength(32);
  layout->addWidget(network);
  auto* password = new QLineEdit(this);
  password->setObjectName("connectPassword");
  password->setEchoMode(QLineEdit::Password);
  password->setMaxLength(64);
  password->setPlaceholderText("Пароль сети");
  layout->addWidget(password);
  auto* status = new QLabel(request ? "Введите название сети и пароль." :
      "Подключение пока недоступно: серверный обработчик ещё не подключён.", this);
  status->setObjectName("connectStatus");
  status->setTextFormat(Qt::PlainText);
  status->setWordWrap(true);
  layout->addWidget(status);
  auto* start = new QPushButton("Подключить", this);
  start->setObjectName("connectStart");
  start->setEnabled(bool(request));
  auto* confirm = new QPushButton("Подтвердить новую сеть", this);
  confirm->setObjectName("connectConfirm");
  confirm->setEnabled(false);
  auto* cancel = new QPushButton("Отменить и вернуть прежнюю сеть", this);
  cancel->setObjectName("connectCancel");
  cancel->setEnabled(false);
  auto* close = new QPushButton("Закрыть", this);
  close->setObjectName("connectClose");
  for (auto* button : {start, confirm, cancel, close}) layout->addWidget(button);
  connect(close, &QPushButton::clicked, this, &WifiConnectDialog::reject);
  struct Pending { std::atomic<bool> done{false}; WifiConnectReply reply; };
  struct State { std::shared_ptr<Pending> pending; };
  auto state = std::make_shared<State>();
  auto* poll = new QTimer(this);
  poll->setInterval(50);
  auto* refresh = new QTimer(this);
  refresh->setInterval(1500);
  auto send = [=](WifiConnectAction action, std::shared_ptr<const encoder::WifiProfile> profile = {}) {
    if (!request || state->pending) return;
    state->pending = std::make_shared<Pending>();
    confirm->setEnabled(false);
    cancel->setEnabled(false);
    // Workers never capture widgets. Destroying the UI cannot access freed memory.
    try {
      std::thread([pending = state->pending, request, action, profile = std::move(profile)] {
        try { pending->reply = request(action, profile); } catch (...) {}
        pending->done.store(true);
      }).detach();
    } catch (...) { state->pending->done.store(true); }
    poll->start();
  };
  connect(start, &QPushButton::clicked, this, [=] {
    auto secret = password->text().toUtf8();
    std::string error;
    auto profile = encoder::WifiProfile::make(network->text().toStdString(),
        std::string_view(secret.constData(), static_cast<size_t>(secret.size())), &error);
    secret.fill('\0');
    password->clear();
    if (!profile) {
      status->setText("Проверьте SSID (1–32 байта UTF-8) и пароль (8–63 печатных ASCII-символа или 64 hex-символа).");
      return;
    }
    active_ = true;
    start->setEnabled(false);
    close->setEnabled(false);
    network->setEnabled(false);
    password->setEnabled(false);
    status->setText("Подключение… Ожидаем подтверждённое состояние от сервера.");
    send(WifiConnectAction::Start, std::make_shared<encoder::WifiProfile>(std::move(*profile)));
  });
  connect(confirm, &QPushButton::clicked, this, [=] { send(WifiConnectAction::Confirm); });
  connect(cancel, &QPushButton::clicked, this, [=] { send(WifiConnectAction::Cancel); });
  connect(refresh, &QTimer::timeout, this, [=] { send(WifiConnectAction::Status); });
  connect(poll, &QTimer::timeout, this, [=] {
    if (!state->pending || !state->pending->done.load()) return;
    poll->stop();
    const auto reply = state->pending->reply;
    state->pending.reset();
    close->setEnabled(true);
    if (!reply.ok) {
      status->setText("Ответ не получен. Результат операции неизвестен. Проверяем состояние; "
          "не отключайте Ethernet. Закрытие окна не подтверждает подключение.");
      cancel->setEnabled(true);
      refresh->start();
      return;
    }
    switch (reply.state) {
      case WifiConnectState::Connecting:
        status->setText("Плата подключается к сети. Ожидаем проверку адреса и защиты."); break;
      case WifiConnectState::AwaitingConfirmation:
        status->setText("Сервер проверил новое подключение. Подтвердите его до окончания серверного срока ожидания.");
        confirm->setEnabled(true); break;
      case WifiConnectState::Committed:
        status->setText("Новая сеть подтверждена и сохранена."); active_ = false; break;
      case WifiConnectState::RolledBack:
        status->setText("Прежние настройки сети восстановлены."); active_ = false; break;
      case WifiConnectState::RecoveryRequired:
        status->setText("Восстановление пока не подтверждено. Сохраните Ethernet-подключение и проверьте плату."); break;
      case WifiConnectState::Rejected:
        status->setText("Сервер отклонил запрос без изменения сети. Проверьте права и условия подключения.");
        active_ = false; break;
      default:
        status->setText("Неизвестное состояние сервера. Подтверждение запрещено."); break;
    }
    cancel->setEnabled(active_);
    if (active_) refresh->start(); else refresh->stop();
  });
}

