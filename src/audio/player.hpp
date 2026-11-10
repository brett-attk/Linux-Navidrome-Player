#pragma once

#include "audio/eq_settings.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace radio::audio {

enum class State {
  Stopped,
  Playing,
  Paused,
};

// Thin C++ wrapper around GStreamer playbin + equalizer-10bands.
// UI code registers callbacks; this class talks to the pipeline and forwards
// EOS / error / progress events back on the GLib main loop.
class Player {
 public:
  using StateCb = std::function<void(State)>;
  using ProgressCb =
      std::function<void(std::int64_t position_ns, std::int64_t duration_ns)>;
  using EosCb = std::function<void()>;
  using ErrorCb = std::function<void(const std::string&)>;

  Player();
  ~Player();

  Player(const Player&) = delete;
  Player& operator=(const Player&) = delete;

  void set_state_callback(StateCb cb);
  void set_progress_callback(ProgressCb cb);
  void set_eos_callback(EosCb cb);
  void set_error_callback(ErrorCb cb);

  // start_position_ns < 0 means "start at the beginning".
  void play_uri(const std::string& uri, std::int64_t start_position_ns = -1);
  void pause();
  void resume();
  void stop();
  void toggle_pause();
  void seek(double fraction);  // 0.0 .. 1.0 of track duration
  void seek_ns(std::int64_t position_ns);
  void set_volume(double volume);  // 0.0 .. 1.0
  double volume() const;
  void set_eq(const EqSettings& settings);
  const EqSettings& eq() const { return eq_; }
  std::int64_t position_ns() const;
  std::int64_t duration_ns() const;

  State state() const { return state_; }

 private:
  void set_state(State state);
  bool on_bus_message(void* message);
  bool on_tick();
  void apply_pending_seek();
  void ensure_bus_and_tick();
  void apply_eq_to_element();

  void* pipeline_ = nullptr;   // GstElement* playbin
  void* equalizer_ = nullptr;  // GstElement* equalizer-10bands (owned by playbin)
  unsigned bus_watch_id_ = 0;  // GSource id from gst_bus_add_watch
  unsigned tick_id_ = 0;       // GSource id from g_timeout_add_full
  State state_ = State::Stopped;
  double volume_ = 0.8;
  // Seek requested before the pipeline reaches PAUSED/PLAYING; applied later.
  std::int64_t pending_seek_ns_ = -1;
  EqSettings eq_;
  // Shared with bus/tick callbacks so they stop touching `this` after destroy.
  std::shared_ptr<std::atomic<bool>> alive_;

  StateCb state_cb_;
  ProgressCb progress_cb_;
  EosCb eos_cb_;
  ErrorCb error_cb_;
};

}  // namespace radio::audio
