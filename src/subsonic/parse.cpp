#include "subsonic/parse.hpp"

namespace radio::subsonic {

Annotations parse_annotations(const nlohmann::json& j) {
  Annotations a;
  if (j.contains("starred") && !j["starred"].is_null()) {
    a.starred = true;
    if (j["starred"].is_string()) {
      a.starred_at = j["starred"].get<std::string>();
    } else {
      a.starred_at = j["starred"].dump();
    }
  }
  a.user_rating = j.value("userRating", 0);
  a.average_rating = j.value("averageRating", 0.0);
  return a;
}

Song parse_song(const nlohmann::json& j) {
  Song s;
  s.id = j.value("id", "");
  s.title = j.value("title", "");
  s.album = j.value("album", "");
  s.album_id = j.value("albumId", "");
  s.artist = j.value("artist", "");
  s.artist_id = j.value("artistId", "");
  s.cover_art = j.value("coverArt", "");
  s.genre = j.value("genre", "");
  s.track = j.value("track", 0);
  s.disc_number = j.value("discNumber", 0);
  s.year = j.value("year", 0);
  s.duration = j.value("duration", 0);
  s.bit_rate = j.value("bitRate", 0);
  s.play_count = j.value("playCount", 0);
  s.content_type = j.value("contentType", "");
  s.suffix = j.value("suffix", "");
  s.annotations = parse_annotations(j);
  return s;
}

Album parse_album(const nlohmann::json& j) {
  Album a;
  a.id = j.value("id", "");
  a.name = j.value("name", "");
  a.artist = j.value("artist", "");
  a.artist_id = j.value("artistId", "");
  a.cover_art = j.value("coverArt", "");
  a.year = j.value("year", 0);
  a.song_count = j.value("songCount", 0);
  a.duration = j.value("duration", 0);
  a.play_count = j.value("playCount", 0);
  a.annotations = parse_annotations(j);
  return a;
}

Artist parse_artist(const nlohmann::json& j) {
  Artist a;
  a.id = j.value("id", "");
  a.name = j.value("name", "");
  a.album_count = j.value("albumCount", 0);
  a.play_count = j.value("playCount", 0);
  a.cover_art = j.value("coverArt", "");
  a.annotations = parse_annotations(j);
  return a;
}

std::optional<ClientError> parse_api_error(const nlohmann::json& root) {
  if (!root.contains("subsonic-response")) {
    return ClientError{"Invalid Subsonic response", -1};
  }
  const auto& resp = root["subsonic-response"];
  if (resp.value("status", "") == "ok") {
    return std::nullopt;
  }
  ClientError err;
  if (resp.contains("error")) {
    err.code = resp["error"].value("code", 0);
    err.message = resp["error"].value("message", "Unknown API error");
  } else {
    err.message = "Subsonic request failed";
  }
  return err;
}

}  // namespace radio::subsonic
