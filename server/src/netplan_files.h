#pragma once
#include "encoder/secure_memory.h"
#include <memory>
#include <string>
namespace encoder {
// Only the encoder-owned Wi-Fi file is managed. Other Netplan files, including
// Ethernet configuration, are never written by this component. No shell calls.
// Current/candidate/backup bytes must match our exact generated draft format.
// A marker alone is not ownership or scope validation. This still does not
// authorize activation or resolve conflicts in OTHER Netplan files.
class NetplanFiles {
 public:
  static std::unique_ptr<NetplanFiles> open(const std::string& directory, std::string* error);
  // Separate fixed runtime file; private directory required. Never accepts YAML
  // or an arbitrary supplicant configuration as a managed runtime policy.
  static std::unique_ptr<NetplanFiles> open_supplicant(const std::string& directory, std::string* error);
  ~NetplanFiles();
  NetplanFiles(const NetplanFiles&) = delete;
  NetplanFiles& operator=(const NetplanFiles&) = delete;
  bool backup(bool* exists, SecureBuffer* contents, std::string* error);
  bool replace(const SecureBuffer& contents, std::string* error);
  bool restore(bool existed, const SecureBuffer& contents, std::string* error);
 private:
  explicit NetplanFiles(int directory, bool supplicant = false) : directory_(directory), supplicant_(supplicant) {}
  const char* filename() const;
  bool canonical(const SecureBuffer&) const;
  int directory_;
  bool supplicant_ = false;
};
}
