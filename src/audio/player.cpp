#include "audio/player.hpp"

#include "util/safe.hpp"

#include <gst/gst.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>

namespace radio::audio {
namespace {

// Passed as void* into C-style GStreamer / GLib callbacks.
struct WatchContext {
  Player* self = nullptr;
  std::shared_ptr<std::atomic<bool>> alive;
};

}  // namespace

Player::Player() {
  // gst_init is process-wide; only call it once.
  static bool gst_inited = false;
  if (!gst_inited) {
    gst_init(nullptr, nullptr);
    gst_inited = true;
  }
  alive_ = std::make_shared<std::atomic<bool>>(true);
  pipeline_ = gst_element_factory_make("playbin", "player");
  equalizer_ = gst_element_factory_make("equalizer-10bands", "eq");
  if (pipeline_) {
    g_object_set(pipeline_, "volume", volume_, nullptr);
    if (equalizer_) {
      // playbin adopts the floating GObject ref from factory_make.
      g_object_set(pipeline_, "audio-filter", equalizer_, nullptr);
      apply_eq_to_element();
    }
  }
}

Player::~Player() {
  if (alive_) {
    alive_->store(false);
  }
  stop();
  if (tick_id_) {
    g_source_remove(tick_id_);
    tick_id_ = 0;
  }
  if (bus_watch_id_) {
    g_source_remove(bus_watch_id_);
    bus_watch_id_ = 0;
  }
  state_cb_ = {};
  progress_cb_ = {};
  eos_cb_ = {};
  error_cb_ = {};
  if (pipeline_) {
    // Clearing audio-filter drops playbin's reference to the equalizer.
    g_object_set(pipeline_, "audio-filter", static_cast<GstElement*>(nullptr),
                 nullptr);
    gst_object_unref(pipeline_);
    pipeline_ = nullptr;
  }
  equalizer_ = nullptr;
}

void Player::set_state_callback(StateCb cb) { state_cb_ = std::move(cb); }
void Player::set_progress_callback(ProgressCb cb) {
  progress_cb_ = std::move(cb);
}
void Player::set_eos_callback(EosCb cb) { eos_cb_ = std::move(cb); }
void Player::set_error_callback(ErrorCb cb) { error_cb_ = std::move(cb); }

void Player::set_state(State state) {
  state_ = state;
  util::invoke_nothrow(state_cb_, state_);
}

bool Player::on_bus_message(void* message_ptr) {
  auto* message = static_cast<GstMessage*>(message_ptr);
  switch (GST_MESSAGE_TYPE(message)) {
    case GST_MESSAGE_EOS:
      set_state(State::Stopped);
      util::invoke_nothrow(eos_cb_);
      break;
    case GST_MESSAGE_ERROR: {
      GError* err = nullptr;
      gchar* debug = nullptr;
      gst_message_parse_error(message, &err, &debug);
      std::string msg = err ? err->message : "GStreamer error";
      if (err) {
        g_error_free(err);
      }
      g_free(debug);
      set_state(State::Stopped);
      util::invoke_nothrow(error_cb_, msg);
      break;
    }
    case GST_MESSAGE_STATE_CHANGED: {
      if (GST_MESSAGE_SRC(message) == GST_OBJECT(pipeline_)) {
        GstState old_state = GST_STATE_NULL;
        GstState new_state = GST_STATE_NULL;
        gst_message_parse_state_changed(message, &old_state, &new_state,
                                        nullptr);
        if (new_state == GST_STATE_PLAYING) {
          set_state(State::Playing);
          apply_pending_seek();
        } else if (new_state == GST_STATE_PAUSED && state_ != State::Stopped) {
          set_state(State::Paused);
          apply_pending_seek();
        }
      }
      break;
    }
    case GST_MESSAGE_ASYNC_DONE:
      apply_pending_seek();
      break;
    default:
      break;
  }
  return true;
}

bool Player::on_tick() {
  if (!pipeline_ || !progress_cb_ || state_ == State::Stopped) {
    return true;
  }
  gint64 pos = 0;
  gint64 dur = 0;
  if (gst_element_query_position(GST_ELEMENT(pipeline_), GST_FORMAT_TIME,
                                 &pos) &&
      gst_element_query_duration(GST_ELEMENT(pipeline_), GST_FORMAT_TIME,
                                 &dur)) {
    util::invoke_nothrow(progress_cb_, pos, dur);
  }
  return true;
}

void Player::ensure_bus_and_tick() {
  if (!pipeline_) {
    return;
  }
  // Bus watch: GStreamer posts EOS/ERROR/STATE_CHANGED messages here.
  if (!bus_watch_id_) {
    GstBus* bus = gst_element_get_bus(GST_ELEMENT(pipeline_));
    if (!bus) {
      return;
    }
    auto* data = new WatchContext{this, alive_};
    bus_watch_id_ = gst_bus_add_watch(
        bus,
        +[](GstBus*, GstMessage* message, gpointer user_data) -> gboolean {
          auto* d = static_cast<WatchContext*>(user_data);
          if (!d->alive || !d->alive->load() || !d->self) {
            return FALSE;  // remove the watch
          }
          return d->self->on_bus_message(message) ? TRUE : FALSE;
        },
        data);
    // When the pipeline GObject is destroyed, free WatchContext automatically.
    g_object_set_data_full(G_OBJECT(pipeline_), "radio-bus-data", data,
                           +[](gpointer p) { delete static_cast<WatchContext*>(p); });
    gst_object_unref(bus);
  }

  // Progress timer: ~4 Hz UI updates while playing.
  // g_timeout_add_full's final arg is a destroy notify for user_data.
  if (!tick_id_) {
    auto* data = new WatchContext{this, alive_};
    tick_id_ = g_timeout_add_full(
        G_PRIORITY_DEFAULT, 250,
        +[](gpointer user_data) -> gboolean {
          auto* d = static_cast<WatchContext*>(user_data);
          if (!d->alive || !d->alive->load() || !d->self) {
            return G_SOURCE_REMOVE;
          }
          return d->self->on_tick() ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
        },
        data, +[](gpointer p) { delete static_cast<WatchContext*>(p); });
  }
}

void Player::apply_pending_seek() {
  // Seeking before the pipeline is ready often fails; defer until PLAYING/PAUSED.
  if (!pipeline_ || pending_seek_ns_ < 0) {
    return;
  }
  const gint64 pos = pending_seek_ns_;
  pending_seek_ns_ = -1;
  gst_element_seek_simple(GST_ELEMENT(pipeline_), GST_FORMAT_TIME,
                          static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH |
                                                    GST_SEEK_FLAG_KEY_UNIT),
                          pos);
}

