#pragma once

#include <algorithm>
#include <cstdint>

namespace radio::util {

inline constexpr std::int64_t kNsPerSec = 1000000000LL;

// Subsonic/Navidrome guideline: submit after ~30s or half the track
// (whichever comes first), with a small floor so short clips still count
// when forced at end-of-song.
inline bool scrobble_eligible(std::int64_t position_ns, std::int64_t duration_ns,
                              bool force, bool already_scrobbled) {
  if (already_scrobbled) {
    return false;
  }
  if (force) {
    return true;
  }
  if (duration_ns > 0) {
    const std::int64_t half = duration_ns / 2;
    const std::int64_t thirty = 30 * kNsPerSec;
    const std::int64_t threshold =
        std::min(thirty, std::max(half, 4 * kNsPerSec));
    return position_ns >= threshold;
  }
  return position_ns >= 30 * kNsPerSec;
}

}  // namespace radio::util
