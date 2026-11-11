#include "audio/player.hpp"

#include "audio/beat_math.hpp"
#include "util/safe.hpp"

#include <gst/gst.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <span>

namespace radio::audio {
namespace {

struct WatchContext {
  Player* self = nullptr;
  std::shared_ptr<std::atomic<bool>> alive;
};

constexpr int kSpectrumFftBands = 64;
// Fast enough for visuals; still cheap so the playback branch stays primary.
constexpr guint64 kSpectrumIntervalNs = 25 * GST_MSECOND;

bool link_tee_pad(GstElement* tee, GstElement* sink_element) {
  GstPad* tee_src = gst_element_request_pad_simple(tee, "src_%u");
  GstPad* sink_pad = gst_element_get_static_pad(sink_element, "sink");
  if (!tee_src || !sink_pad) {
    if (tee_src) {
      gst_element_release_request_pad(tee, tee_src);
      gst_object_unref(tee_src);
    }
    if (sink_pad) {
      gst_object_unref(sink_pad);
    }
    return false;
  }
  const bool ok = gst_pad_link(tee_src, sink_pad) == GST_PAD_LINK_OK;
  gst_object_unref(sink_pad);
  gst_object_unref(tee_src);
  return ok;
}

void abandon_element(GstElement*& element) {
  if (element) {
    gst_object_unref(element);
    element = nullptr;
  }
}

// Play-only sink used when the optional visualizer branch cannot be built.
GstElement* build_play_only_sink() {
  auto* bin = gst_bin_new("radio-audio-sink");
  auto* sink = gst_element_factory_make("autoaudiosink", "real-sink");
  if (!bin || !sink) {
    abandon_element(bin);
    abandon_element(sink);
    return nullptr;
  }
  gst_bin_add(GST_BIN(bin), sink);
  GstPad* sink_pad = gst_element_get_static_pad(sink, "sink");
  if (!sink_pad) {
    gst_object_unref(bin);
    return nullptr;
  }
  gst_element_add_pad(bin, gst_ghost_pad_new("sink", sink_pad));
  gst_object_unref(sink_pad);
  return bin;
}

}  // namespace

Player::Player() {
  static bool gst_inited = false;
  if (!gst_inited) {
    gst_init(nullptr, nullptr);
    gst_inited = true;
  }
  alive_ = std::make_shared<std::atomic<bool>>(true);
  pipeline_ = gst_element_factory_make("playbin", "player");
  if (pipeline_) {
    g_object_set(pipeline_, "volume", volume_, nullptr);
    build_audio_filter();
    build_audio_sink();
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
  if (pipeline_) {
    GstBus* bus = gst_element_get_bus(GST_ELEMENT(pipeline_));
    if (bus) {
      gst_bus_set_sync_handler(bus, nullptr, nullptr, nullptr);
      gst_object_unref(bus);
    }
  }
  state_cb_ = {};
  progress_cb_ = {};
  eos_cb_ = {};
  error_cb_ = {};
  if (pipeline_) {
    g_object_set(pipeline_, "audio-filter", static_cast<GstElement*>(nullptr),
                 nullptr);
    g_object_set(pipeline_, "audio-sink", static_cast<GstElement*>(nullptr),
                 nullptr);
    gst_object_unref(pipeline_);
    pipeline_ = nullptr;
  }
  equalizer_ = nullptr;
  audio_sink_ = nullptr;
}

void Player::build_audio_filter() {
  equalizer_ = gst_element_factory_make("equalizer-10bands", "eq");
  if (!equalizer_) {
    return;
  }
  g_object_set(pipeline_, "audio-filter", equalizer_, nullptr);
  apply_eq_to_element();
}

void Player::build_audio_sink() {
  // Playback is primary. The spectrum visualizer is optional and attached on
  // a leaky side-chain so analyzer stalls never block audible audio.
  auto* bin = gst_bin_new("radio-audio-sink");
  auto* tee = gst_element_factory_make("tee", "audio-tee");
  auto* q_play = gst_element_factory_make("queue", "play-queue");
  auto* sink = gst_element_factory_make("autoaudiosink", "real-sink");
  auto* q_viz = gst_element_factory_make("queue", "viz-queue");
  auto* convert = gst_element_factory_make("audioconvert", "viz-convert");
  auto* spectrum = gst_element_factory_make("spectrum", "spectrum");
  auto* fakesink = gst_element_factory_make("fakesink", "viz-sink");

  const bool have_play = bin && tee && q_play && sink;
  const bool have_viz = q_viz && convert && spectrum && fakesink;
  if (!have_play) {
    abandon_element(bin);
    abandon_element(tee);
    abandon_element(q_play);
    abandon_element(sink);
    abandon_element(q_viz);
    abandon_element(convert);
    abandon_element(spectrum);
    abandon_element(fakesink);
    // Last resort: plain autoaudiosink so music still works.
    audio_sink_ = build_play_only_sink();
    if (audio_sink_) {
      g_object_set(pipeline_, "audio-sink", audio_sink_, nullptr);
    }
    return;
  }

  if (!have_viz) {
    abandon_element(q_viz);
    abandon_element(convert);
    abandon_element(spectrum);
    abandon_element(fakesink);
    abandon_element(tee);
    abandon_element(q_play);
    gst_bin_add(GST_BIN(bin), sink);
    GstPad* sink_pad = gst_element_get_static_pad(sink, "sink");
    if (!sink_pad) {
      gst_object_unref(bin);
      return;
    }
    gst_element_add_pad(bin, gst_ghost_pad_new("sink", sink_pad));
    gst_object_unref(sink_pad);
    audio_sink_ = bin;
    g_object_set(pipeline_, "audio-sink", bin, nullptr);
    return;
  }

  g_object_set(bin, "message-forward", TRUE, nullptr);
  // Playback queue may buffer; visualizer queue must drop instead of blocking.
  g_object_set(q_play, "max-size-buffers", 0, "max-size-bytes", 0,
               "max-size-time", static_cast<guint64>(GST_SECOND), nullptr);
  g_object_set(q_viz, "leaky", 2 /* GST_QUEUE_LEAK_DOWNSTREAM */,
               "max-size-buffers", 1, "max-size-bytes", 0, "max-size-time",
               static_cast<guint64>(0), nullptr);
  g_object_set(spectrum, "bands", static_cast<guint>(kSpectrumFftBands),
               "threshold", kSpectrumThresholdDb, "interval",
               kSpectrumIntervalNs, "post-messages", TRUE, "message-phase",
               FALSE, "multi-channel", FALSE, nullptr);
  g_object_set(fakesink, "sync", FALSE, "async", FALSE, nullptr);

  gst_bin_add_many(GST_BIN(bin), tee, q_play, sink, q_viz, convert, spectrum,
                   fakesink, nullptr);

  bool ok = link_tee_pad(tee, q_play) && gst_element_link(q_play, sink);
  if (ok) {
    ok = link_tee_pad(tee, q_viz) &&
         gst_element_link_many(q_viz, convert, spectrum, fakesink, nullptr);
  }

  GstPad* tee_sink = gst_element_get_static_pad(tee, "sink");
  if (!ok || !tee_sink) {
    if (tee_sink) {
      gst_object_unref(tee_sink);
    }
    gst_object_unref(bin);
    // Visualizer linkage failed — fall back to play-only so audio still works.
    audio_sink_ = build_play_only_sink();
    if (audio_sink_) {
      g_object_set(pipeline_, "audio-sink", audio_sink_, nullptr);
    }
    return;
  }
  gst_element_add_pad(bin, gst_ghost_pad_new("sink", tee_sink));
  gst_object_unref(tee_sink);

  audio_sink_ = bin;
  g_object_set(pipeline_, "audio-sink", bin, nullptr);
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

void Player::store_spectrum(void* structure_ptr) {
  // Runs on the GStreamer streaming thread via the sync bus handler.
  // Keep this allocation-free and short so playback stays smooth.
  auto* structure = static_cast<const GstStructure*>(structure_ptr);
  const GValue* magnitudes = gst_structure_get_value(structure, "magnitude");
  if (!magnitudes) {
    return;
  }

  std::array<float, kSpectrumFftBands> raw{};
  std::size_t n = 0;
  if (GST_VALUE_HOLDS_ARRAY(magnitudes)) {
    n = std::min<std::size_t>(gst_value_array_get_size(magnitudes),
                              raw.size());
    for (std::size_t i = 0; i < n; ++i) {
      raw[i] = g_value_get_float(gst_value_array_get_value(
          magnitudes, static_cast<guint>(i)));
    }
  } else if (GST_VALUE_HOLDS_LIST(magnitudes)) {
    n = std::min<std::size_t>(gst_value_list_get_size(magnitudes), raw.size());
    for (std::size_t i = 0; i < n; ++i) {
      raw[i] = g_value_get_float(gst_value_list_get_value(
          magnitudes, static_cast<guint>(i)));
    }
  }
  if (n == 0) {
    return;
  }

  const BeatLevels levels =
      levels_from_magnitudes(std::span<const float>(raw.data(), n));
  {
    std::lock_guard<std::mutex> lock(beat_mu_);
    beat_levels_ = levels;
    ++beat_seq_;
  }
}

bool Player::pull_beat_levels(BeatLevels& out) {
  std::lock_guard<std::mutex> lock(beat_mu_);
  if (beat_pulled_seq_ == beat_seq_) {
    return false;
  }
  out = beat_levels_;
  beat_pulled_seq_ = beat_seq_;
  return true;
}

int Player::on_bus_sync(void* message_ptr) {
  auto* message = static_cast<GstMessage*>(message_ptr);
  if (GST_MESSAGE_TYPE(message) != GST_MESSAGE_ELEMENT) {
    return GST_BUS_PASS;
  }
  const GstStructure* structure = gst_message_get_structure(message);
  if (!structure || !gst_structure_has_name(structure, "spectrum")) {
    return GST_BUS_PASS;
  }
  store_spectrum(const_cast<GstStructure*>(structure));
  return GST_BUS_DROP;
}

bool Player::on_bus_message(void* message_ptr) {
  auto* message = static_cast<GstMessage*>(message_ptr);
  switch (GST_MESSAGE_TYPE(message)) {
    case GST_MESSAGE_EOS:
      set_state(State::Stopped);
      util::invoke_nothrow(eos_cb_);
      break;
    case GST_MESSAGE_WARNING: {
      GError* err = nullptr;
      gchar* debug = nullptr;
      gst_message_parse_warning(message, &err, &debug);
      std::string msg = err ? err->message : "GStreamer warning";
      if (debug && *debug) {
        msg += " (";
        msg += debug;
        msg += ")";
      }
      if (err) {
        g_error_free(err);
      }
      g_free(debug);
      util::log_warn("GStreamer: " + msg);
      break;
    }
    case GST_MESSAGE_ERROR: {
      GError* err = nullptr;
      gchar* debug = nullptr;
      gst_message_parse_error(message, &err, &debug);
      std::string msg = err ? err->message : "GStreamer error";
      if (debug && *debug) {
        msg += " (";
        msg += debug;
        msg += ")";
      }
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
  if (!bus_watch_id_) {
    GstBus* bus = gst_element_get_bus(GST_ELEMENT(pipeline_));
    if (!bus) {
      return;
    }
    auto* sync_data = new WatchContext{this, alive_};
    gst_bus_set_sync_handler(
        bus,
        +[](GstBus*, GstMessage* message, gpointer user_data) -> GstBusSyncReply {
          auto* d = static_cast<WatchContext*>(user_data);
          if (!d->alive || !d->alive->load() || !d->self) {
            return GST_BUS_PASS;
          }
          return static_cast<GstBusSyncReply>(d->self->on_bus_sync(message));
        },
        sync_data, +[](gpointer p) { delete static_cast<WatchContext*>(p); });

    auto* data = new WatchContext{this, alive_};
    bus_watch_id_ = gst_bus_add_watch(
        bus,
        +[](GstBus*, GstMessage* message, gpointer user_data) -> gboolean {
          auto* d = static_cast<WatchContext*>(user_data);
          if (!d->alive || !d->alive->load() || !d->self) {
            return FALSE;
          }
          return d->self->on_bus_message(message) ? TRUE : FALSE;
        },
        data);
    g_object_set_data_full(G_OBJECT(pipeline_), "radio-bus-data", data,
                           +[](gpointer p) { delete static_cast<WatchContext*>(p); });
    gst_object_unref(bus);
  }

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
