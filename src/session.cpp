#include "session.hpp"

#include "util/log.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace radio {
namespace {

nlohmann::json annotations_to_json(const subsonic::Annotations& a) {
  return nlohmann::json{
      {"starred", a.starred},
      {"starred_at", a.starred_at},
      {"user_rating", a.user_rating},
      {"average_rating", a.average_rating},
  };
}

subsonic::Annotations annotations_from_json(const nlohmann::json& j) {
  subsonic::Annotations a;
  if (!j.is_object()) {
    return a;
  }
  a.starred = j.value("starred", false);
  a.starred_at = j.value("starred_at", "");
  a.user_rating = j.value("user_rating", 0);
  a.average_rating = j.value("average_rating", 0.0);
  return a;
}

nlohmann::json song_to_json(const subsonic::Song& s) {
  return nlohmann::json{
      {"id", s.id},
      {"title", s.title},
      {"album", s.album},
      {"album_id", s.album_id},
      {"artist", s.artist},
      {"artist_id", s.artist_id},
      {"cover_art", s.cover_art},
      {"genre", s.genre},
      {"track", s.track},
      {"disc_number", s.disc_number},
      {"year", s.year},
      {"duration", s.duration},
      {"bit_rate", s.bit_rate},
      {"play_count", s.play_count},
      {"content_type", s.content_type},
      {"suffix", s.suffix},
      {"annotations", annotations_to_json(s.annotations)},
  };
}

subsonic::Song song_from_json(const nlohmann::json& j) {
  subsonic::Song s;
  s.id = j.value("id", "");
  s.title = j.value("title", "");
  s.album = j.value("album", "");
  s.album_id = j.value("album_id", "");
  s.artist = j.value("artist", "");
  s.artist_id = j.value("artist_id", "");
  s.cover_art = j.value("cover_art", "");
  s.genre = j.value("genre", "");
  s.track = j.value("track", 0);
  s.disc_number = j.value("disc_number", 0);
  s.year = j.value("year", 0);
  s.duration = j.value("duration", 0);
  s.bit_rate = j.value("bit_rate", 0);
  s.play_count = j.value("play_count", 0);
  s.content_type = j.value("content_type", "");
  s.suffix = j.value("suffix", "");
  if (j.contains("annotations")) {
    s.annotations = annotations_from_json(j["annotations"]);
  }
  return s;
}

const char* repeat_to_string(queue::RepeatMode mode) {
  switch (mode) {
    case queue::RepeatMode::All:
      return "all";
    case queue::RepeatMode::One:
      return "one";
    case queue::RepeatMode::Off:
    default:
      return "off";
  }
}

queue::RepeatMode repeat_from_string(const std::string& value) {
  if (value == "all") {
    return queue::RepeatMode::All;
  }
  if (value == "one") {
    return queue::RepeatMode::One;
  }
  return queue::RepeatMode::Off;
}

}  // namespace

std::string SessionStore::path() {
  const char* xdg = std::getenv("XDG_CONFIG_HOME");
  fs::path base;
  if (xdg && *xdg) {
    base = xdg;
  } else {
    const char* home = std::getenv("HOME");
    base = home ? fs::path(home) / ".config" : fs::path(".config");
  }
  return (base / "radio-player" / "session.json").string();
}

PlaybackSession SessionStore::load() {
  PlaybackSession session;
  std::ifstream in(path());
  if (!in) {
    return session;
  }
  try {
    nlohmann::json j;
    in >> j;
    session.server_url = j.value("server_url", "");
    session.current = j.value("current", -1);
    session.position_ns = j.value("position_ns", static_cast<std::int64_t>(0));
    session.shuffle = j.value("shuffle", false);
    session.repeat = repeat_from_string(j.value("repeat", "off"));
    if (j.contains("songs") && j["songs"].is_array()) {
      for (const auto& item : j["songs"]) {
        session.songs.push_back(song_from_json(item));
      }
    }
    if (j.contains("order") && j["order"].is_array()) {
      for (const auto& item : j["order"]) {
        session.order.push_back(item.get<std::size_t>());
      }
    }
  } catch (const std::exception& ex) {
    util::log_warn(std::string{"Session load failed: "} + ex.what());
    return PlaybackSession{};
  } catch (...) {
    util::log_warn("Session load failed: unknown error");
    return PlaybackSession{};
  }
  return session;
}

bool SessionStore::save(const PlaybackSession& session) {
  try {
    const auto file = path();
    std::error_code ec;
    fs::create_directories(fs::path(file).parent_path(), ec);
    if (ec) {
      return false;
    }

    nlohmann::json songs = nlohmann::json::array();
    for (const auto& song : session.songs) {
      songs.push_back(song_to_json(song));
    }
    nlohmann::json order = nlohmann::json::array();
    for (const auto idx : session.order) {
      order.push_back(idx);
    }

    nlohmann::json j{
        {"server_url", session.server_url},
        {"songs", std::move(songs)},
        {"order", std::move(order)},
        {"current", session.current},
        {"position_ns", session.position_ns},
        {"shuffle", session.shuffle},
        {"repeat", repeat_to_string(session.repeat)},
    };

    const auto tmp = file + ".tmp";
    {
      std::ofstream out(tmp);
      if (!out) {
        return false;
      }
      out << j.dump(2);
      if (!out) {
        fs::remove(tmp, ec);
        return false;
      }
    }
    fs::rename(tmp, file, ec);
    if (ec) {
      fs::remove(tmp, ec);
      return false;
    }
    return true;
  } catch (...) {
    return false;
  }
}

void SessionStore::clear() {
  std::error_code ec;
  fs::remove(path(), ec);
}

}  // namespace radio
