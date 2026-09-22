// Private fixtures only: never opens /etc/netplan or executes network commands.
#include "netplan_files.h"
#include "wifi_recovery_worker.h"
#include <sys/stat.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
using namespace encoder;
void check(bool value, const char* message) { if (!value) { std::cerr << message << '\n'; std::exit(1); } }
SecureBuffer bytes(const std::string& text) { SecureBuffer b(text.size()); std::memcpy(b.data(), text.data(), text.size()); return b; }
int main() {
  char temporary[] = "/tmp/encoder-netplan-files-XXXXXX";
  check(mkdtemp(temporary), "private fixture");
  const std::string dir = temporary, managed = dir + "/90-encoder-wifi.yaml", ethernet = dir + "/10-ethernet.yaml";
  { std::ofstream out(ethernet); out << "unchanged Ethernet"; }
  std::string error;
  auto files = NetplanFiles::open(dir, &error); check(bool(files), "open managed directory");
  bool exists; SecureBuffer backup;
  check(files->backup(&exists, &backup, &error) && !exists, "original absence recorded");
  auto first = bytes("# encoder managed wifi v1\nnetwork: {version: 2}\n");
  auto second = bytes("# encoder managed wifi v1\nnetwork: {version: 2, renderer: networkd}\n");
  check(files->replace(first, &error), "write managed fixture");
  struct stat s{}; stat(managed.c_str(), &s); check((s.st_mode & 0777) == 0600, "secret file mode");
  check(files->backup(&exists, &backup, &error) && exists, "capture original file");
  check(files->replace(second, &error) && files->restore(true, backup, &error), "replace then restore");
  SecureBuffer restored;
  check(files->backup(&exists, &restored, &error) && restored.size() == first.size() &&
        !std::memcmp(restored.data(), first.data(), first.size()), "restore exact original bytes");
  SecureBuffer empty;
  check(files->restore(false, empty, &error) && files->restore(false, empty, &error), "absent restoration idempotent");
  { std::ofstream out(managed); out << "foreign configuration"; } chmod(managed.c_str(), 0600);
  check(!files->replace(first, &error) && !files->restore(false, empty, &error), "foreign content never overwritten or deleted");
  unlink(managed.c_str()); symlink(ethernet.c_str(), managed.c_str());
  check(!files->replace(first, &error) && !files->restore(false, empty, &error), "symlink rejected");
  unlink(managed.c_str()); check(files->replace(first, &error), "reset fixture");
  link(managed.c_str(), (dir + "/alias").c_str());
  check(!files->replace(second, &error), "hardlink rejected"); unlink((dir + "/alias").c_str());
  chmod(managed.c_str(), 0644); check(!files->backup(&exists, &backup, &error), "public secret file rejected");
  chmod(managed.c_str(), 0600);
  auto too_big = bytes("# encoder managed wifi v1\n" + std::string(65536, 'x'));
  check(!files->replace(too_big, &error), "size cap");
  const auto state_dir = dir + "/recovery";
  check(!mkdir(state_dir.c_str(), 0700), "recovery fixture directory");
  RecoveryRecord record; record.transaction = std::string(32, 'a'); record.previous_exists = true;
  record.previous = bytes("# encoder managed wifi v1\nnetwork: {version: 2}\n");
  uint64_t now;
  check(recovery_clock(&record.boot_id, &now, &error), "recovery clock");
  record.deadline_ms = now > 1 ? now - 1 : 1;
  {
    auto journal = WifiJournal::open(state_dir, &error);
    check(journal && journal->begin(record, &error), "pending recovery written");
  }
  check(files->replace(second, &error), "new configuration fixture");
  int applied = 0;
  check(wifi_managed_recovery_tick(state_dir, dir, [&]() { ++applied; return false; }, &error) == RecoveryOutcome::Failed,
        "failed reconfigure is not reported as restored");
  check(wifi_managed_recovery_tick(state_dir, dir, [&]() { ++applied; return true; }, &error) == RecoveryOutcome::Restored && applied == 2,
        "recovery retries application after restoring files");
  check(wifi_managed_recovery_tick(state_dir, dir, [&]() { ++applied; return true; }, &error) == RecoveryOutcome::Nothing && applied == 2,
        "resolved recovery does not reapply configuration");
  record.deadline_ms = now + 90000;
  {
    auto journal = WifiJournal::open(state_dir, &error);
    check(journal && journal->begin(record, &error), "pending cancellation fixture");
  }
  check(files->replace(second, &error), "cancellation candidate file");
  check(wifi_managed_cancel(state_dir, dir, std::string(32, 'b'), [&]() { ++applied; return true; }, &error) == RecoveryOutcome::Failed && applied == 2,
        "wrong cancellation identity does not apply anything");
  check(files->backup(&exists, &restored, &error) && restored.size() == second.size() &&
        !std::memcmp(restored.data(), second.data(), second.size()), "wrong cancellation leaves candidate unchanged");
  check(wifi_managed_cancel(state_dir, dir, record.transaction, [&]() { ++applied; return false; }, &error) == RecoveryOutcome::Failed,
        "cancellation apply failure retains recovery");
  check(wifi_managed_cancel(state_dir, dir, record.transaction, [&]() { ++applied; return true; }, &error) == RecoveryOutcome::Restored && applied == 4,
        "explicit cancellation restores files and retries apply");
  check(files->backup(&exists, &restored, &error) && restored.size() == first.size() &&
        !std::memcmp(restored.data(), first.data(), first.size()), "cancellation restores original bytes");
  check(wifi_managed_cancel(state_dir, dir, record.transaction, [&]() { ++applied; return true; }, &error) == RecoveryOutcome::Nothing && applied == 4,
        "duplicate file cancellation is idempotent");
  unlink((state_dir + "/journal").c_str()); unlink((state_dir + "/lock").c_str());
  check(!rmdir(state_dir.c_str()), "recovery fixture removed");
  { std::ifstream in(ethernet); std::string text; std::getline(in, text); check(text == "unchanged Ethernet", "other files untouched"); }
  files.reset(); chmod(dir.c_str(), 0777); check(!NetplanFiles::open(dir, &error), "shared writable directory rejected");
  chmod(dir.c_str(), 0700); unlink(managed.c_str()); unlink(ethernet.c_str()); check(!rmdir(dir.c_str()), "fixture removed");
  std::cout << "Netplan file layer passed: backup, atomic replacement, restoration, absent file, permissions and isolation\n";
}
