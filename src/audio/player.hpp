#pragma once

#include "audio/eq_settings.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace radio::audio {

enum class State {
  Stopped,
  Playing,
  Paused,
};

// Lightweight bass/overall energy for UI visualizers. Captured off the
// playback path and polled from the UI thread.
struct BeatLevels {
  float bass = 0.0f;     // 0..1
  float overall = 0.0f;  // 0..1
};

// Thin C++ wrapper around GStreamer playbin + equalizer-10bands.
// Spectrum analysis runs on a leaky side-chain so it cannot block playback.
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

  // Thread-safe snapshot for visualizers. Returns true when new analyzer
  // data arrived since the previous successful pull.
  bool pull_beat_levels(BeatLevels& out);

 private:
  void set_state(State state);
  bool on_bus_message(void* message);
  bool on_tick();
  void apply_pending_seek();
  void ensure_bus_and_tick();
  void apply_eq_to_element();
  void build_audio_filter();
  void build_audio_sink();
  void store_spectrum(void* structure);
  int on_bus_sync(void* message);

  void* pipeline_ = nullptr;   // GstElement* playbin
  void* equalizer_ = nullptr;  // GstElement* equalizer-10bands (owned by playbin)
  void* audio_sink_ = nullptr; // GstBin* playback+viz sink (owned by playbin)
  unsigned bus_watch_id_ = 0;
  unsigned tick_id_ = 0;
  State state_ = State::Stopped;
  double volume_ = 0.8;
  std::int64_t pending_seek_ns_ = -1;
  EqSettings eq_;
  std::shared_ptr<std::atomic<bool>> alive_;

  mutable std::mutex beat_mu_;
  BeatLevels beat_levels_{};
  std::uint64_t beat_seq_ = 0;
  std::uint64_t beat_pulled_seq_ = 0;

  StateCb state_cb_;
  ProgressCb progress_cb_;
  EosCb eos_cb_;
  ErrorCb error_cb_;
};

}  // namespace radio::audio
