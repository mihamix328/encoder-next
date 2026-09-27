#include "encoder/bytes.h"
#include <filesystem>
#include <chrono>
#include <iostream>
#include <stdexcept>

int main() {
  namespace fs = std::filesystem;
  const auto root = fs::temp_directory_path() / ("encoder-unicode-" +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  try {
    if (!fs::create_directory(root)) throw std::runtime_error("temporary directory collision");
    const auto directory = root / fs::u8path(u8"\u041c\u041e\u042f \u6587\u4ef6 \U0001f512");
    fs::create_directory(directory);
    const auto file = directory / fs::u8path(u8"\u0431\u0438\u043e\u043b\u043e\u0433\u0438\u044f.docx");
    for (const auto& data : {std::vector<uint8_t>{0, 1, 127, 128, 255}, std::vector<uint8_t>{}}) {
      if (!encoder::write_file(file.u8string(), data)) throw std::runtime_error("Unicode write failed");
      bool ok = false;
      if (encoder::read_file(file.u8string(), &ok) != data || !ok)
        throw std::runtime_error("Unicode read mismatch");
      if (!fs::exists(file)) throw std::runtime_error("Wrong native filename");
    }
    bool ok = true;
    encoder::read_file((directory / "missing").u8string(), &ok);
    if (ok) throw std::runtime_error("Missing file accepted");
    fs::remove_all(root);
    std::cout << "Unicode paths: Cyrillic, CJK, emoji, binary and empty files passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    // Preserve the unique fixture for diagnosis on failure.
    return 1;
  }
}