void Player::play_uri(const std::string& uri, std::int64_t start_position_ns) {
  if (!pipeline_) {
    util::invoke_nothrow(error_cb_, std::string{"GStreamer playbin unavailable"});
    return;
  }
  if (uri.empty()) {
    util::invoke_nothrow(error_cb_, std::string{"Missing stream URL"});
    return;
  }

  pending_seek_ns_ = start_position_ns;
  gst_element_set_state(GST_ELEMENT(pipeline_), GST_STATE_NULL);
  g_object_set(pipeline_, "uri", uri.c_str(), "volume", volume_, nullptr);
  ensure_bus_and_tick();
  const GstStateChangeReturn ret =
      gst_element_set_state(GST_ELEMENT(pipeline_), GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    set_state(State::Stopped);
    util::invoke_nothrow(error_cb_, std::string{"Failed to start playback"});
    return;
  }
  set_state(State::Playing);
}

void Player::pause() {
  if (!pipeline_ || state_ != State::Playing) {
    return;
  }
  gst_element_set_state(GST_ELEMENT(pipeline_), GST_STATE_PAUSED);
  set_state(State::Paused);
}

void Player::resume() {
  if (!pipeline_ || state_ != State::Paused) {
    return;
  }
  gst_element_set_state(GST_ELEMENT(pipeline_), GST_STATE_PLAYING);
  set_state(State::Playing);
}

void Player::stop() {
  if (!pipeline_) {
    return;
  }
  pending_seek_ns_ = -1;
  gst_element_set_state(GST_ELEMENT(pipeline_), GST_STATE_NULL);
  set_state(State::Stopped);
}

void Player::toggle_pause() {
  if (state_ == State::Playing) {
    pause();
  } else if (state_ == State::Paused) {
    resume();
  }
}

void Player::seek_ns(std::int64_t position_ns) {
  if (!pipeline_ || position_ns < 0) {
    return;
  }
  if (state_ == State::Stopped) {
    pending_seek_ns_ = position_ns;
    return;
  }
  gst_element_seek_simple(GST_ELEMENT(pipeline_), GST_FORMAT_TIME,
                          static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH |
                                                    GST_SEEK_FLAG_KEY_UNIT),
                          position_ns);
}

void Player::seek(double fraction) {
  if (!pipeline_) {
    return;
  }
  fraction = std::clamp(fraction, 0.0, 1.0);
  gint64 dur = 0;
  if (!gst_element_query_duration(GST_ELEMENT(pipeline_), GST_FORMAT_TIME,
                                  &dur) ||
      dur <= 0) {
    return;
  }
  seek_ns(static_cast<std::int64_t>(fraction * static_cast<double>(dur)));
}

void Player::set_volume(double volume) {
  volume_ = std::clamp(volume, 0.0, 1.0);
  if (pipeline_) {
    g_object_set(pipeline_, "volume", volume_, nullptr);
  }
}

void Player::apply_eq_to_element() {
  if (!equalizer_) {
    return;
  }
  for (int i = 0; i < kEqBands; ++i) {
    GObject* band =
        gst_child_proxy_get_child_by_index(GST_CHILD_PROXY(equalizer_), i);
    if (!band) {
      continue;
    }
    const double gain =
        eq_.enabled ? eq_.gains_db[static_cast<std::size_t>(i)] : 0.0;
    // g_object_set varargs for "gdouble" require an actual gdouble, not double
    // promotion ambiguity on some ABIs.
    const gdouble gain_db = static_cast<gdouble>(std::clamp(gain, -24.0, 12.0));
    g_object_set(band, "gain", gain_db, nullptr);
    g_object_unref(band);
  }
}

void Player::set_eq(const EqSettings& settings) {
  eq_ = settings;
  apply_eq_to_element();
}

double Player::volume() const { return volume_; }

std::int64_t Player::position_ns() const {
  if (!pipeline_) {
    return 0;
  }
  gint64 pos = 0;
  if (!gst_element_query_position(GST_ELEMENT(pipeline_), GST_FORMAT_TIME,
                                  &pos)) {
    return 0;
  }
  return pos;
}

std::int64_t Player::duration_ns() const {
  if (!pipeline_) {
    return 0;
  }
  gint64 dur = 0;
  if (!gst_element_query_duration(GST_ELEMENT(pipeline_), GST_FORMAT_TIME,
                                  &dur)) {
    return 0;
  }
  return dur;
}

}  // namespace radio::audio
