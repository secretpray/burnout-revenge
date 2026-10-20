// ealobby: a Burnout Revenge lobby server of your own, standing in for EA's.
//
//   ealobby [--host 0.0.0.0] [--directory-port 31860] [--lobby-port 31861]
//           [--advertise <address clients should reach the lobby at>]
//           [--locker-port 31862] [--locker-dir locker]
//
// The Locker keeps the players' Burnout Clips (shared, recommended, the top 20)
// as files under --locker-dir; --locker-port 0 turns it off.
//
// Point a game at it with --lobby_server=<address> (see SETUP.md).
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <thread>

#include "ealobby/server.h"

namespace {
volatile std::sig_atomic_t g_stop = 0;
void OnSignal(int) {
  g_stop = 1;
}
}  // namespace

int main(int argc, char** argv) {
  ealobby::Options options;
  options.locker_port = 31862;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto value = [&]() -> std::string {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "%s needs a value\n", arg.c_str());
        std::exit(2);
      }
      return argv[++i];
    };
    if (arg == "--host") {
      options.host = value();
    } else if (arg == "--directory-port") {
      options.directory_port = uint16_t(std::stoi(value()));
    } else if (arg == "--lobby-port") {
      options.lobby_port = uint16_t(std::stoi(value()));
    } else if (arg == "--advertise") {
      options.advertise = value();
    } else if (arg == "--locker-port") {
      options.locker_port = uint16_t(std::stoi(value()));
    } else if (arg == "--locker-dir") {
      options.locker_dir = value();
    } else {
      std::fprintf(stderr,
                   "usage: ealobby [--host A] [--directory-port P] [--lobby-port P] "
                   "[--advertise A] [--locker-port P] [--locker-dir D]\n");
      return arg == "--help" || arg == "-h" ? 0 : 2;
    }
  }
  ealobby::Server server(options, [](const std::string& line) {
    char stamp[16];
    const std::time_t now = std::time(nullptr);
    std::strftime(stamp, sizeof(stamp), "%H:%M:%S", std::localtime(&now));
    std::fprintf(stderr, "%s %s\n", stamp, line.c_str());
  });
  if (!server.Start()) {
    return 1;
  }
  std::signal(SIGINT, OnSignal);
  std::signal(SIGTERM, OnSignal);
  while (!g_stop) {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }
  server.Stop();
  return 0;
}
