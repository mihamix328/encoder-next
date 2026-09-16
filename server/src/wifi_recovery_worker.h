#pragma once
#include "wifi_journal.h"
namespace encoder {
// One independent supervisor iteration. No network/config paths or shell commands
// are built here. The future privileged service supplies a fixed idempotent adapter.
// Errors and lock contention are retried by the supervisor, never treated as success.
RecoveryOutcome wifi_recovery_tick(const std::string& directory,
    const std::function<bool(const RecoveryRecord&)>& restore, std::string* error);
}
