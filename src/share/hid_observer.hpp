#pragma once

// `krbn::hid_observer` can be used safely in a multi-threaded environment.

#include "boost_utility.hpp"
#include "dispatcher.hpp"
#include "human_interface_device.hpp"
#include "logger.hpp"
#include "thread_utility.hpp"

namespace krbn {
class hid_observer final : public pqrs::dispatcher::dispatcher_client {
public:
  // Signals

  boost::signals2::signal<void(void)> device_observed;
  boost::signals2::signal<void(void)> device_unobserved;

  // Methods

  hid_observer(const hid_observer&) = delete;

  hid_observer(std::weak_ptr<pqrs::dispatcher::dispatcher> weak_dispatcher,
               std::weak_ptr<human_interface_device> weak_hid) : dispatcher_client(weak_dispatcher),
                                                                 weak_hid_(weak_hid),
                                                                 observed_(false) {
    if (auto hid = weak_hid.lock()) {
      // opened
      {
        auto c = hid->opened.connect([this] {
          enqueue_to_dispatcher([this] {
            if (auto hid = weak_hid_.lock()) {
              observed_ = true;

              device_observed();

              hid->async_queue_start();
              hid->async_schedule();
            }
          });
        });
        human_interface_device_connections_.push_back(c);
      }

      // open_failed
      {
        auto c = hid->open_failed.connect([this](auto&& error) {
          enqueue_to_dispatcher([this, error] {
            if (auto hid = weak_hid_.lock()) {
              auto message = fmt::format("IOHIDDeviceOpen error: {0} ({1}) {2}",
                                         iokit_utility::get_error_name(error),
                                         error,
                                         hid->get_name_for_log());
              logger_unique_filter_.error(message);
            }
          });
        });
        human_interface_device_connections_.push_back(c);
      }

      // closed
      {
        auto c = hid->closed.connect([this] {
          enqueue_to_dispatcher([this] {
            if (auto hid = weak_hid_.lock()) {
              device_unobserved();
            }
          });
        });
        human_interface_device_connections_.push_back(c);
      }

      // close_failed
      {
        auto c = hid->close_failed.connect([this](auto&& error) {
          enqueue_to_dispatcher([this, error] {
            if (auto hid = weak_hid_.lock()) {
              auto message = fmt::format("IOHIDDeviceClose error: {0} ({1}) {2}",
                                         iokit_utility::get_error_name(error),
                                         error,
                                         hid->get_name_for_log());
              logger_unique_filter_.error(message);

              device_unobserved();
            }
          });
        });
        human_interface_device_connections_.push_back(c);
      }
    }
  }

  virtual ~hid_observer(void) {
    detach_from_dispatcher([this] {
      unobserve();
    });

    // Disconnect `human_interface_device_connections_`

    if (auto hid = weak_hid_.lock()) {
      hid->get_run_loop_thread()->enqueue(^{
        human_interface_device_connections_.disconnect_all_connections();
      });
    } else {
      human_interface_device_connections_.disconnect_all_connections();
    }

    human_interface_device_connections_.wait_disconnect_all_connections();
  }

  std::weak_ptr<human_interface_device> get_weak_hid(void) {
    return weak_hid_;
  }

  void async_observe(void) {
    enqueue_to_dispatcher([this] {
      if (timer_) {
        return;
      }

      logger_unique_filter_.reset();

      timer_ = std::make_unique<thread_utility::timer>(
          [](auto&& count) {
            if (count == 0) {
              return std::chrono::milliseconds(0);
            } else {
              return std::chrono::milliseconds(3000);
            }
          },
          thread_utility::timer::mode::repeat,
          [this] {
            enqueue_to_dispatcher([this] {
              if (!observed_) {
                if (auto hid = weak_hid_.lock()) {
                  if (!hid->get_removed()) {
                    hid->async_open();
                    return;
                  }
                }
              }

              timer_->cancel();
            });
          });
    });
  }

  void async_unobserve(void) {
    enqueue_to_dispatcher([this] {
      unobserve();
    });
  }

private:
  void unobserve(void) {
    if (!timer_) {
      return;
    }

    timer_->cancel();
    timer_ = nullptr;

    if (observed_) {
      if (auto hid = weak_hid_.lock()) {
        hid->async_unschedule();
        hid->async_queue_stop();
        hid->async_close();
      }

      observed_ = false;
    }
  }

  std::weak_ptr<human_interface_device> weak_hid_;

  boost_utility::signals2_connections human_interface_device_connections_;
  bool observed_;
  logger::unique_filter logger_unique_filter_;
  std::unique_ptr<thread_utility::timer> timer_;
};
} // namespace krbn
