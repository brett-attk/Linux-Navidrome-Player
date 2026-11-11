#pragma once

#include <string>
#include <string_view>

namespace radio::util {

enum class LogLevel { Warning, Error };

// Thread-safe file logger. Callers enqueue lines only; a background writer
// owns all disk I/O so logging cannot stall playback/UI for filesystem waits.
void log_init();
void log_shutdown();

// Override path (used by tests). Call before log_init(), or instead of it
// via log_init_for_test().
void log_set_path_for_test(std::string path);
void log_init_for_test(std::string path);

std::string log_path();

void log_message(LogLevel level, std::string_view message);

inline void log_warn(std::string_view message) {
  log_message(LogLevel::Warning, message);
}

inline void log_error(std::string_view message) {
  log_message(LogLevel::Error, message);
}

}  // namespace radio::util
