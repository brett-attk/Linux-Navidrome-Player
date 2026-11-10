#pragma once

#include <gtk/gtk.h>

#include <array>
#include <random>
#include <vector>

namespace radio::ui {

// Home-sidebar beat playground: randomly cycles visual effects that punch to
// the music and shift color while a song is playing.
class BeatStage {
 public:
  BeatStage();
  ~BeatStage();

  BeatStage(const BeatStage&) = delete;
  BeatStage& operator=(const BeatStage&) = delete;

  GtkWidget* widget() const { return GTK_WIDGET(area_); }

  void set_active(bool active);
  void on_levels(float bass, float overall);
  bool tick();
  void detach();

 private:
  enum class Effect {
    Fireworks,
    PulseRings,
    NeonBars,
    Starburst,
    Fountain,
    Orbits,
    Rain,
    Tunnel,
    ColorSnow,
    StarField,
    Count,
  };

  struct Particle {
    float x = 0;
    float y = 0;
    float vx = 0;
    float vy = 0;
    float life = 0;
    float max_life = 1;
    float r = 1;
    float g = 1;
    float b = 1;
    float size = 2;
  };

  struct Rocket {
    float x = 0;
    float y = 0;
    float vx = 0;
    float vy = 0;
    float fuse = 0;
    float r = 1;
    float g = 1;
    float b = 1;
  };

  struct Orbiter {
    float angle = 0;
    float radius = 40;
    float speed = 1;
    float size = 6;
  };

  // 3D-ish warp stars: x/y are offsets from center, z is depth.
  struct Star {
    float x = 0;
    float y = 0;
    float z = 1;
    float r = 1;
    float g = 1;
    float b = 1;
    float size = 1.5f;
  };

  void draw(cairo_t* cr, int width, int height);
  void clear_transient();
  void reshuffle_effect_deck();
  void pick_next_effect();
  void color_from_hue(float hue, float sat, float val, float& r, float& g,
                      float& b) const;
  void current_palette(float& r, float& g, float& b, float& r2, float& g2,
                       float& b2) const;

  void on_beat_hit(int width, int height, float energy);
  void launch_rocket(int width, int height, float energy);
  void explode(const Rocket& rocket);
  void spawn_burst(int width, int height, float energy);
  void spawn_fountain(int width, int height, float energy);
  void spawn_rain(int width, int height, float energy);
  void spawn_snow(int width, int height, float energy, int count);
  void seed_starfield(int count);
  void reset_star(Star& star, bool scatter_depth);

  void tick_fireworks();
  void tick_fountain();
  void tick_rain();
  void tick_snow(int width, int height);
  void tick_starfield(int width, int height);
  void tick_orbits(float energy);

  void draw_backdrop(cairo_t* cr, int width, int height);
  void draw_fireworks(cairo_t* cr);
  void draw_pulse_rings(cairo_t* cr, int width, int height);
  void draw_neon_bars(cairo_t* cr, int width, int height);
  void draw_starburst(cairo_t* cr, int width, int height);
  void draw_fountain(cairo_t* cr);
  void draw_orbits(cairo_t* cr, int width, int height);
  void draw_rain(cairo_t* cr);
  void draw_tunnel(cairo_t* cr, int width, int height);
  void draw_color_snow(cairo_t* cr);
  void draw_starfield(cairo_t* cr, int width, int height);
  void draw_idle(cairo_t* cr, int width, int height);

  GtkDrawingArea* area_ = nullptr;
  bool active_ = false;
  Effect effect_ = Effect::Fireworks;
  float bass_ = 0.0f;
  float overall_ = 0.0f;
  float bass_ema_ = 0.0f;
  float cooldown_ = 0.0f;
  float hue_ = 0.72f;
  float effect_timer_ = 0.0f;
  float effect_duration_ = 12.0f;
  float pulse_ = 0.0f;
  float time_ = 0.0f;
  std::array<float, 16> bar_levels_{};
  std::array<Effect, static_cast<std::size_t>(Effect::Count)> effect_deck_{};
  std::size_t effect_deck_pos_ =
      static_cast<std::size_t>(Effect::Count);  // force reshuffle
  std::vector<Rocket> rockets_;
  std::vector<Particle> particles_;
  std::vector<Orbiter> orbiters_;
  std::vector<Star> stars_;
  float snow_spawn_ = 0.0f;
  std::mt19937 rng_{std::random_device{}()};
};

}  // namespace radio::ui
