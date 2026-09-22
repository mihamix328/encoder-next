#pragma once
#include "wifi_journal.h"
namespace encoder {
// One independent supervisor iteration. No network/config paths or shell commands
// are built here. The future privileged service supplies a fixed idempotent adapter.
// Errors and lock contention are retried by the supervisor, never treated as success.
RecoveryOutcome wifi_recovery_tick(const std::string& directory,
    const std::function<bool(const RecoveryRecord&)>& restore, std::string* error);
// File restoration is not success until the fixed network adapter re-applies it.
// Caller supplies trusted directories and a bounded idempotent reconfigure action.
RecoveryOutcome wifi_managed_recovery_tick(const std::string& state_directory,
    const std::string& netplan_directory, const std::function<bool()>& reconfigure, std::string* error);
}
