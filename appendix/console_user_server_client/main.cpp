#include "logger.hpp"
#include "manipulator/details/post_event_to_virtual_devices.hpp"
#include "thread_utility.hpp"
#include "time_utility.hpp"
#include "virtual_hid_device_client.hpp"

int main(int argc, const char* argv[]) {
  krbn::thread_utility::register_main_thread();

  signal(SIGINT, [](int) {
    CFRunLoopStop(CFRunLoopGetMain());
  });

  auto time_source = std::make_shared<pqrs::dispatcher::hardware_time_source>();
  auto dispatcher = std::make_shared<pqrs::dispatcher::dispatcher>(time_source);

  auto client = std::make_shared<krbn::console_user_server_client>(dispatcher);

  client->connected.connect([client] {
    std::string shell_command = "open /Applications/Safari.app";
    client->async_shell_command_execution(shell_command);
  });

  client->connect_failed.connect([](auto&& error_code) {
    krbn::logger::get_logger().error("Failed to connect");
  });

  client->async_start();

  // ------------------------------------------------------------

  CFRunLoopRun();

  // ------------------------------------------------------------

  client = nullptr;

  dispatcher->terminate();
  dispatcher = nullptr;

  return 0;
}
