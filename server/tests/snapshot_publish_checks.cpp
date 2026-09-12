#include "snapshot_publish.h"
#include "network_status.h"
#include <sys/stat.h>
#include <unistd.h>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>

int main() {
  char temporary[] = "/tmp/encoder-publish-test-XXXXXX";
  if (!mkdtemp(temporary)) return 1;
  const auto directory = std::filesystem::path(temporary);
  const auto target = directory / "wifi.txt";
  const auto victim = directory / "untouched.txt";
  const auto alias = directory / "alias";
  const std::string heading = "bssid / frequency / signal level / flags / ssid\n";
  const std::string data = "encoder-wifi-v1 " + std::to_string(std::time(nullptr)) + "\n" + heading;
  std::string result, error;
  const auto original_mask = umask(0077);
  bool ok = encoder::publish_wifi_snapshot(directory.string(), data, &error);
  struct stat info{};
  ok = lstat(target.c_str(), &info) == 0 && S_ISREG(info.st_mode) &&
       (info.st_mode & 0777) == 0640 && info.st_uid == geteuid() && ok;
  ok = encoder::wifi_snapshot(target.string(), &result, &error) && result == heading && ok;
  const auto inode = info.st_ino;
  ok = encoder::publish_wifi_snapshot(directory.string(), data, &error) && ok;
  ok = lstat(target.c_str(), &info) == 0 && inode != info.st_ino && ok;
  ok = !encoder::publish_wifi_snapshot(directory.string(), std::string(65536, 'x'), &error) && ok;
  ok = encoder::wifi_snapshot(target.string(), &result, &error) && result == heading && ok;
  { std::ofstream file(victim); file << "unchanged"; }
  std::filesystem::remove(target);
  std::filesystem::create_symlink(victim, target);
  ok = encoder::publish_wifi_snapshot(directory.string(), data, &error) && ok;
  { std::ifstream file(victim); std::getline(file, result); ok = result == "unchanged" && ok; }
  std::filesystem::create_directory_symlink(directory, alias);
  ok = !encoder::publish_wifi_snapshot(alias.string(), data, &error) && ok;
  chmod(directory.c_str(), 0770);
  ok = !encoder::publish_wifi_snapshot(directory.string(), data, &error) && ok;
  chmod(directory.c_str(), 0700);
  size_t files = 0;
  for (const auto& entry : std::filesystem::directory_iterator(directory)) { (void)entry; ++files; }
  ok = files == 3 && ok; // No leftover temporary files.
  std::filesystem::remove(alias);
  std::filesystem::remove(victim);
  std::filesystem::remove(target);
  std::filesystem::remove(directory);
  umask(original_mask);
  if (!ok) { std::cerr << "Snapshot publication checks failed\n"; return 1; }
  std::cout << "Snapshot publication passed: atomic replace, mode 0640, size limit, symlinks and directory permissions\n";
}
