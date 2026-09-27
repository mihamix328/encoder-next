#pragma once
#include "encoder/crypto.h"
namespace encoder {
bool gost_native_encrypt(Cipher, const std::vector<uint8_t>&, CryptoResult*, std::string*);
bool gost_native_decrypt(Cipher, const std::vector<uint8_t>&, const std::vector<uint8_t>&, CryptoResult*, std::string*);
bool gost_native_recover_legacy(const std::vector<uint8_t>&, const std::vector<uint8_t>&,
                               HashAlg, const std::string&, CryptoResult*, std::string*);
}
