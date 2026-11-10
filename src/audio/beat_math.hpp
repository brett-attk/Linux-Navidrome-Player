#pragma once

#include "audio/player.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>

namespace radio::audio {

inline constexpr int kSpectrumThresholdDb = -80;

// Map a spectrum magnitude (dB) into a 0..1 energy level.
inline float db_to_level(float db,
                         int threshold_db = kSpectrumThresholdDb) {
  const float clamped =
      std::clamp(db, static_cast<float>(threshold_db), 0.0f);
  const float norm =
      (clamped - static_cast<float>(threshold_db)) /
      static_cast<float>(-threshold_db);
  return std::clamp(std::pow(norm, 0.75f) * 0.9f, 0.0f, 1.0f);
}

// Aggregate FFT magnitudes into bass + overall beat levels.
inline BeatLevels levels_from_magnitudes(std::span<const float> magnitudes_db,
                                         int threshold_db = kSpectrumThresholdDb) {
  BeatLevels levels;
  if (magnitudes_db.empty()) {
    return levels;
  }

  const std::size_t n = magnitudes_db.size();
  const std::size_t bass_n = std::max<std::size_t>(1, n / 8);
  float bass_sum = 0.0f;
  float all_sum = 0.0f;
  for (std::size_t i = 0; i < n; ++i) {
    const float level = db_to_level(magnitudes_db[i], threshold_db);
    all_sum += level;
    if (i < bass_n) {
      bass_sum += level;
    }
  }

  levels.bass = std::clamp(bass_sum / static_cast<float>(bass_n), 0.0f, 1.0f);
  levels.overall = std::clamp(all_sum / static_cast<float>(n), 0.0f, 1.0f);
  return levels;
}

struct BeatOnsetState {
  float bass_ema = 0.0f;
  float cooldown = 0.0f;
};

struct BeatOnsetResult {
  bool hit = false;
  float energy = 0.0f;
  float pulse = 0.0f;
};

// Onset detector shared by the Home beat stage. Tuned to favor clear kicks
// over every small transient.
inline BeatOnsetResult detect_beat_onset(BeatOnsetState& state, float bass,
                                         float overall, float dt) {
  BeatOnsetResult out;
  state.bass_ema = state.bass_ema * 0.78f + bass * 0.22f;
  out.pulse = bass * 0.90f + overall * 0.35f;
  state.cooldown = std::max(0.0f, state.cooldown - std::max(0.0f, dt));

  const float spike = bass - state.bass_ema;
  if (state.cooldown <= 0.0f && spike > 0.10f && bass > 0.24f) {
    out.hit = true;
    out.energy =
        std::clamp(0.4f + spike * 1.8f + overall * 0.45f, 0.35f, 1.0f);
    state.cooldown = 0.14f + (1.0f - out.energy) * 0.16f;
  }
  return out;
}

}  // namespace radio::audio
