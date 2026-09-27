#include "encoder/gost_native.h"
#include "encoder/secure_memory.h"
extern "C" {
#include <libakrypt.h>
}
#include <openssl/crypto.h>
#include <openssl/rand.h>
#include <algorithm>
#include <cstring>
#include <mutex>
namespace encoder {
namespace {
std::mutex library_mutex;
constexpr size_t header_size = 18;
constexpr size_t max_size = 100 * 1024 * 1024;
constexpr unsigned char magic[8] = {'E','N','G','O','S','T','0','2'};
bool init() {
  static const bool initialized = ak_libakrypt_create(nullptr) == ak_true &&
      ak_libakrypt_test_magma() && ak_libakrypt_test_kuznechik() && ak_libakrypt_test_mgm();
  return initialized;
}
struct Context {
  struct bckey key{};
  bool created = false;
  ~Context() { if (created) ak_bckey_destroy(&key); }
  bool setup(Cipher cipher, const uint8_t* value) {
    created = (cipher == Cipher::MAGMA ? ak_bckey_create_magma(&key) :
        ak_bckey_create_kuznechik(&key)) == ak_error_ok;
    return created && ak_bckey_set_key(&key, const_cast<uint8_t*>(value), 32) == ak_error_ok;
  }
};
bool fail(std::string* error) { if (error) *error = "GOST-MGM operation failed or data authentication rejected"; return false; }
unsigned id(Cipher cipher) { return cipher == Cipher::MAGMA ? 1 : cipher == Cipher::KUZNECHIK ? 2 : 0; }
}
bool gost_native_encrypt(Cipher cipher, const std::vector<uint8_t>& plain, CryptoResult* out, std::string* error) {
  if (!out || !id(cipher) || plain.size() > max_size) return fail(error);
  std::lock_guard<std::mutex> lock(library_mutex);
  if (!init()) return fail(error);
  SecureBuffer key(32);
  const size_t block = cipher == Cipher::MAGMA ? 8 : 16;
  std::vector<uint8_t> frame(header_size + block + plain.size() + block);
  std::copy(std::begin(magic), std::end(magic), frame.begin());
  frame[8] = static_cast<uint8_t>(id(cipher)); frame[9] = static_cast<uint8_t>(block);
  for (size_t i = 0; i < 8; ++i) frame[10+i] = static_cast<uint8_t>(static_cast<uint64_t>(plain.size()) >> (8*i));
  auto* iv = frame.data() + header_size;
  if (RAND_bytes(key.data(), 32) != 1 || RAND_bytes(iv, static_cast<int>(block)) != 1) return fail(error);
  // Keep the reserved MGM high bit clear in either library byte convention.
  iv[0] &= 0x7f; iv[block-1] &= 0x7f;
  Context ctx;
  if (!ctx.setup(cipher, key.data())) return fail(error);
  uint8_t empty = 0;
  if (ak_bckey_encrypt_mgm(&ctx.key, &ctx.key, frame.data(), header_size,
      plain.empty() ? &empty : const_cast<uint8_t*>(plain.data()), iv + block, plain.size(),
      iv, block, iv + block + plain.size(), block) != ak_error_ok) return fail(error);
  out->data = std::move(frame); out->key = key.to_vector(); out->iv.clear(); out->tag.clear();
  return true;
}
bool gost_native_decrypt(Cipher cipher, const std::vector<uint8_t>& frame, const std::vector<uint8_t>& key,
                         CryptoResult* out, std::string* error) {
  if (!out || !id(cipher) || key.size() != 32 || frame.size() < header_size) return fail(error);
  const size_t block = cipher == Cipher::MAGMA ? 8 : 16;
  if (!std::equal(std::begin(magic), std::end(magic), frame.begin()) ||
      frame[8] != id(cipher) || frame[9] != block) return fail(error);
  uint64_t size = 0;
  for (size_t i = 0; i < 8; ++i) size |= uint64_t(frame[10+i]) << (8*i);
  if (size > max_size || frame.size() != header_size + block * 2 + size) return fail(error);
  std::lock_guard<std::mutex> lock(library_mutex);
  if (!init()) return fail(error);
  Context ctx;
  if (!ctx.setup(cipher, key.data())) return fail(error);
  SecureBuffer plain(static_cast<size_t>(size) + 1);
  const auto* iv = frame.data() + header_size;
  if (ak_bckey_decrypt_mgm(&ctx.key, &ctx.key, const_cast<uint8_t*>(frame.data()), header_size,
      const_cast<uint8_t*>(iv + block), plain.data(), static_cast<size_t>(size),
      const_cast<uint8_t*>(iv), block, const_cast<uint8_t*>(iv + block + size), block) != ak_error_ok) return fail(error);
  out->data.assign(plain.data(), plain.data() + size);
  out->key.clear(); out->iv.clear(); out->tag.clear(); return true;
}
bool gost_native_recover_legacy(const std::vector<uint8_t>& frame, const std::vector<uint8_t>& key,
    HashAlg hash, const std::string& digest, CryptoResult* out, std::string* error) {
  if (!out || key.size() != 32 || frame.size() < 16 || (frame.size()-16) % 16 ||
      frame.size() > max_size + 32 || digest.empty()) return fail(error);
  std::lock_guard<std::mutex> lock(library_mutex);
  if (!init()) return fail(error);
  Context ctx;
  // Historical server configured BOTH names to the same Kuznechik utility.
  if (!ctx.setup(Cipher::KUZNECHIK, key.data())) return fail(error);
  struct Plain {
    std::vector<uint8_t> data;
    ~Plain() { if (!data.empty()) OPENSSL_cleanse(data.data(), data.size()); }
  } plain;
  plain.data.resize(frame.size()-16);
  if (!plain.data.empty() && ak_bckey_decrypt_cbc(&ctx.key, const_cast<uint8_t*>(frame.data()+16),
      plain.data.data(), plain.data.size(), const_cast<uint8_t*>(frame.data()), 16) != ak_error_ok) return fail(error);
  CryptoEngine engine;
  HashResult calculated;
  if (!engine.hash(hash, plain.data, &calculated, error)) return false;
  if (calculated.hex.size() == digest.size() && CRYPTO_memcmp(calculated.hex.data(), digest.data(), digest.size()) == 0) {
    out->data = plain.data; return true;
  }
  if (plain.data.empty()) return fail(error);
  const auto padding = plain.data.back();
  if (!padding || padding > 15 || !std::all_of(plain.data.end()-padding, plain.data.end(),
      [padding](uint8_t value) { return value == padding; })) return fail(error);
  OPENSSL_cleanse(plain.data.data()+plain.data.size()-padding, padding);
  plain.data.resize(plain.data.size()-padding);
  if (!engine.hash(hash, plain.data, &calculated, error) || calculated.hex.size() != digest.size() ||
      CRYPTO_memcmp(calculated.hex.data(), digest.data(), digest.size()) != 0) return fail(error);
  out->data = plain.data; return true;
}
}
