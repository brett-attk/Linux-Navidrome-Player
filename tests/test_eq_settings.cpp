#include "minitest.hpp"

#include "audio/eq_settings.hpp"

TEST(EqSettings, presets_non_empty) {
  const auto& presets = radio::audio::eq_presets();
  CHECK_TRUE(presets.size() >= 5u);
  CHECK_EQ(std::string(presets.front().name), std::string("Flat"));
}

TEST(EqSettings, find_preset) {
  const auto* rock = radio::audio::find_eq_preset("Rock");
  CHECK(rock != nullptr);
  CHECK_EQ(rock->gains_db.size(), static_cast<std::size_t>(radio::audio::kEqBands));
  CHECK(radio::audio::find_eq_preset("NotARealPreset") == nullptr);
}

TEST(EqSettings, flat_is_zero) {
  const auto* flat = radio::audio::find_eq_preset("Flat");
  CHECK(flat != nullptr);
  for (double g : flat->gains_db) {
    CHECK_NEAR(g, 0.0, 0.0001);
  }
}

TEST(EqSettings, band_labels_count) {
  CHECK_EQ(radio::audio::kEqBandLabels.size(),
           static_cast<std::size_t>(radio::audio::kEqBands));
}
