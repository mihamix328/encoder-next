#include "wifi_recovery_worker.h"
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
}
