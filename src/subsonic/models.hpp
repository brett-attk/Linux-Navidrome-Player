#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace radio::subsonic {

struct ClientError {
  std::string message;
  int code = 0;
};

struct Annotations {
  bool starred = false;
  std::string starred_at;  // timestamp from server when starred
  int user_rating = 0;     // 0–5, per-user
  double average_rating = 0.0;
};

struct Artist {
  std::string id;
  std::string name;
  int album_count = 0;
  int play_count = 0;
  std::string cover_art;
  Annotations annotations;
};

struct Album {
  std::string id;
  std::string name;
  std::string artist;
  std::string artist_id;
  std::string cover_art;
  int year = 0;
  int song_count = 0;
  int duration = 0;
  int play_count = 0;
  Annotations annotations;
};

struct Song {
  std::string id;
  std::string title;
  std::string album;
  std::string album_id;
  std::string artist;
  std::string artist_id;
  std::string cover_art;
  std::string genre;
  int track = 0;
  int disc_number = 0;
  int year = 0;
  int duration = 0;
  int bit_rate = 0;
  int play_count = 0;
  std::string content_type;
  std::string suffix;
  Annotations annotations;
};

struct Starred {
  std::vector<Artist> artists;
  std::vector<Album> albums;
  std::vector<Song> songs;
};

}  // namespace radio::subsonic
