#include "wifi_managed_backend.h"
#include "netplan_files.h"
#include "wifi_recovery_worker.h"
#include "encoder/wifi_config_draft.h"
#include <openssl/crypto.h>
#include <openssl/rand.h>
#include <utility>
namespace encoder {
namespace {
bool equal(const SecureBuffer& a, const SecureBuffer& b) {
  return a.size() == b.size() && (!a.size() || !CRYPTO_memcmp(a.data(), b.data(), a.size()));
}
}
WifiManagedBackend::WifiManagedBackend(std::string state, std::string netplan, WifiManagedPlatform& platform)
    : state_directory_(std::move(state)), netplan_directory_(std::move(netplan)), platform_(platform) {
  error_.reserve(512); transaction_.reserve(32);
}
bool WifiManagedBackend::fail(const char* message) noexcept { error_ = message; return false; }
void WifiManagedBackend::forget_target() noexcept { target_.reset(); candidate_ = SecureBuffer{}; }
bool WifiManagedBackend::ethernet_recovery_available() noexcept { return platform_.ethernet_ready(); }
bool WifiManagedBackend::current(const RecoveryRecord& record) noexcept {
  try {
    std::string boot; uint64_t now;
    return recovery_clock(&boot, &now, &error_) && record.phase == RecoveryPhase::Pending &&
      record.transaction == transaction_ && record.boot_id == boot && now < record.deadline_ms;
  } catch (...) { return fail("Cannot verify pending Wi-Fi transaction"); }
}
bool WifiManagedBackend::prepare(const WifiProfile& profile, std::chrono::seconds lifetime) noexcept {
  try {
    if (attempted_) return fail("Wi-Fi backend is single use");
    attempted_ = true;
    if (lifetime.count() <= 0 || lifetime.count() > 90) return fail("Invalid recovery lifetime");
    auto draft = make_wifi_config_draft(profile, &error_);
    if (!draft) return false;
    candidate_ = std::move(draft->netplan_yaml);
    target_ = read_wifi_config_draft(std::string_view(reinterpret_cast<const char*>(candidate_.data()), candidate_.size()), &error_);
    if (!target_) return false;
    if (!platform_.ethernet_ready() || !platform_.preflight(*target_) || !platform_.watchdog_ready(state_directory_))
      return fail("Wi-Fi preparation safety checks failed");
    auto journal = WifiJournal::open(state_directory_, &error_);
    if (!journal) return false;
    RecoveryRecord record; bool exists;
    if (!journal->load(&record, &exists, &error_)) return false;
    // Do not claim ownership or cancel somebody else's pending operation.
    if (exists && record.phase == RecoveryPhase::Pending) return fail("Another Wi-Fi transaction is pending");
    record = RecoveryRecord{};
    auto files = NetplanFiles::open(netplan_directory_, &error_);
    if (!files || !files->backup(&record.previous_exists, &record.previous, &error_)) return false;
    uint64_t now;
    if (!recovery_clock(&record.boot_id, &now, &error_)) return false;
    record.deadline_ms = now + static_cast<uint64_t>(lifetime.count()) * 1000;
    SecureBuffer random(16);
    if (RAND_bytes(random.data(), 16) != 1) return fail("Cannot create Wi-Fi transaction identity");
    constexpr char hex[] = "0123456789abcdef";
    record.transaction.resize(32);
    for (size_t i = 0; i < 16; ++i) {
      record.transaction[2*i] = hex[random.data()[i] >> 4];
      record.transaction[2*i+1] = hex[random.data()[i] & 15];
    }
    // Save ownership before begin: an I/O error may happen AFTER durable rename.
    transaction_ = record.transaction;
    if (!journal->begin(record, &error_)) return false;
    if (!platform_.watchdog_ready(state_directory_) || !current(record))
      return fail("Recovery supervisor or transaction is not ready");
    prepared_ = true;
    return true;
  } catch (...) { return fail("Wi-Fi preparation failed"); }
}
bool WifiManagedBackend::activate() noexcept {
  try {
    if (!prepared_ || activated_ || committed_ || restored_ || !target_) return fail("Wi-Fi activation is not prepared");
    auto journal = WifiJournal::open(state_directory_, &error_);
    RecoveryRecord record; bool exists;
    if (!journal || !journal->load(&record, &exists, &error_) || !exists || !current(record)) return false;
    if (!platform_.ethernet_ready() || !platform_.preflight(*target_) || !platform_.watchdog_ready(state_directory_))
      return fail("Wi-Fi activation safety checks failed");
    auto files = NetplanFiles::open(netplan_directory_, &error_);
    SecureBuffer before;
    if (!files || !files->backup(&exists, &before, &error_)) return false;
    if (exists != record.previous_exists || !equal(before, record.previous)) return fail("Wi-Fi configuration changed after backup");
    if (!current(record) || !files->replace(candidate_, &error_)) return false;
    if (!platform_.apply_target(*target_)) return fail("Wi-Fi target application failed");
    SecureBuffer after;
    if (!files->backup(&exists, &after, &error_) || !exists || !equal(after, candidate_) || !current(record))
      return fail("Wi-Fi activation did not preserve the pending candidate");
    activated_ = true;
    return true;
  } catch (...) { return fail("Wi-Fi activation failed"); }
}
WifiLink WifiManagedBackend::probe() noexcept {
  if (!activated_ || committed_ || restored_ || !target_ || !platform_.watchdog_ready(state_directory_)) return WifiLink::Failed;
  return platform_.probe_target(*target_);
}
bool WifiManagedBackend::commit() noexcept {
  try {
    if (!activated_ || committed_ || restored_ || !target_) return fail("Wi-Fi confirmation is not pending");
    if (!wifi_managed_commit(state_directory_, netplan_directory_, transaction_, *target_, [&] {
      return platform_.ethernet_ready() && platform_.preflight(*target_) &&
        platform_.watchdog_ready(state_directory_) && platform_.probe_target(*target_) == WifiLink::Ready;
    }, &error_)) return false;
    committed_ = true; forget_target(); return true;
  } catch (...) { return fail("Wi-Fi confirmation failed"); }
}
bool WifiManagedBackend::rollback() noexcept {
  try {
    if (committed_) return fail("Committed Wi-Fi transaction cannot be rolled back");
    if (transaction_.empty() || restored_) { forget_target(); return true; }
    const auto outcome = wifi_managed_cancel(state_directory_, netplan_directory_, transaction_,
      [&] { return platform_.restore_network(); }, &error_);
    if (outcome != RecoveryOutcome::Restored && outcome != RecoveryOutcome::Nothing) return false;
    restored_ = true; forget_target(); return true;
  } catch (...) { return fail("Wi-Fi rollback failed; recovery remains required"); }
}
}
