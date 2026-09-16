#include "scan_limit.h"
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <charconv>
#include <ctime>

namespace encoder {
bool reserve_scan_slot(const std::string& directory, std::string* error) {
  struct Fd { int value; ~Fd() { if (value >= 0) close(value); } };
  Fd dir{open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)};
  struct stat info{};
  if (dir.value < 0 || fstat(dir.value, &info) || info.st_uid != geteuid() || (info.st_mode & 0022)) {
    *error = "Unsafe or missing scan state directory"; return false;
  }
  Fd file{openat(dir.value, "scan-limit", O_RDWR | O_CREAT | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC, 0600)};
  if (file.value < 0 || fstat(file.value, &info) || !S_ISREG(info.st_mode) ||
      info.st_uid != geteuid() || (info.st_mode & 0077) || info.st_nlink != 1 ||
      flock(file.value, LOCK_EX | LOCK_NB)) {
    *error = "Scan state is unsafe or busy"; return false;
  }
  char buffer[64];
  const auto count = read(file.value, buffer, sizeof(buffer));
  const auto now = static_cast<long long>(std::time(nullptr));
  if (count < 0 || count == static_cast<ssize_t>(sizeof(buffer)) || now <= 0) {
    *error = "Cannot read scan state"; return false;
  }
  if (count) {
    long long last = 0;
    const auto parsed = std::from_chars(buffer, buffer + count, last);
    if (parsed.ec != std::errc{} || parsed.ptr != buffer + count || last <= 0) {
      *error = "Invalid scan state"; return false;
    }
    if (last > now || now - last < 30) {
      *error = "Scan cooldown: wait at least 30 seconds between attempts"; return false;
    }
  }
  const auto stamp = std::to_string(now);
  if (pwrite(file.value, stamp.data(), stamp.size(), 0) != static_cast<ssize_t>(stamp.size()) ||
      ftruncate(file.value, stamp.size()) || fsync(file.value)) {
    *error = "Cannot persist scan cooldown"; return false;
  }
  return true;
}
}
