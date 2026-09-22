#pragma once
#include "encoder/secure_memory.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
namespace encoder {
enum class RecoveryPhase : uint8_t { Pending = 1, Committed = 2, Restored = 3 };
struct RecoveryRecord {
  RecoveryPhase phase = RecoveryPhase::Pending;
  bool previous_exists = false;
  std::string transaction; // 32 lowercase hex digits, not the confirmation ticket
  std::string boot_id;
  uint64_t deadline_ms = 0; // CLOCK_BOOTTIME, not wall clock
  SecureBuffer previous;
};
enum class RecoveryOutcome { Nothing, Waiting, Restored, Failed };
// Linux-only durable journal. Caller supplies a dedicated, trusted ancestor path;
// leaf directory must be owned by effective uid and inaccessible to others.
// Production will use root; tests use an unprivileged private temp directory.
// Holds an exclusive nonblocking lock until destroyed; never hold for the whole
// confirmation window. Recovery and confirmation must use the SAME lock.
class WifiJournal {
 public:
  static std::unique_ptr<WifiJournal> open(const std::string& directory, std::string* error);
  ~WifiJournal();
  WifiJournal(const WifiJournal&) = delete;
  WifiJournal& operator=(const WifiJournal&) = delete;
  bool load(RecoveryRecord* record, bool* exists, std::string* error);
  bool begin(const RecoveryRecord& record, std::string* error);
  bool commit(const std::string& transaction, const std::string& boot, uint64_t now_ms, std::string* error);
  // Explicit cancellation before the deadline. The privileged caller must
  // authenticate the request separately; transaction IDs are not credentials.
  // A stale request cannot cancel another transaction or a committed change.
  RecoveryOutcome cancel(const std::string& transaction, const std::string& boot,
      const std::function<bool(const RecoveryRecord&)>& restore, std::string* error);
  // Restore MUST be idempotent and restore the fixed adapter target + reload it.
  // No paths/commands are read from the journal. Failure keeps pending evidence.
  RecoveryOutcome recover_due(const std::string& boot, uint64_t now_ms,
      const std::function<bool(const RecoveryRecord&)>& restore, std::string* error);
 private:
  WifiJournal(int directory, int lock) : directory_(directory), lock_(lock) {}
  bool save(const RecoveryRecord& record, std::string* error);
  RecoveryOutcome restore_pending(RecoveryRecord& record,
      const std::function<bool(const RecoveryRecord&)>& restore, std::string* error);
  int directory_, lock_;
};
bool recovery_clock(std::string* boot, uint64_t* milliseconds, std::string* error);
}
