#include "netplan_files.h"
#include "encoder/wifi_config_draft.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <cstdio>
namespace encoder {
namespace {
constexpr char filename[] = "90-encoder-wifi.yaml";
constexpr size_t maximum = 4096;
struct Fd { int value; ~Fd() { if (value >= 0) close(value); } };
bool fail(std::string* error, const char* text) { *error = text; return false; }
bool canonical(const SecureBuffer& bytes) {
  return bytes.size() && bytes.size() <= maximum &&
      read_wifi_config_draft({reinterpret_cast<const char*>(bytes.data()), bytes.size()}, nullptr).has_value();
}
}
std::unique_ptr<NetplanFiles> NetplanFiles::open(const std::string& directory, std::string* error) {
  Fd dir{::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)};
  struct stat info{};
  if (dir.value < 0 || fstat(dir.value, &info) || info.st_uid != geteuid() || (info.st_mode & 0022)) {
    *error = "Unsafe Netplan directory"; return nullptr;
  }
  auto result = std::unique_ptr<NetplanFiles>(new NetplanFiles(dir.value)); dir.value = -1; return result;
}
NetplanFiles::~NetplanFiles() { close(directory_); }
bool NetplanFiles::backup(bool* exists, SecureBuffer* contents, std::string* error) {
  *exists = false; contents->resize(0);
  Fd file{openat(directory_, filename, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK)};
  if (file.value < 0) return errno == ENOENT ? true : fail(error, "Cannot open managed Wi-Fi configuration");
  struct stat info{};
  if (fstat(file.value, &info) || !S_ISREG(info.st_mode) || info.st_uid != geteuid() ||
      (info.st_mode & 0077) || info.st_nlink != 1 || info.st_size <= 0 || info.st_size > static_cast<off_t>(maximum))
    return fail(error, "Unsafe managed Wi-Fi configuration");
  SecureBuffer data(static_cast<size_t>(info.st_size));
  size_t offset = 0;
  while (offset < data.size()) {
    const auto n = read(file.value, data.data()+offset, data.size()-offset);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) return fail(error, "Incomplete Wi-Fi configuration backup");
    offset += static_cast<size_t>(n);
  }
  char extra;
  if (read(file.value, &extra, 1) != 0 || !canonical(data)) return fail(error, "Refusing noncanonical Wi-Fi configuration");
  *contents = std::move(data); *exists = true; return true;
}
bool NetplanFiles::replace(const SecureBuffer& contents, std::string* error) {
  if (!canonical(contents)) return fail(error, "Unsupported managed Wi-Fi configuration format or size");
  bool exists; SecureBuffer previous;
  if (!backup(&exists, &previous, error)) return false;
  static std::atomic<unsigned> sequence{0};
  const auto temporary = ".encoder-wifi-" + std::to_string(getpid()) + "-" + std::to_string(sequence++);
  Fd file{openat(directory_, temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600)};
  if (file.value < 0) return fail(error, "Cannot create managed Wi-Fi temporary file");
  size_t offset = 0;
  while (offset < contents.size()) {
    const auto n = write(file.value, contents.data()+offset, contents.size()-offset);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) break;
    offset += static_cast<size_t>(n);
  }
  if (offset != contents.size() || fchmod(file.value, 0600) || fsync(file.value) || renameat(directory_, temporary.c_str(), directory_, filename)) {
    unlinkat(directory_, temporary.c_str(), 0); return fail(error, "Cannot persist managed Wi-Fi configuration");
  }
  return fsync(directory_) == 0 ? true : fail(error, "Wi-Fi configuration durability uncertain");
}
bool NetplanFiles::restore(bool existed, const SecureBuffer& contents, std::string* error) {
  if (existed) return replace(contents, error);
  if (contents.size()) return fail(error, "Unexpected backup for absent Wi-Fi configuration");
  bool current_exists; SecureBuffer current;
  if (!backup(&current_exists, &current, error)) return false;
  if (current_exists && unlinkat(directory_, filename, 0)) return fail(error, "Cannot remove test Wi-Fi configuration");
  return fsync(directory_) == 0 ? true : fail(error, "Wi-Fi restoration durability uncertain");
}
}
