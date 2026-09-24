#include "wifi_runtime_policy.h"
#include "netplan_files.h"
#include "encoder/wifi_config_draft.h"
#include <openssl/crypto.h>
namespace encoder {
bool prepare_wifi_runtime_policy(const std::string& netplan_directory,
    const std::string& runtime_directory, bool check_only, std::string* error) {
  auto source = NetplanFiles::open(netplan_directory, error);
  auto target = NetplanFiles::open_supplicant(runtime_directory, error);
  bool exists = false; SecureBuffer yaml, previous;
  if (!source || !target || !source->backup(&exists, &yaml, error)) return false;
  if (!exists) { *error = "A managed Wi-Fi profile is required before preparing runtime policy"; return false; }
  auto profile = read_wifi_config_draft({reinterpret_cast<const char*>(yaml.data()), yaml.size()}, error);
  if (!profile) return false;
  auto draft = make_wifi_config_draft(*profile, error);
  if (!draft || !target->backup(&exists, &previous, error)) return false;
  if (check_only) return true;
  SecureBuffer current;
  if (!source->backup(&exists, &current, error) || !exists || current.size() != yaml.size() ||
      CRYPTO_memcmp(current.data(), yaml.data(), yaml.size())) {
    *error = "Managed profile changed during runtime policy preparation"; return false;
  }
  return target->replace(draft->supplicant_config, error);
}
}
