#include "encoder/crypto.h"
#include "encoder/bytes.h"
#include <cstdlib>
#include <iostream>
using namespace encoder;
void check(bool ok, const char* what) { if (!ok) { std::cerr << what << '\n'; std::exit(1); } }
int main(int argc, char** argv) {
  CryptoEngine engine;
  if (argc == 4) {
    bool ok = false;
    const auto cipher = read_file(argv[1], &ok); check(ok, "legacy ciphertext");
    const auto key = read_file(argv[2], &ok); check(ok, "legacy key");
    const auto original = read_file(argv[3], &ok); check(ok, "legacy fixture");
    HashResult digest; CryptoResult result; std::string error;
    check(engine.hash(HashAlg::SHA256, original, &digest, &error), "fixture hash");
    check(engine.recover_legacy_gost(cipher, key, HashAlg::SHA256, digest.hex, &result, &error), "native legacy recovery");
    check(result.data == original, "legacy mismatch");
    check(!engine.recover_legacy_gost(cipher, key, HashAlg::SHA256, std::string(64, '0'), &result, &error), "wrong trusted hash accepted");
    check(result.data.empty(), "legacy failure exposed plaintext");
    return 0;
  }
  size_t count = 0;
  for (auto cipher : {Cipher::KUZNECHIK, Cipher::MAGMA}) {
    for (size_t size : {0u, 1u, 7u, 8u, 15u, 16u, 17u, 31u, 32u, 64u, 257u, 65536u}) {
      std::vector<uint8_t> data(size, 1); CryptoResult encrypted, plain;
      std::string error;
      check(engine.encrypt(cipher, data, &encrypted, &error), error.c_str());
      check(encrypted.key.size() == 32, "key size");
      check(engine.decrypt(cipher, encrypted.data, encrypted.key, {}, {}, &plain, &error), "decrypt");
      check(plain.data == data, "round trip");
      for (size_t offset : {size_t(0), size_t(8), size_t(10), size_t(18), encrypted.data.size()-1}) {
        auto tampered = encrypted.data; tampered[offset] ^= 1;
        check(!engine.decrypt(cipher, tampered, encrypted.key, {}, {}, &plain, &error), "tampering accepted");
        check(plain.data.empty(), "unauthenticated plaintext exposed");
      }
      if (size) {
        auto tampered = encrypted.data; tampered[encrypted.data.size()-1-(cipher == Cipher::MAGMA ? 8 : 16)] ^= 1;
        check(!engine.decrypt(cipher, tampered, encrypted.key, {}, {}, &plain, &error), "ciphertext tampering accepted");
      }
      auto wrong = encrypted.key; wrong[0] ^= 1;
      check(!engine.decrypt(cipher, encrypted.data, wrong, {}, {}, &plain, &error), "wrong key accepted");
      check(!engine.decrypt(cipher == Cipher::MAGMA ? Cipher::KUZNECHIK : Cipher::MAGMA,
                            encrypted.data, encrypted.key, {}, {}, &plain, &error), "wrong cipher accepted");
      auto short_frame = encrypted.data; short_frame.pop_back();
      check(!engine.decrypt(cipher, short_frame, encrypted.key, {}, {}, &plain, &error), "truncation accepted");
      ++count;
    }
  }
  std::cout << "Native GOST-MGM: " << count << " round trips and tampering checks passed\n";
}
