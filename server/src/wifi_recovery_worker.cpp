#include "wifi_recovery_worker.h"
#include "netplan_files.h"
namespace encoder {
namespace {
bool restore_managed(const RecoveryRecord& record, const std::string& netplan_directory,
    const std::function<bool()>& reconfigure, std::string* error) {
  auto files = NetplanFiles::open(netplan_directory, error);
  return files && files->restore(record.previous_exists, record.previous, error) && reconfigure();
}
}
RecoveryOutcome wifi_recovery_tick(const std::string& directory,
    const std::function<bool(const RecoveryRecord&)>& restore, std::string* error) {
  auto journal = WifiJournal::open(directory, error);
  if (!journal) return RecoveryOutcome::Failed;
  // Read boot time after taking the lock; confirmation must obey the same order.
  std::string boot;
  uint64_t now;
  if (!recovery_clock(&boot, &now, error)) return RecoveryOutcome::Failed;
  return journal->recover_due(boot, now, restore, error);
}
RecoveryOutcome wifi_managed_recovery_tick(const std::string& state_directory,
    const std::string& netplan_directory, const std::function<bool()>& reconfigure, std::string* error) {
  return wifi_recovery_tick(state_directory, [&](const RecoveryRecord& record) {
    return restore_managed(record, netplan_directory, reconfigure, error);
  }, error);
}
RecoveryOutcome wifi_managed_cancel(const std::string& state_directory,
    const std::string& netplan_directory, const std::string& transaction,
    const std::function<bool()>& reconfigure, std::string* error) {
  auto journal = WifiJournal::open(state_directory, error);
  if (!journal) return RecoveryOutcome::Failed;
  std::string boot; uint64_t now;
  if (!recovery_clock(&boot, &now, error)) return RecoveryOutcome::Failed;
  return journal->cancel(transaction, boot, [&](const RecoveryRecord& record) {
    return restore_managed(record, netplan_directory, reconfigure, error);
  }, error);
}
}
