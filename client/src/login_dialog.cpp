#include "login_dialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QFileInfo>
#include <QIntValidator>
#include <QMessageBox>

LoginDialog::LoginDialog(QWidget* parent) : QDialog(parent) {
  setWindowTitle("Вход в систему");
  auto* layout = new QVBoxLayout(this);
  layout->setSpacing(10);
  layout->setContentsMargins(16, 16, 16, 16);
  auto* form = new QFormLayout();
  form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  form->setHorizontalSpacing(14);
  form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

  host_edit_ = new QLineEdit(this);
  port_edit_ = new QLineEdit(this);
  port_edit_->setValidator(new QIntValidator(1, 65535, port_edit_));
  host_edit_->setPlaceholderText("IP-адрес или имя Orange Pi");
  certificate_edit_ = new QLineEdit(this);
  certificate_edit_->setReadOnly(true);
  user_edit_ = new QLineEdit(this);
  pass_edit_ = new QLineEdit(this);
  pass_edit_->setEchoMode(QLineEdit::Password);

  auto* user_label = new QLabel("Логин:", this);
  auto* pass_label = new QLabel("Пароль:", this);
  user_label->setMinimumWidth(120);
  pass_label->setMinimumWidth(120);
  form->addRow(user_label, user_edit_);
  form->addRow(pass_label, pass_edit_);

  layout->addLayout(form);

  auto* toggle = new QToolButton(this);
  toggle->setText("Сетевые настройки");
  toggle->setCheckable(true);
  toggle->setChecked(true);
  layout->addWidget(toggle);

  auto* advanced_box = new QGroupBox("", this);
  auto* advanced_layout = new QFormLayout(advanced_box);
  advanced_layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  advanced_layout->setHorizontalSpacing(14);
  advanced_layout->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
  auto* host_label = new QLabel("Сервер:", advanced_box);
  auto* port_label = new QLabel("Порт:", advanced_box);
  host_label->setMinimumWidth(120);
  port_label->setMinimumWidth(120);
  advanced_layout->addRow(host_label, host_edit_);
  advanced_layout->addRow(port_label, port_edit_);
  auto* certificate_row = new QHBoxLayout();
  certificate_row->addWidget(certificate_edit_, 1);
  auto* browse = new QPushButton("Выбрать…", advanced_box);
  browse->setObjectName("secondary");
  certificate_row->addWidget(browse);
  advanced_layout->addRow("Сертификат:", certificate_row);
  auto* certificate_hint = new QLabel("Выберите сертификат .crt/.pem, полученный от администратора платы.", advanced_box);
  certificate_hint->setWordWrap(true);
  advanced_layout->addRow(certificate_hint);
  connect(browse, &QPushButton::clicked, this, [this]() {
    const auto path = QFileDialog::getOpenFileName(this, "Сертификат сервера", QString(), "Сертификаты (*.crt *.pem);;Все файлы (*)");
    if (!path.isEmpty()) certificate_edit_->setText(path);
  });
  advanced_box->setVisible(true);
  connect(toggle, &QToolButton::toggled, advanced_box, &QWidget::setVisible);
  layout->addWidget(advanced_box);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  if (auto* ok_btn = buttons->button(QDialogButtonBox::Ok)) {
    ok_btn->setText("Подключиться");
  }
  if (auto* cancel_btn = buttons->button(QDialogButtonBox::Cancel)) {
    cancel_btn->setText("Отмена");
  }
  connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
    if (host().isEmpty() || host().contains("://") || host().contains('/') || host().contains(' ')) {
      QMessageBox::warning(this, "Подключение", "Введите IP-адрес или имя сервера без http:// и пути.");
      return;
    }
    if (!port_edit_->hasAcceptableInput()) {
      QMessageBox::warning(this, "Подключение", "Порт должен быть числом от 1 до 65535.");
      return;
    }
    if (username().isEmpty() || password().isEmpty()) {
      QMessageBox::warning(this, "Подключение", "Введите логин и пароль.");
      return;
    }
    const QFileInfo cert(certificate());
    if (!cert.isFile() || !cert.isReadable()) {
      QMessageBox::warning(this, "Подключение", "Выберите доступный файл сертификата сервера.");
      return;
    }
    accept();
  });
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addWidget(buttons);

  user_edit_->setFocus();
  setMinimumWidth(500);
}

void LoginDialog::setDefaults(const QString& host, int port) {
  host_edit_->setText(host);
  port_edit_->setText(QString::number(port));
}

QString LoginDialog::username() const { return user_edit_->text(); }
QString LoginDialog::password() const { return pass_edit_->text(); }
QString LoginDialog::host() const { return host_edit_->text().trimmed(); }
void LoginDialog::setCertificate(const QString& path) { certificate_edit_->setText(path); }
QString LoginDialog::certificate() const { return certificate_edit_->text(); }

int LoginDialog::port() const {
  return port_edit_->text().toInt();
}
