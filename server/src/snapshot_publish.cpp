#include "snapshot_publish.h"
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace encoder {
bool publish_wifi_snapshot(const std::string& directory, const std::string& data,
                           std::string* error) {
  if (data.empty() || data.size() >= 65536) {
    *error = "Invalid snapshot size"; return false;
  }
  struct Descriptor {
    int value;
    ~Descriptor() { if (value >= 0) close(value); }
  } dir{open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)};
  struct stat info{};
  if (dir.value < 0 || fstat(dir.value, &info) != 0 || info.st_uid != geteuid() ||
      (info.st_mode & (S_IWGRP | S_IWOTH))) {
    *error = "Snapshot directory must be owned by collector and not writable by others";
    return false;
  }
  static std::atomic<unsigned long> sequence{0};
  std::string temporary;
  Descriptor file{-1};
  for (int attempt = 0; attempt < 16; ++attempt) {
    temporary = ".wifi-" + std::to_string(getpid()) + "-" + std::to_string(sequence++);
    file.value = openat(dir.value, temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (file.value >= 0 || errno != EEXIST) break;
  }
  if (file.value < 0) { *error = "Cannot create snapshot temporary file"; return false; }
  size_t written = 0;
  while (written < data.size()) {
    const auto count = write(file.value, data.data() + written, data.size() - written);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) break;
    written += static_cast<size_t>(count);
  }
  const bool complete = written == data.size() && fchmod(file.value, 0640) == 0 && fsync(file.value) == 0;
  const bool closed = close(file.value) == 0;
  file.value = -1;
  if (!complete || !closed || renameat(dir.value, temporary.c_str(), dir.value, "wifi.txt") != 0) {
    unlinkat(dir.value, temporary.c_str(), 0);
    *error = "Cannot publish complete snapshot";
    return false;
  }
  return true;
}
}
