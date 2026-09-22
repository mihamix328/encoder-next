#pragma once
#include "encoder/secure_memory.h"
#include <memory>
#include <string>
namespace encoder {
// Only the encoder-owned Wi-Fi file is managed. Other Netplan files, including
// Ethernet configuration, are never written by this component. No shell calls.
class NetplanFiles {
 public:
  static std::unique_ptr<NetplanFiles> open(const std::string& directory, std::string* error);
  ~NetplanFiles();
  NetplanFiles(const NetplanFiles&) = delete;
  NetplanFiles& operator=(const NetplanFiles&) = delete;
  bool backup(bool* exists, SecureBuffer* contents, std::string* error);
  bool replace(const SecureBuffer& contents, std::string* error);
  bool restore(bool existed, const SecureBuffer& contents, std::string* error);
 private:
  explicit NetplanFiles(int directory) : directory_(directory) {}
  int directory_;
};
}
