#include "components_manager.hpp"
#include "constants.hpp"
#include "filesystem.hpp"
#include "karabiner_version.h"
#include "logger.hpp"
#include "process_utility.hpp"
#include "spdlog_utility.hpp"
#include "thread_utility.hpp"
#include <iostream>
#include <sstream>
#include <unistd.h>

int main(int argc, const char* argv[]) {
  if (getuid() != 0) {
    std::cerr << "fatal: karabiner_observer requires root privilege." << std::endl;
    exit(1);
  }

  signal(SIGUSR1, SIG_IGN);
  signal(SIGUSR2, SIG_IGN);
  krbn::thread_utility::register_main_thread();

  // Setup logger

  krbn::logger::set_async_rotating_logger("observer",
                                          "/var/log/karabiner/observer.log",
                                          0755);

  krbn::logger::get_logger().info("version {0}", karabiner_version);

  // Check another process

  {
    std::string pid_file_path = krbn::constants::get_pid_directory() + "/karabiner_observer.pid";
    if (!krbn::process_utility::lock_single_application(pid_file_path)) {
      auto message = "Exit since another process is running.";
      krbn::logger::get_logger().info(message);
      std::cerr << message << std::endl;
      exit(1);
    }
  }

  // Run components_manager

  auto time_source = std::make_shared<pqrs::dispatcher::hardware_time_source>();
  auto dispatcher = std::make_shared<pqrs::dispatcher::dispatcher>(time_source);
  auto components_manager = std::make_unique<krbn::components_manager>(dispatcher);

  CFRunLoopRun();

  components_manager = nullptr;

  dispatcher->terminate();
  dispatcher = nullptr;

  time_source = nullptr;

  krbn::logger::get_logger().info("karabiner_observer is terminated.");

  return 0;
}
