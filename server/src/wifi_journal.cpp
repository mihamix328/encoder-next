#include "wifi_journal.h"
#include <openssl/evp.h>
#include <openssl/crypto.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <atomic>
#include <cerrno>
#include <cstring>

namespace encoder {
namespace {
constexpr size_t header = 90, digest_size = 32, max_backup = 65536;
struct Fd { int value; ~Fd() { if (value >= 0) close(value); } };
bool fail(std::string* error, const char* message) { *error = message; return false; }
bool owned_file(int fd) {
  struct stat s{};
  return !fstat(fd, &s) && S_ISREG(s.st_mode) && s.st_uid == geteuid() && !(s.st_mode & 0077) && s.st_nlink == 1;
}
bool hex_string(const std::string& text, size_t length) {
  if (text.size() != length) return false;
  for (char c : text) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  return true;
}
bool boot_valid(const std::string& boot) {
  if (boot.size() != 36) return false;
  std::string digits;
  for (size_t i = 0; i < boot.size(); ++i) {
    if (i == 8 || i == 13 || i == 18 || i == 23) { if (boot[i] != '-') return false; }
    else digits += boot[i];
  }
  return hex_string(digits, 32);
}
bool valid(const RecoveryRecord& record) {
  const auto phase = static_cast<unsigned>(record.phase);
  return phase >= 1 && phase <= 3 && hex_string(record.transaction, 32) && boot_valid(record.boot_id) &&
      record.deadline_ms > 0 && record.previous.size() <= max_backup &&
      (record.previous_exists || record.previous.size() == 0);
}
void put(uint8_t* target, uint64_t value, size_t bytes) {
  for (size_t i = 0; i < bytes; ++i) { target[bytes-i-1] = static_cast<uint8_t>(value); value >>= 8; }
}
uint64_t get(const uint8_t* source, size_t bytes) {
  uint64_t value = 0; for (size_t i = 0; i < bytes; ++i) value = (value << 8) | source[i]; return value;
}
bool checksum(const uint8_t* data, size_t size, uint8_t* digest) {
  unsigned length = 0;
  return EVP_Digest(data, size, digest, &length, EVP_sha256(), nullptr) == 1 && length == digest_size;
}
}
std::unique_ptr<WifiJournal> WifiJournal::open(const std::string& path, std::string* error) {
  Fd dir{::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)};
  struct stat info{};
  if (dir.value < 0 || fstat(dir.value, &info) || info.st_uid != geteuid() || (info.st_mode & 0077)) {
    *error = "Recovery directory must be private and owned by the helper"; return nullptr;
  }
  Fd lock{openat(dir.value, "lock", O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, 0600)};
  if (lock.value < 0 || !owned_file(lock.value) || flock(lock.value, LOCK_EX | LOCK_NB)) {
    *error = "Recovery journal unsafe or busy"; return nullptr;
  }
  auto result = std::unique_ptr<WifiJournal>(new WifiJournal(dir.value, lock.value));
  dir.value = lock.value = -1;
  return result;
}
WifiJournal::~WifiJournal() { close(lock_); close(directory_); }
bool WifiJournal::load(RecoveryRecord* record, bool* exists, std::string* error) {
  *exists = false;
  Fd file{openat(directory_, "journal", O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK)};
  if (file.value < 0) return errno == ENOENT ? true : fail(error, "Cannot open recovery journal");
  struct stat info{};
  if (!owned_file(file.value) || fstat(file.value, &info) || info.st_size < static_cast<off_t>(header + digest_size) ||
      info.st_size > static_cast<off_t>(header + max_backup + digest_size)) return fail(error, "Unsafe or invalid recovery journal");
  SecureBuffer bytes(static_cast<size_t>(info.st_size));
  size_t read_size = 0;
  while (read_size < bytes.size()) {
    const auto n = read(file.value, bytes.data() + read_size, bytes.size() - read_size);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) return fail(error, "Truncated recovery journal");
    read_size += static_cast<size_t>(n);
  }
  uint8_t extra;
  if (read(file.value, &extra, 1) != 0) return fail(error, "Recovery journal changed during read");
  uint8_t hash[digest_size];
  const auto* b = bytes.data();
  if (std::memcmp(b, "ENCWIFI1", 8) || b[9] > 1 || get(b + 86, 4) != bytes.size() - header - digest_size ||
      !checksum(b, bytes.size() - digest_size, hash) ||
      CRYPTO_memcmp(hash, b + bytes.size() - digest_size, digest_size)) return fail(error, "Corrupted recovery journal");
  RecoveryRecord loaded;
  loaded.phase = static_cast<RecoveryPhase>(b[8]); loaded.previous_exists = b[9] != 0;
  loaded.transaction.assign(reinterpret_cast<const char*>(b + 10), 32);
  loaded.boot_id.assign(reinterpret_cast<const char*>(b + 42), 36);
  loaded.deadline_ms = get(b + 78, 8);
  loaded.previous.resize(bytes.size() - header - digest_size);
  if (loaded.previous.size()) std::memcpy(loaded.previous.data(), b + header, loaded.previous.size());
  if (!valid(loaded)) return fail(error, "Invalid recovery journal fields");
  *record = std::move(loaded); *exists = true; return true;
}
bool WifiJournal::save(const RecoveryRecord& record, std::string* error) {
  if (!valid(record)) return fail(error, "Invalid recovery record");
  SecureBuffer bytes(header + record.previous.size() + digest_size);
  auto* b = bytes.data();
  std::memcpy(b, "ENCWIFI1", 8); b[8] = static_cast<uint8_t>(record.phase); b[9] = record.previous_exists;
  std::memcpy(b + 10, record.transaction.data(), 32); std::memcpy(b + 42, record.boot_id.data(), 36);
  put(b + 78, record.deadline_ms, 8); put(b + 86, record.previous.size(), 4);
  if (record.previous.size()) std::memcpy(b + header, record.previous.data(), record.previous.size());
  if (!checksum(b, bytes.size() - digest_size, b + bytes.size() - digest_size)) return fail(error, "Cannot checksum recovery journal");
  static std::atomic<unsigned> sequence{0};
  const auto name = ".journal-" + std::to_string(getpid()) + "-" + std::to_string(sequence++);
  Fd file{openat(directory_, name.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600)};
  if (file.value < 0) return fail(error, "Cannot create recovery journal");
  size_t written = 0;
  while (written < bytes.size()) {
    const auto n = write(file.value, b + written, bytes.size() - written);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) break;
    written += static_cast<size_t>(n);
  }
  if (written != bytes.size() || fsync(file.value) || renameat(directory_, name.c_str(), directory_, "journal")) {
    unlinkat(directory_, name.c_str(), 0); return fail(error, "Cannot persist recovery journal");
  }
  if (fsync(directory_)) return fail(error, "Recovery journal durability uncertain");
  return true;
}
bool WifiJournal::begin(const RecoveryRecord& record, std::string* error) {
  RecoveryRecord previous; bool exists;
  if (!load(&previous, &exists, error)) return false;
  if (exists && previous.phase == RecoveryPhase::Pending) return fail(error, "Unresolved recovery transaction exists");
  if (record.phase != RecoveryPhase::Pending) return fail(error, "New transaction must be pending");
  return save(record, error);
}
bool WifiJournal::commit(const std::string& transaction, const std::string& boot, uint64_t now, std::string* error) {
  RecoveryRecord record; bool exists;
  if (!load(&record, &exists, error)) return false;
  if (!exists || record.phase != RecoveryPhase::Pending || record.transaction != transaction ||
      record.boot_id != boot || now >= record.deadline_ms) return fail(error, "Recovery transaction cannot be committed");
  record.phase = RecoveryPhase::Committed;
  record.previous.resize(0); // Resolved journal must not retain old credentials.
  return save(record, error);
}
RecoveryOutcome WifiJournal::recover_due(const std::string& boot, uint64_t now,
    const std::function<bool(const RecoveryRecord&)>& restore, std::string* error) {
  if (!boot_valid(boot)) { *error = "Invalid current boot identity"; return RecoveryOutcome::Failed; }
  RecoveryRecord record; bool exists;
  if (!load(&record, &exists, error)) return RecoveryOutcome::Failed;
  if (!exists || record.phase != RecoveryPhase::Pending) return RecoveryOutcome::Nothing;
  if (record.boot_id == boot && now < record.deadline_ms) return RecoveryOutcome::Waiting;
  return restore_pending(record, restore, error);
}
RecoveryOutcome WifiJournal::cancel(const std::string& transaction, const std::string& boot,
    const std::function<bool(const RecoveryRecord&)>& restore, std::string* error) {
  if (!hex_string(transaction, 32) || !boot_valid(boot)) {
    *error = "Invalid cancellation identity"; return RecoveryOutcome::Failed;
  }
  RecoveryRecord record; bool exists;
  if (!load(&record, &exists, error)) return RecoveryOutcome::Failed;
  if (!exists || record.transaction != transaction || record.boot_id != boot || record.phase == RecoveryPhase::Committed) {
    *error = "Recovery transaction cannot be cancelled"; return RecoveryOutcome::Failed;
  }
  if (record.phase == RecoveryPhase::Restored) return RecoveryOutcome::Nothing;
  return restore_pending(record, restore, error);
}
RecoveryOutcome WifiJournal::restore_pending(RecoveryRecord& record,
    const std::function<bool(const RecoveryRecord&)>& restore, std::string* error) {
  bool restored = false;
  try { restored = restore(record); } catch (...) { restored = false; }
  if (!restored) { *error = "Recovery failed; pending journal retained"; return RecoveryOutcome::Failed; }
  record.phase = RecoveryPhase::Restored;
  record.previous.resize(0);
  return save(record, error) ? RecoveryOutcome::Restored : RecoveryOutcome::Failed;
}
bool recovery_clock(std::string* boot, uint64_t* milliseconds, std::string* error) {
  Fd file{::open("/proc/sys/kernel/random/boot_id", O_RDONLY | O_CLOEXEC)};
  char data[38]{};
  const auto n = file.value < 0 ? -1 : read(file.value, data, sizeof(data));
  if (n != 37 || data[36] != '\n') return fail(error, "Cannot read boot identity");
  boot->assign(data, 36);
  timespec time{};
  if (!boot_valid(*boot) || clock_gettime(CLOCK_BOOTTIME, &time) || time.tv_sec < 0) return fail(error, "Cannot read recovery clock");
  *milliseconds = static_cast<uint64_t>(time.tv_sec) * 1000 + static_cast<uint64_t>(time.tv_nsec) / 1000000;
  return true;
}
}
