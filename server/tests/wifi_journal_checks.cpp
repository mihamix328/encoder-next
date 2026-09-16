// Unprivileged temporary files only. No Netplan, service, or radio access.
#include "wifi_journal.h"
#include "wifi_recovery_worker.h"
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iostream>
using namespace encoder;
void check(bool value, const char* message) { if (!value) { std::cerr << message << '\n'; std::exit(1); } }
RecoveryRecord record() {
  RecoveryRecord r;
  r.transaction = std::string(32, 'a'); r.boot_id = "12345678-1234-1234-1234-123456789abc";
  r.deadline_ms = 1000; r.previous_exists = true; r.previous.resize(6);
  std::memcpy(r.previous.data(), "before", 6); return r;
}
int main() {
  char temporary[] = "/tmp/encoder-journal-XXXXXX";
  check(mkdtemp(temporary), "create private fixture");
  const std::string dir = temporary, path = dir + "/journal";
  std::string error;
  auto journal = WifiJournal::open(dir, &error);
  check(bool(journal), "open journal");
  check(!WifiJournal::open(dir, &error), "second writer excluded");
  auto r = record();
  check(journal->begin(r, &error), "durable begin");
  check(!journal->begin(r, &error), "pending backup never overwritten");
  struct stat info{}; stat(path.c_str(), &info);
  check((info.st_mode & 0777) == 0600, "backup is private");
  RecoveryRecord loaded; bool exists;
  check(journal->load(&loaded, &exists, &error) && exists && loaded.previous.size() == 6 &&
        !std::memcmp(loaded.previous.data(), "before", 6), "binary backup round trip");
  int restores = 0;
  auto restore = [&](const RecoveryRecord& old) { ++restores; return old.previous_exists && old.previous.size() == 6; };
  check(journal->recover_due(r.boot_id, 999, restore, &error) == RecoveryOutcome::Waiting && restores == 0, "not before deadline");
  check(!journal->commit(std::string(32, 'b'), r.boot_id, 999, &error), "wrong transaction cannot commit");
  check(!journal->commit(r.transaction, r.boot_id, 1000, &error), "commit loses deadline race");
  check(journal->recover_due(r.boot_id, 1000, [](const RecoveryRecord&) { return false; }, &error) == RecoveryOutcome::Failed, "restore failure surfaced");
  check(journal->load(&loaded, &exists, &error) && loaded.phase == RecoveryPhase::Pending, "failed restore keeps evidence");
  check(journal->recover_due(r.boot_id, 1000, restore, &error) == RecoveryOutcome::Restored && restores == 1, "deadline rollback");
  check(journal->recover_due(r.boot_id, 1000, restore, &error) == RecoveryOutcome::Nothing && restores == 1, "resolved rollback not repeated");
  check(journal->load(&loaded, &exists, &error) && loaded.previous.size() == 0, "restored journal drops old secrets");
  {
    auto bad = record(); bad.transaction = "../unsafe";
    check(!journal->begin(bad, &error), "invalid transaction ID rejected");
    bad = record(); bad.previous_exists = false;
    check(!journal->begin(bad, &error), "absent previous file cannot have backup bytes");
    bad = record(); bad.previous.resize(65537);
    check(!journal->begin(bad, &error), "oversized backup rejected");
    check(journal->load(&loaded, &exists, &error) && loaded.phase == RecoveryPhase::Restored, "bad input preserves journal");
  }
  check(journal->begin(r, &error) && journal->commit(r.transaction, r.boot_id, 999, &error), "confirmed transaction durable");
  check(journal->recover_due("aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa", 1, restore, &error) == RecoveryOutcome::Nothing, "committed survives reboot");
  check(journal->begin(r, &error), "next transaction allowed");
  check(journal->recover_due("aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa", 1, restore, &error) == RecoveryOutcome::Restored, "pending from previous boot rolls back immediately");
  {
    auto absent = record(); absent.previous_exists = false; absent.previous.resize(0);
    check(journal->begin(absent, &error), "absent previous config represented");
    check(journal->recover_due(absent.boot_id, 1000, [](const RecoveryRecord& old) {
      return !old.previous_exists && old.previous.size() == 0;
    }, &error) == RecoveryOutcome::Restored, "restore callback receives absence, not an empty existing file");
  }
  // A stale temporary file is not authoritative and must not replace the journal.
  { std::ofstream out(dir + "/.journal-stale"); out << "partial"; }
  check(journal->load(&loaded, &exists, &error) && loaded.phase == RecoveryPhase::Restored, "ignore incomplete temporary write");
  { std::fstream file(path, std::ios::in | std::ios::out | std::ios::binary); file.seekp(92); file.put('X'); }
  check(!journal->load(&loaded, &exists, &error), "checksum catches corrupt backup");
  check(journal->recover_due(r.boot_id, 2000, restore, &error) == RecoveryOutcome::Failed && restores == 2, "corruption never applied");
  unlink(path.c_str());
  { std::ofstream out(dir + "/victim"); out << "unchanged"; }
  symlink((dir + "/victim").c_str(), path.c_str());
  check(!journal->begin(r, &error), "symlink journal refused");
  unlink(path.c_str()); check(journal->begin(r, &error), "recreate fixture");
  link(path.c_str(), (dir + "/alias").c_str());
  check(!journal->load(&loaded, &exists, &error), "hardlinked journal refused");
  unlink((dir + "/alias").c_str());
  chmod(path.c_str(), 0644);
  check(!journal->load(&loaded, &exists, &error), "public backup refused");
  chmod(path.c_str(), 0600);
  unlink(path.c_str()); journal.reset();
  // Simulate coordinator death after durable prepare. A separate process (parent)
  // acquires the released OS lock and restores the fixture from disk.
  const auto child = fork();
  check(child >= 0, "fork crash fixture");
  if (child == 0) {
    auto owner = WifiJournal::open(dir, &error);
    if (!owner || !owner->begin(r, &error)) _exit(2);
    raise(SIGKILL); _exit(3);
  }
  int status = 0;
  check(waitpid(child, &status, 0) == child && WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL, "owner died without destructors");
  journal = WifiJournal::open(dir, &error);
  check(bool(journal), "OS lock released after owner death");
  check(journal->recover_due(r.boot_id, 1000, [&](const RecoveryRecord& old) {
    std::ofstream out(dir + "/restored", std::ios::binary);
    out.write(reinterpret_cast<const char*>(old.previous.data()), old.previous.size()); return bool(out);
  }, &error) == RecoveryOutcome::Restored, "independent recovery after SIGKILL");
  { std::ifstream in(dir + "/restored"); std::string text; in >> text; check(text == "before", "original fixture restored"); }
  std::string boot; uint64_t ms;
  check(recovery_clock(&boot, &ms, &error) && boot.size() == 36, "Linux boot identity and suspend-aware clock");
  r.boot_id = boot; r.deadline_ms = ms + 250;
  check(journal->begin(r, &error), "prepare timed recovery fixture");
  journal.reset();
  const auto watcher = fork();
  check(watcher >= 0, "fork independent supervisor");
  if (watcher == 0) {
    for (int attempt = 0; attempt < 300; ++attempt) {
      auto outcome = wifi_recovery_tick(dir, [&](const RecoveryRecord& old) {
        std::ofstream out(dir + "/watchdog-restored", std::ios::binary);
        out.write(reinterpret_cast<const char*>(old.previous.data()), old.previous.size()); return bool(out);
      }, &error);
      if (outcome == RecoveryOutcome::Restored) _exit(0);
      if (outcome == RecoveryOutcome::Failed || outcome == RecoveryOutcome::Nothing) _exit(2);
      usleep(10000);
    }
    _exit(3);
  }
  // The coordinator does nothing: only the separate worker watches the deadline.
  check(waitpid(watcher, &status, 0) == watcher && WIFEXITED(status) && WEXITSTATUS(status) == 0,
        "independent worker recovers without coordinator polling");
  { std::ifstream in(dir + "/watchdog-restored"); std::string text; in >> text; check(text == "before", "watchdog restored original fixture"); }
  chmod(dir.c_str(), 0750); check(!WifiJournal::open(dir, &error), "shared directory rejected"); chmod(dir.c_str(), 0700);
  for (const char* file : {"journal", "lock", ".journal-stale", "victim", "restored", "watchdog-restored"}) unlink((dir + "/" + file).c_str());
  check(rmdir(dir.c_str()) == 0, "all fixture files removed");
  std::cout << "Recovery journal passed: private atomic state, locking, checksum, deadlines, reboot, failed recovery and SIGKILL\n";
}
