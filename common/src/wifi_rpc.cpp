#include "encoder/wifi_rpc.h"
#include <vector>
namespace encoder {
namespace {
bool hex(std::string_view value, size_t min, size_t max) {
  if (value.size() < min || value.size() > max || value.size() % 2) return false;
  for (char c : value) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  return true;
}
std::string state_name(WifiChangeState s) {
  switch (s) {
    case WifiChangeState::Connecting: return "connecting";
    case WifiChangeState::AwaitingConfirmation: return "ready";
    case WifiChangeState::Committed: return "committed";
    case WifiChangeState::RolledBack: return "rolled_back";
    case WifiChangeState::RecoveryRequired: return "recovery_required";
    default: return "rejected";
  }
}
}
std::string WifiRpc::handle(const std::string& packet) {
  if (packet.size() > 256 || packet.find('\0') != std::string::npos) return "error";
  std::vector<std::string_view> fields;
  size_t begin = 0;
  for (;;) {
    const auto end = packet.find('|', begin);
    fields.emplace_back(packet.data() + begin, (end == std::string::npos ? packet.size() : end) - begin);
    if (end == std::string::npos) break;
    begin = end + 1;
  }
  if (fields.size() == 3 && fields[0] == "start") {
    if ((session_ && !session_->finished()) || !hex(fields[1], 2, 64) || !hex(fields[2], 64, 64)) return "rejected";
    std::string ssid;
    auto digit = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    for (size_t i = 0; i < fields[1].size(); i += 2)
      ssid += static_cast<char>((digit(fields[1][i]) << 4) | digit(fields[1][i+1]));
    auto profile = WifiProfile::make(ssid, fields[2], nullptr);
    if (!profile) return "rejected";
    session_.reset();
    backend_ = factory_();
    if (!backend_) return "rejected";
    session_ = std::make_unique<WifiSession>(*backend_);
    SecureBuffer ticket;
    if (!session_->start(*profile, &ticket)) return "recovery_required";
    return "connecting|" + std::string(reinterpret_cast<const char*>(ticket.data()), ticket.size());
  }
  if (fields.size() != 2 || !hex(fields[1], 64, 64) || !session_) return "error";
  auto state = WifiChangeState::Idle;
  bool ok = false;
  if (fields[0] == "status") ok = session_->status(fields[1], &state);
  else if (fields[0] == "confirm") ok = session_->confirm(fields[1], &state);
  else if (fields[0] == "cancel") ok = session_->cancel(fields[1], &state);
  return ok ? state_name(state) : "error";
}
void WifiRpc::tick() { if (session_) session_->tick(); }
}
