#include "encoder/auth.h"

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/crypto.h>
#include <algorithm>
#include <filesystem>
#if defined(_WIN32)
#include <windows.h>
#endif

#include <fstream>
#include <sstream>
#include <vector>

namespace encoder {

namespace {

std::string bytes_to_hex(const unsigned char* data, size_t len) {
  static const char* kHex = "0123456789abcdef";
  std::string out;
  out.reserve(len * 2);
  for (size_t i = 0; i < len; ++i) {
    out.push_back(kHex[(data[i] >> 4) & 0xF]);
    out.push_back(kHex[data[i] & 0xF]);
  }
  return out;
}

std::vector<unsigned char> hex_to_bytes(const std::string& hex) {
  std::vector<unsigned char> out;
  if (hex.size() % 2 != 0) return out;
  out.reserve(hex.size() / 2);
  for (size_t i = 0; i < hex.size(); i += 2) {
    unsigned char hi = static_cast<unsigned char>(std::stoi(hex.substr(i, 1), nullptr, 16));
    unsigned char lo = static_cast<unsigned char>(std::stoi(hex.substr(i + 1, 1), nullptr, 16));
    out.push_back((hi << 4) | lo);
  }
  return out;
}

} // namespace

bool UserStore::load(const std::string& path) {
  users_.clear();
  std::ifstream file(path);
  if (!file) return false;
  std::string line;
  while (std::getline(file, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream ss(line);
    std::string username, salt, hash;
    if (!std::getline(ss, username, ':')) continue;
    if (!std::getline(ss, salt, ':')) continue;
    if (!std::getline(ss, hash, ':')) continue;
    UserRecord rec{username, salt, hash};
    std::string blocked;
    if (std::getline(ss, blocked, ':')) {
      if (!blocked.empty() && blocked.back() == '\r') blocked.pop_back();
      rec.blocked = blocked != "0";
    }
    std::string permissions;
    if (std::getline(ss, permissions, ':')) {
      if (!permissions.empty() && permissions.back() == '\r') permissions.pop_back();
      rec.permissions = permissions.size() == 1 && permissions[0] >= '0' && permissions[0] <= '3'
          ? static_cast<unsigned>(permissions[0] - '0') : 0;
    }
    users_[username] = rec;
  }
  return true;
}

bool UserStore::save(const std::string& path) const {
  const std::string temporary = path + ".tmp";
  std::ofstream file(temporary, std::ios::trunc);
  if (!file) return false;
  for (const auto& kv : users_) {
    file << kv.second.username << ':' << kv.second.salt_hex << ':' << kv.second.hash_hex
         << ':' << (kv.second.blocked ? "1" : "0") << ':' << kv.second.permissions << '\n';
  }
  file.close();
  if (!file) return false;
#if defined(_WIN32)
  return MoveFileExW(std::filesystem::path(temporary).c_str(), std::filesystem::path(path).c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
  std::error_code error;
  std::filesystem::rename(temporary, path, error);
  return !error;
#endif
}

bool UserStore::verify(const std::string& username, const std::string& password) const {
  auto it = users_.find(username);
  if (it == users_.end() || it->second.blocked) return false;
  try {
    std::string computed = pbkdf2_hash(password, it->second.salt_hex, 120000);
    return !computed.empty() && computed.size() == it->second.hash_hex.size()
        && CRYPTO_memcmp(computed.data(), it->second.hash_hex.data(), computed.size()) == 0;
  } catch (...) { return false; }
}

bool UserStore::upsert(const std::string& username, const std::string& password) {
  if ((!exists(username) && !valid_username(username)) || password.empty() || password.size() > 1024) return false;
  UserRecord rec;
  if (exists(username)) rec.blocked = users_.at(username).blocked;
  if (exists(username)) rec.permissions = users_.at(username).permissions;
  rec.username = username;
  rec.salt_hex = random_salt_hex(16);
  if (rec.salt_hex.empty()) return false;
  rec.hash_hex = pbkdf2_hash(password, rec.salt_hex, 120000);
  if (rec.salt_hex.empty() || rec.hash_hex.empty()) return false;
  users_[username] = rec;
  return true;
}

std::string UserStore::pbkdf2_hash(const std::string& password,
                                  const std::string& salt_hex,
                                  int iterations) {
  std::vector<unsigned char> salt = hex_to_bytes(salt_hex);
  unsigned char out[32];
  if (PKCS5_PBKDF2_HMAC(password.c_str(), static_cast<int>(password.size()),
                        salt.data(), static_cast<int>(salt.size()),
                        iterations, EVP_sha256(), sizeof(out), out) != 1) {
    return {};
  }
  return bytes_to_hex(out, sizeof(out));
}

std::string UserStore::random_salt_hex(size_t bytes) {
  std::vector<unsigned char> salt(bytes);
  if (RAND_bytes(salt.data(), static_cast<int>(salt.size())) != 1) return {};
  return bytes_to_hex(salt.data(), salt.size());
}

bool UserStore::valid_username(const std::string& username) {
  return !username.empty() && username.size() <= 64 && std::all_of(username.begin(), username.end(), [](unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
  });
}
bool UserStore::exists(const std::string& username) const { return users_.count(username) != 0; }
unsigned UserStore::permissions(const std::string& username) const {
  const auto it = users_.find(username);
  return it == users_.end() ? 0 : it->second.permissions;
}
bool UserStore::set_permissions(const std::string& username, unsigned permissions) {
  const auto it = users_.find(username);
  if (it == users_.end() || permissions > 3) return false;
  it->second.permissions = permissions;
  return true;
}
bool UserStore::set_blocked(const std::string& username, bool blocked) {
  auto it = users_.find(username);
  if (it == users_.end()) return false;
  it->second.blocked = blocked;
  return true;
}
std::vector<std::pair<std::string, bool>> UserStore::list() const {
  std::vector<std::pair<std::string, bool>> result;
  for (const auto& item : users_) result.emplace_back(item.first, item.second.blocked);
  std::sort(result.begin(), result.end());
  return result;
}
} // namespace encoder
