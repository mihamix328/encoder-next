#include "wifi_recovery_worker.h"
#include "netplan_files.h"
#include "encoder/wifi_config_draft.h"
#include <openssl/crypto.h>
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
bool wifi_managed_commit(const std::string& state_directory,
    const std::string& netplan_directory, const std::string& transaction,
    const WifiProfile& target, const std::function<bool()>& verify_live, std::string* error) {
  auto fail = [&](const char* message) { *error = message; return false; };
  auto journal = WifiJournal::open(state_directory, error);
  if (!journal) return false;
  RecoveryRecord record; bool exists;
  if (!journal->load(&record, &exists, error)) return false;
  std::string boot; uint64_t now;
  if (!recovery_clock(&boot, &now, error)) return false;
  if (!exists || record.phase != RecoveryPhase::Pending || record.transaction != transaction ||
      record.boot_id != boot || now >= record.deadline_ms)
    return fail("Pending Wi-Fi transaction cannot be confirmed");
  auto files = NetplanFiles::open(netplan_directory, error);
  SecureBuffer persisted;
  if (!files || !files->backup(&exists, &persisted, error)) return false;
  auto expected = make_wifi_config_draft(target, nullptr);
  if (!exists || !expected || persisted.size() != expected->netplan_yaml.size() ||
      CRYPTO_memcmp(persisted.data(), expected->netplan_yaml.data(), persisted.size()))
    return fail("Persisted Wi-Fi profile does not match the confirmation target");
  bool verified = false;
  try { verified = verify_live(); } catch (...) { verified = false; }
  if (!verified) return fail("Live Wi-Fi confirmation checks failed");
  SecureBuffer checked_again;
  if (!files->backup(&exists, &checked_again, error)) return false;
  if (!exists || checked_again.size() != expected->netplan_yaml.size() ||
      CRYPTO_memcmp(checked_again.data(), expected->netplan_yaml.data(), checked_again.size()))
    return fail("Wi-Fi candidate changed during confirmation checks");
  // Slow socket/interface checks must never extend the confirmation window.
  if (!recovery_clock(&boot, &now, error)) return false;
  return journal->commit(transaction, boot, now, error);
}
}
