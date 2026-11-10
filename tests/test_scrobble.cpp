#include "minitest.hpp"

#include "util/scrobble.hpp"

using radio::util::kNsPerSec;
using radio::util::scrobble_eligible;

TEST(Scrobble, already_scrobbled_never_eligible) {
  CHECK_FALSE(scrobble_eligible(60 * kNsPerSec, 180 * kNsPerSec, true, true));
  CHECK_FALSE(scrobble_eligible(60 * kNsPerSec, 180 * kNsPerSec, false, true));
}

TEST(Scrobble, force_submits_when_not_yet_done) {
  CHECK_TRUE(scrobble_eligible(0, 180 * kNsPerSec, true, false));
}

TEST(Scrobble, long_track_uses_thirty_second_rule) {
  const std::int64_t dur = 240 * kNsPerSec;  // 4 minutes
  CHECK_FALSE(scrobble_eligible(10 * kNsPerSec, dur, false, false));
  CHECK_FALSE(scrobble_eligible(29 * kNsPerSec, dur, false, false));
  CHECK_TRUE(scrobble_eligible(30 * kNsPerSec, dur, false, false));
}

TEST(Scrobble, short_track_uses_half_with_floor) {
  // 20s track: half is 10s, floor is 4s → threshold = min(30s, max(10s, 4s)) = 10s
  const std::int64_t dur = 20 * kNsPerSec;
  CHECK_FALSE(scrobble_eligible(9 * kNsPerSec, dur, false, false));
  CHECK_TRUE(scrobble_eligible(10 * kNsPerSec, dur, false, false));
}

TEST(Scrobble, tiny_clip_uses_four_second_floor) {
  // 6s track: half is 3s, floor lifts threshold to 4s
  const std::int64_t dur = 6 * kNsPerSec;
  CHECK_FALSE(scrobble_eligible(3 * kNsPerSec, dur, false, false));
  CHECK_TRUE(scrobble_eligible(4 * kNsPerSec, dur, false, false));
}

TEST(Scrobble, unknown_duration_falls_back_to_thirty_seconds) {
  CHECK_FALSE(scrobble_eligible(29 * kNsPerSec, 0, false, false));
  CHECK_TRUE(scrobble_eligible(30 * kNsPerSec, 0, false, false));
}
