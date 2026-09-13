#include "network_dialog.h"
#include <QDialogButtonBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QTableWidget>
#include <QHeaderView>
#include <QLineEdit>
#include <QDateTime>
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
  auto* status = new QLabel(this);
  status->setObjectName("networkStatus");
  status->setTextFormat(Qt::PlainText);
  status->setWordWrap(true);
  layout->addWidget(status);
  auto* filter = new QLineEdit(this);
  filter->setObjectName("wifiFilter");
  filter->setPlaceholderText("Поиск по названию сети или BSSID");
  filter->setClearButtonEnabled(true);
  filter->hide();
  layout->addWidget(filter);
  auto* summary = new QLabel(this);
  summary->setObjectName("wifiSummary");
  summary->setTextFormat(Qt::PlainText);
  summary->setWordWrap(true);
  layout->addWidget(summary);
  auto* connection = new QLabel(this);
  connection->setObjectName("wifiConnection");
  connection->setTextFormat(Qt::PlainText);
  connection->setWordWrap(true);
  layout->addWidget(connection);
  auto* networks = new QTableWidget(0, 5, this);
  networks->setObjectName("wifiNetworks");
  networks->setHorizontalHeaderLabels({"Сеть (SSID)", "Сигнал*", "МГц", "Защита", "BSSID"});
  networks->setEditTriggers(QAbstractItemView::NoEditTriggers);
  networks->setSelectionBehavior(QAbstractItemView::SelectRows);
  networks->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
  networks->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  networks->hide();
  layout->addWidget(networks, 1);
  auto apply_filter = [=]() {
    const QString needle = filter->text().trimmed();
    for (int row = 0; row < networks->rowCount(); ++row) {
      const bool match = networks->item(row, 0)->text().contains(needle, Qt::CaseInsensitive) ||
                         networks->item(row, 4)->text().contains(needle, Qt::CaseInsensitive);
      networks->setRowHidden(row, !match);
    }
  };
  connect(filter, &QLineEdit::textChanged, this, [=]() { apply_filter(); });
  connect(networks->model(), &QAbstractItemModel::layoutChanged, this, [=]() { apply_filter(); });
  auto* refresh = new QPushButton("Обновить IP и интерфейсы", this);
  refresh->setObjectName("refreshNetwork");
  auto* wifi = new QPushButton("Wi-Fi: прочитать сохранённый список", this);
  wifi->setObjectName("refreshWifi");
  layout->addWidget(refresh);
  layout->addWidget(wifi);
  auto* current = new QPushButton("Текущее подключение Wi-Fi", this);
  current->setObjectName("refreshWifiStatus");
  layout->addWidget(current);
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
    refresh->setEnabled(false);
    wifi->setEnabled(false);
    current->setEnabled(false);
    status->setText("Запрос к плате… Предыдущие данные пока не обновлены. Окно можно закрыть.");
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
    if (state->pending->ok && state->pending->operation == "admin_wifi_results" &&
        state->pending->text.rfind("bssid / frequency / signal level / flags / ssid\n", 0) != 0) {
      state->pending->ok = false;
      state->pending->error = "Invalid Wi-Fi response format";
    }
    status->setText(state->pending->ok ? "Ответ получен в " + QDateTime::currentDateTime().toString("HH:mm:ss")
        : "Ошибка обновления: " + QString::fromStdString(state->pending->error) +
          "\nПредыдущие данные сохранены; они могут быть устаревшими.");
    if (state->pending->ok && state->pending->operation == "admin_network_status")
      view->setPlainText(QString::fromStdString(state->pending->text));
    if (state->pending->ok && state->pending->operation == "admin_wifi_status") {
      QString details = "Состояние на момент снимка (не проверка Интернета):\n";
      for (const auto& line : QString::fromStdString(state->pending->text).split('\n')) {
        const int split = line.indexOf('=');
        if (split < 0) continue;
        const auto key = line.left(split), value = line.mid(split + 1);
        if (key == "wpa_state") {
          const auto state_text = value == "COMPLETED" ? "Подключено" :
              value == "DISCONNECTED" ? "Отключено" : value == "SCANNING" ? "Поиск сети" : value;
          details += "Состояние: " + state_text + "\n";
        } else if (key == "ssid") details += "Сеть: " + value + "\n";
        else if (key == "bssid") details += "BSSID: " + value + "\n";
        else if (key == "ip_address") details += "IP: " + value + "\n";
      }
      connection->setText(details);
    }
    if (state->pending->ok && state->pending->operation == "admin_wifi_results") {
      const auto lines = QString::fromStdString(state->pending->text).split('\n');
      networks->setSortingEnabled(false);
      networks->setRowCount(0);
      int skipped = 0;
      for (int i = 1; i < lines.size(); ++i) {
        if (lines[i].isEmpty()) continue;
        const auto fields = lines[i].split('\t');
        bool signal_ok = false, frequency_ok = false;
        if (fields.size() != 5) { ++skipped; continue; }
        const int signal = fields[2].toInt(&signal_ok);
        const int frequency = fields[1].toInt(&frequency_ok);
        if (!signal_ok || !frequency_ok || frequency <= 0 || networks->rowCount() >= 512) {
          ++skipped; continue;
        }
        const int row = networks->rowCount();
        networks->insertRow(row);
        const QStringList values{fields[4].isEmpty() ? "(скрытая сеть)" : fields[4],
                                 fields[2], fields[1], fields[3], fields[0]};
        for (int column = 0; column < 5; ++column)
          networks->setItem(row, column, new QTableWidgetItem(values[column]));
        networks->item(row, 1)->setData(Qt::DisplayRole, signal);
        networks->item(row, 2)->setData(Qt::DisplayRole, frequency);
      }
      summary->setText("Сохранённый список: " + QString::number(networks->rowCount()) +
          " сетей. Кэш может быть неполным или устаревшим.\n"
          "*Сигнал указан в формате драйвера. Экранирование SSID сохранено.\n" +
          (skipped ? "Пропущено строк (формат или лимит 512): " + QString::number(skipped) : QString()));
      networks->setSortingEnabled(true);
      networks->sortItems(1, Qt::DescendingOrder);
      apply_filter();
      filter->show();
      networks->show();
    }
    state->pending.reset();
    refresh->setEnabled(true);
    wifi->setEnabled(true);
    current->setEnabled(true);
  });
  connect(refresh, &QPushButton::clicked, this, [=]() { start("admin_network_status"); });
  connect(wifi, &QPushButton::clicked, this, [=]() { start("admin_wifi_results"); });
  connect(current, &QPushButton::clicked, this, [=]() { start("admin_wifi_status"); });
  start("admin_network_status");
}
