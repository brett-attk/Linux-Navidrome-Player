#pragma once

#include <array>
#include <string>
#include <vector>

namespace radio::audio {

inline constexpr int kEqBands = 10;

inline constexpr std::array<const char*, kEqBands> kEqBandLabels = {
    "29", "59", "119", "237", "474", "947", "1.9k", "3.8k", "7.5k", "15k",
};

struct EqPreset {
  const char* name;
  std::array<double, kEqBands> gains_db;
};

struct EqSettings {
  bool enabled = false;
  std::string preset = "Flat";
  std::array<double, kEqBands> gains_db{};
};

inline const std::vector<EqPreset>& eq_presets() {
  static const std::vector<EqPreset> presets = {
      {"Flat", {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
      {"Rock", {4.5, 3.0, -1.5, -2.5, -1.0, 1.5, 3.5, 4.5, 4.5, 4.0}},
      {"Pop", {-2.0, -1.0, 0.0, 2.0, 4.0, 4.0, 2.0, 0.0, -1.0, -2.0}},
      {"Jazz", {3.0, 2.0, 0.0, 1.0, -2.0, -2.0, 0.0, 2.0, 3.0, 4.0}},
      {"Classical", {4.0, 3.0, 2.0, 1.0, -1.0, -1.0, 0.0, 2.0, 3.0, 4.0}},
      {"Electronic", {5.0, 4.0, 1.0, 0.0, -2.0, 2.0, 1.0, 2.0, 4.0, 5.0}},
      {"Hip-Hop", {6.0, 5.0, 2.0, 1.0, -1.0, -1.0, 1.0, 2.0, 3.0, 4.0}},
      {"Metal", {4.0, 3.0, 0.0, -2.0, -3.0, 0.0, 3.0, 4.0, 4.0, 3.0}},
      {"R&B", {4.0, 5.0, 2.0, 0.0, -2.0, -1.0, 1.0, 2.0, 3.0, 4.0}},
      {"Country", {2.0, 1.0, 0.0, 0.0, 1.0, 2.0, 3.0, 2.0, 1.0, 0.0}},
      {"Folk", {2.0, 1.0, 0.0, 0.0, 1.0, 2.0, 1.0, 1.0, 2.0, 3.0}},
      {"Bass Booster", {7.0, 6.0, 4.0, 2.0, 0.0, -1.0, -2.0, -2.0, -1.0, 0.0}},
      {"Treble Booster", {0.0, -1.0, -2.0, -1.0, 0.0, 1.0, 2.0, 4.0, 5.0, 6.0}},
      {"Vocal Boost", {-2.0, -3.0, -2.0, 0.0, 3.0, 4.0, 3.0, 1.0, 0.0, -1.0}},
  };
  return presets;
}

inline const EqPreset* find_eq_preset(const std::string& name) {
  for (const auto& preset : eq_presets()) {
    if (name == preset.name) {
      return &preset;
    }
  }
  return nullptr;
}

}  // namespace radio::audio
