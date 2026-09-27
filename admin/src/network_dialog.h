#pragma once
#include <QDialog>
#include <functional>
#include <string>
#include "wifi_connect_dialog.h"

// Request is copied into each worker; it must own its non-UI dependencies.
class NetworkDialog : public QDialog {
 public:
  using Request = std::function<bool(const std::string&, std::string*, std::string*)>;
  NetworkDialog(const QString& name, Request request, QWidget* parent = nullptr,
                WifiConnectRequest connect_request = {});
};
