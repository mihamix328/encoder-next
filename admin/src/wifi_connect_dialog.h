#pragma once
#include <QDialog>
#include <functional>
#include <memory>
#include <string>
#include "encoder/wifi_profile.h"

// Trusted transport adapter, not a wire protocol. It must own its dependencies,
// authenticate every operation and enforce deadlines on the server.
enum class WifiConnectAction { Start, Status, Confirm, Cancel };
enum class WifiConnectState { Connecting, AwaitingConfirmation, Committed, RolledBack, RecoveryRequired, Rejected };
struct WifiConnectReply {
  bool ok = false;
  WifiConnectState state = WifiConnectState::Connecting;
};
using WifiConnectRequest = std::function<WifiConnectReply(
    WifiConnectAction, std::shared_ptr<const encoder::WifiProfile>)>;

class WifiConnectDialog : public QDialog {
 public:
  WifiConnectDialog(const QString& ssid, WifiConnectRequest request, QWidget* parent = nullptr);
 private:
  bool active_ = false;
};
