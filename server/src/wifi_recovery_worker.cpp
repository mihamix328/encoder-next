#include "wifi_recovery_worker.h"
#include "netplan_files.h"
namespace encoder {
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
    auto files = NetplanFiles::open(netplan_directory, error);
    return files && files->restore(record.previous_exists, record.previous, error) && reconfigure();
  }, error);
}
}
