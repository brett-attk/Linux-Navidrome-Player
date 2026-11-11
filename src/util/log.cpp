#include "util/log.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>

namespace fs = std::filesystem;

namespace radio::util {
namespace {

constexpr std::size_t kMaxQueuedLines = 512;
constexpr std::uintmax_t kRotateBytes = 2 * 1024 * 1024;  // 2 MiB

std::mutex g_mu;
std::condition_variable g_cv;
std::deque<std::string> g_queue;
std::thread g_writer;
std::atomic<bool> g_running{false};
std::atomic<bool> g_drop_notified{false};
std::string g_path;
std::string g_path_override;
bool g_started = false;

std::string default_log_path() {
  const char* xdg = std::getenv("XDG_STATE_HOME");
  fs::path base;
  if (xdg && *xdg) {
    base = xdg;
  } else {
    const char* home = std::getenv("HOME");
    base = home ? fs::path(home) / ".local" / "state" : fs::path(".local") / "state";
  }
  return (base / "radio-player" / "radio-player.log").string();
}

std::string level_tag(LogLevel level) {
  switch (level) {
    case LogLevel::Warning:
      return "WARN";
    case LogLevel::Error:
      return "ERROR";
  }
  return "ERROR";
}

std::string utc_timestamp() {
  using clock = std::chrono::system_clock;
  const auto now = clock::now();
  const std::time_t t = clock::to_time_t(now);
  std::tm tm{};
  gmtime_r(&t, &tm);
  char buf[32];
  if (std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm) == 0) {
    return "unknown-time";
  }
  return buf;
}

void rotate_if_needed(const std::string& path) {
  std::error_code ec;
  if (!fs::exists(path, ec)) {
    return;
  }
  const auto size = fs::file_size(path, ec);
  if (ec || size < kRotateBytes) {
    return;
  }
  const fs::path rotated = fs::path(path).string() + ".old";
  fs::remove(rotated, ec);
  fs::rename(path, rotated, ec);
}

void writer_main(std::string path) {
  std::ofstream out(path, std::ios::out | std::ios::app);
  // If the file cannot be opened, drain quietly so producers never block forever.
  while (true) {
    std::deque<std::string> batch;
    {
      std::unique_lock<std::mutex> lock(g_mu);
      g_cv.wait(lock, [] {
        return !g_queue.empty() || !g_running.load(std::memory_order_relaxed);
      });
      if (g_queue.empty() && !g_running.load(std::memory_order_relaxed)) {
        break;
      }
      batch.swap(g_queue);
    }
    if (!out) {
      continue;
    }
    for (const auto& line : batch) {
      out << line << '\n';
    }
    out.flush();
  }
}

void start_writer_unlocked() {
  if (g_started) {
    return;
  }
  g_path = g_path_override.empty() ? default_log_path() : g_path_override;
  std::error_code ec;
  fs::create_directories(fs::path(g_path).parent_path(), ec);
  rotate_if_needed(g_path);
  g_running.store(true, std::memory_order_relaxed);
  g_drop_notified.store(false, std::memory_order_relaxed);
  g_writer = std::thread(writer_main, g_path);
  g_started = true;
}

}  // namespace

void log_set_path_for_test(std::string path) {
  std::lock_guard<std::mutex> lock(g_mu);
  g_path_override = std::move(path);
}

void log_init_for_test(std::string path) {
  log_set_path_for_test(std::move(path));
  log_init();
}

void log_init() {
  std::lock_guard<std::mutex> lock(g_mu);
  start_writer_unlocked();
}

void log_shutdown() {
  std::thread writer;
  {
    std::lock_guard<std::mutex> lock(g_mu);
    if (!g_started) {
      return;
    }
    g_running.store(false, std::memory_order_relaxed);
    writer = std::move(g_writer);
    g_started = false;
  }
  g_cv.notify_all();
  if (writer.joinable()) {
    writer.join();
  }
}

std::string log_path() {
  std::lock_guard<std::mutex> lock(g_mu);
  if (!g_path.empty()) {
    return g_path;
  }
  if (!g_path_override.empty()) {
    return g_path_override;
  }
  return default_log_path();
}

void log_message(LogLevel level, std::string_view message) {
  if (message.empty()) {
    return;
  }
  std::string line = utc_timestamp();
  line += " [";
  line += level_tag(level);
  line += "] ";
  line.append(message.data(), message.size());

  {
    std::lock_guard<std::mutex> lock(g_mu);
    if (!g_started) {
      // Lazy-start so early failures before main()'s log_init still record.
      start_writer_unlocked();
    }
    if (!g_running.load(std::memory_order_relaxed)) {
      return;
    }
    if (g_queue.size() >= kMaxQueuedLines) {
      g_queue.pop_front();
      if (!g_drop_notified.exchange(true, std::memory_order_relaxed)) {
        g_queue.push_back(utc_timestamp() +
                          " [WARN] Log queue full; older lines were dropped");
        if (g_queue.size() >= kMaxQueuedLines) {
          g_queue.pop_front();
        }
      }
    }
    g_queue.push_back(std::move(line));
  }
  g_cv.notify_one();
}

}  // namespace radio::util
