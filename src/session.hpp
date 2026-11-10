#pragma once

#include "queue/play_queue.hpp"
#include "subsonic/models.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace radio {

struct PlaybackSession {
  std::string server_url;
  std::vector<subsonic::Song> songs;
  std::vector<std::size_t> order;
  int current = -1;
  std::int64_t position_ns = 0;
  bool shuffle = false;
  queue::RepeatMode repeat = queue::RepeatMode::Off;
};

class SessionStore {
 public:
  static std::string path();
  static PlaybackSession load();
  static bool save(const PlaybackSession& session);
  static void clear();
};

}  // namespace radio
