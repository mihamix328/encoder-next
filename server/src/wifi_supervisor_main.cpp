#include "wifi_linux_platform.h"
#include "wifi_recovery_supervisor.h"
#include <signal.h>
#include <unistd.h>
#include <iostream>
namespace {
volatile sig_atomic_t stopping = 0;
void stop_requested(int) { stopping = 1; }
}
int main(int argc, char** argv) {
  if (argc == 2 && std::string(argv[1]) == "--version") {
    std::cout << "encoder Wi-Fi recovery supervisor v1 (experimental)\n"; return 0;
  }
  if (argc != 2 || std::string(argv[1]) != "--run") {
    std::cerr << "Usage: encoder-wifi-recovery-supervisor --run|--version\n"; return 2;
  }
  if (geteuid() != 0) { std::cerr << "Privileged helper required\n"; return 1; }
  try {
    struct sigaction action{}; action.sa_handler = stop_requested; sigemptyset(&action.sa_mask);
    if (sigaction(SIGTERM, &action, nullptr) || sigaction(SIGINT, &action, nullptr)) return 1;
    // Recovery must also run if Ethernet was lost; it does not start transactions.
    encoder::WifiLinuxPlatform platform(""); std::string error;
    const bool result = encoder::run_wifi_recovery_supervisor(
        encoder::WifiLinuxPlatform::state_directory, encoder::WifiLinuxPlatform::netplan_directory,
        [&] { return platform.restore_network(); }, [] { return stopping != 0; }, &error,
        [](encoder::RecoveryOutcome state) {
          const char* status = state == encoder::RecoveryOutcome::Failed ? "Recovery failed or busy; retrying"
            : state == encoder::RecoveryOutcome::Restored ? "Recovery completed"
            : state == encoder::RecoveryOutcome::Waiting ? "Pending confirmation" : "No pending recovery";
          std::cout << status << std::endl;
        });
    if (!result) std::cerr << "Recovery supervisor stopped with an error\n";
    return result ? 0 : 1;
  } catch (...) { std::cerr << "Recovery supervisor failed\n"; return 1; }
}
