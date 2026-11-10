#include "minitest.hpp"

#include "audio/beat_math.hpp"

#include <vector>

TEST(BeatMath, db_to_level_clamps_and_scales) {
  CHECK_NEAR(radio::audio::db_to_level(0.0f), 0.9, 0.0001);
  CHECK_NEAR(radio::audio::db_to_level(-80.0f), 0.0, 0.0001);
  CHECK_NEAR(radio::audio::db_to_level(-200.0f), 0.0, 0.0001);
  CHECK_TRUE(radio::audio::db_to_level(-20.0f) >
             radio::audio::db_to_level(-40.0f));
}

TEST(BeatMath, levels_from_magnitudes_empty) {
  const auto levels = radio::audio::levels_from_magnitudes({});
  CHECK_NEAR(levels.bass, 0.0, 0.0001);
  CHECK_NEAR(levels.overall, 0.0, 0.0001);
}

TEST(BeatMath, levels_from_magnitudes_bass_vs_overall) {
  // 16 bands: first 2 are bass (n/8). Put loud energy only in bass.
  std::vector<float> mags(16, -80.0f);
  mags[0] = -6.0f;
  mags[1] = -6.0f;
  const auto levels = radio::audio::levels_from_magnitudes(mags);
  CHECK_TRUE(levels.bass > 0.4f);
  CHECK_TRUE(levels.overall > 0.0f);
  CHECK_TRUE(levels.bass > levels.overall);
}

TEST(BeatMath, levels_from_magnitudes_overall_spread) {
  std::vector<float> mags(16, -12.0f);
  const auto levels = radio::audio::levels_from_magnitudes(mags);
  CHECK_NEAR(levels.bass, levels.overall, 0.0001);
  CHECK_TRUE(levels.overall > 0.3f);
}

TEST(BeatOnset, ignores_quiet_noise) {
  radio::audio::BeatOnsetState state;
  const auto a = radio::audio::detect_beat_onset(state, 0.05f, 0.05f, 0.016f);
  CHECK_FALSE(a.hit);
  const auto b = radio::audio::detect_beat_onset(state, 0.08f, 0.08f, 0.016f);
  CHECK_FALSE(b.hit);
}

TEST(BeatOnset, fires_on_clear_kick_then_cools_down) {
  radio::audio::BeatOnsetState state;
  // Establish a quiet baseline.
  for (int i = 0; i < 8; ++i) {
    (void)radio::audio::detect_beat_onset(state, 0.15f, 0.12f, 0.016f);
  }
  const auto hit =
      radio::audio::detect_beat_onset(state, 0.85f, 0.55f, 0.016f);
  CHECK_TRUE(hit.hit);
  CHECK_TRUE(hit.energy >= 0.35f);
  CHECK_TRUE(state.cooldown > 0.0f);

  // Immediate second sample should be suppressed by cooldown.
  const auto suppressed =
      radio::audio::detect_beat_onset(state, 0.90f, 0.60f, 0.0f);
  CHECK_FALSE(suppressed.hit);

  // Drain cooldown, settle the envelope, then fire another kick.
  (void)radio::audio::detect_beat_onset(state, 0.15f, 0.12f, 1.0f);
  for (int i = 0; i < 8; ++i) {
    (void)radio::audio::detect_beat_onset(state, 0.15f, 0.12f, 0.016f);
  }
  const auto again =
      radio::audio::detect_beat_onset(state, 0.88f, 0.55f, 0.016f);
  CHECK_TRUE(again.hit);
}
