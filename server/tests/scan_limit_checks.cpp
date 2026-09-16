#include "scan_limit.h"
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/stat.h>

int main() {
  char temporary[] = "/tmp/encoder-scan-limit-XXXXXX";
  if (!mkdtemp(temporary)) return 1;
  const auto directory = std::filesystem::path(temporary);
  const auto state = directory / "scan-limit";
  std::string error;
  bool ok = encoder::reserve_scan_slot(directory.string(), &error);
  ok = !encoder::reserve_scan_slot(directory.string(), &error) && error.find("cooldown") != std::string::npos && ok;
  { std::ofstream file(state); file << std::time(nullptr) - 31; }
  ok = encoder::reserve_scan_slot(directory.string(), &error) && ok;
  { std::ofstream file(state); file << std::time(nullptr) + 3600; }
  ok = !encoder::reserve_scan_slot(directory.string(), &error) && ok;
  { std::ofstream file(state); file << "corrupted"; }
  ok = !encoder::reserve_scan_slot(directory.string(), &error) && ok;
  chmod(state.c_str(), 0666);
  ok = !encoder::reserve_scan_slot(directory.string(), &error) && ok;
  std::filesystem::remove(state);
  const auto victim = directory / "victim";
  { std::ofstream file(victim); file << "unchanged"; }
  std::filesystem::create_symlink(victim, state);
  ok = !encoder::reserve_scan_slot(directory.string(), &error) && ok;
  { std::ifstream file(victim); std::string value; file >> value; ok = value == "unchanged" && ok; }
  std::filesystem::remove(state);
  std::filesystem::remove(victim);
  std::filesystem::remove(directory);
  if (!ok) { std::cerr << "Scan cooldown checks failed\n"; return 1; }
  std::cout << "Scan cooldown passed: persistence, expiry, future time, malformed state, permissions and symlinks\n";
}
