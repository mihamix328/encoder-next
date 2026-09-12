#include "network_dialog.h"
#include <QDialogButtonBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QTableWidget>
#include <QHeaderView>
#include <atomic>
#include <memory>
#include <thread>

NetworkDialog::NetworkDialog(const QString& name, Request request, QWidget* parent) : QDialog(parent) {
  setWindowTitle("Сеть платы — " + name);
  resize(680, 380);
  auto* layout = new QVBoxLayout(this);
  auto* explanation = new QLabel("Интерфейсы с IPv4. Carrier — наличие связи, не проверка Интернета.\n"
      "Настройки сети не меняются. Wi-Fi показывает кэш, не новый поиск сетей.", this);
  explanation->setWordWrap(true);
  layout->addWidget(explanation);
  auto* view = new QPlainTextEdit(this);
  view->setObjectName("networkResult");
  view->setReadOnly(true);
  layout->addWidget(view);
  auto* networks = new QTableWidget(0, 5, this);
  networks->setObjectName("wifiNetworks");
  networks->setHorizontalHeaderLabels({"Сеть (SSID)", "Сигнал*", "МГц", "Защита", "BSSID"});
  networks->setEditTriggers(QAbstractItemView::NoEditTriggers);
  networks->setSelectionBehavior(QAbstractItemView::SelectRows);
  networks->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
  networks->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  networks->hide();
  layout->addWidget(networks, 1);
  auto* refresh = new QPushButton("Обновить IP и интерфейсы", this);
  refresh->setObjectName("refreshNetwork");
  auto* wifi = new QPushButton("Wi-Fi: прочитать сохранённый список", this);
  wifi->setObjectName("refreshWifi");
  layout->addWidget(refresh);
  layout->addWidget(wifi);
  auto* close = new QDialogButtonBox(QDialogButtonBox::Close, this);
  connect(close, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addWidget(close);
  struct Result {
    std::atomic<bool> done{false};
    bool ok = false;
    std::string text, error, operation;
  };
  struct State { std::shared_ptr<Result> pending; };
  auto state = std::make_shared<State>();
  auto* poll = new QTimer(this);
  poll->setInterval(50);
  auto start = [=](const char* operation) {
    if (state->pending) return;
    state->pending = std::make_shared<Result>();
    state->pending->operation = operation;
    networks->hide();
    networks->setRowCount(0);
    refresh->setEnabled(false);
    wifi->setEnabled(false);
    view->setPlainText("Запрос к плате… Окно можно закрыть.");
    std::thread([result = state->pending, request, op = std::string(operation)]() {
      try { result->ok = request(op, &result->text, &result->error); }
      catch (...) { result->error = "Network request failed unexpectedly"; }
      result->done.store(true);
    }).detach();
    poll->start();
  };
  connect(poll, &QTimer::timeout, this, [=]() {
    if (!state->pending || !state->pending->done.load()) return;
    poll->stop();
    view->setPlainText(state->pending->ok ? QString::fromStdString(state->pending->text)
        : "Ошибка: " + QString::fromStdString(state->pending->error));
    if (state->pending->ok && state->pending->operation == "admin_wifi_results") {
      const auto lines = QString::fromStdString(state->pending->text).split('\n');
      int skipped = 0;
      for (int i = 1; i < lines.size(); ++i) {
        if (lines[i].isEmpty()) continue;
        const auto fields = lines[i].split('\t');
        if (fields.size() != 5) { ++skipped; continue; }
        const int row = networks->rowCount();
        networks->insertRow(row);
        const QStringList values{fields[4].isEmpty() ? "(скрытая сеть)" : fields[4],
                                 fields[2], fields[1], fields[3], fields[0]};
        for (int column = 0; column < 5; ++column)
          networks->setItem(row, column, new QTableWidgetItem(values[column]));
      }
      view->setPlainText("Сохранённый список: " + QString::number(networks->rowCount()) +
          " сетей. Кэш может быть неполным или устаревшим.\n"
          "*Сигнал указан в формате драйвера. Экранирование SSID сохранено.\n" +
          (skipped ? "Не удалось разобрать строк: " + QString::number(skipped) : QString()));
      networks->show();
    }
    state->pending.reset();
    refresh->setEnabled(true);
    wifi->setEnabled(true);
  });
  connect(refresh, &QPushButton::clicked, this, [=]() { start("admin_network_status"); });
  connect(wifi, &QPushButton::clicked, this, [=]() { start("admin_wifi_results"); });
  start("admin_network_status");
}
