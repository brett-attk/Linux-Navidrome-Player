#include "ui/beat_stage.hpp"

#include "audio/beat_math.hpp"

#include <algorithm>
#include <cmath>

namespace radio::ui {
namespace {

constexpr float kDt = 1.0f / 60.0f;
constexpr float kTau = 6.2831853f;

}  // namespace

BeatStage::BeatStage() {
  area_ = GTK_DRAWING_AREA(gtk_drawing_area_new());
  gtk_widget_set_hexpand(GTK_WIDGET(area_), TRUE);
  gtk_widget_set_vexpand(GTK_WIDGET(area_), TRUE);
  gtk_widget_add_css_class(GTK_WIDGET(area_), "beat-stage");
  gtk_widget_set_tooltip_text(
      GTK_WIDGET(area_),
      "Random beat visuals — they change look and color while you listen");
  gtk_drawing_area_set_draw_func(
      area_,
      +[](GtkDrawingArea*, cairo_t* cr, int width, int height, gpointer self) {
        static_cast<BeatStage*>(self)->draw(cr, width, height);
      },
      this, nullptr);

  for (int i = 0; i < 5; ++i) {
    Orbiter o;
    o.angle = i * (kTau / 5.0f);
    o.radius = 28.0f + i * 10.0f;
    o.speed = 0.8f + i * 0.25f;
    o.size = 5.0f + (i % 3);
    orbiters_.push_back(o);
  }
  pick_next_effect();
}

BeatStage::~BeatStage() { detach(); }

void BeatStage::detach() {
  if (GTK_IS_DRAWING_AREA(area_)) {
    gtk_drawing_area_set_draw_func(area_, nullptr, nullptr, nullptr);
  }
  area_ = nullptr;
  active_ = false;
  clear_transient();
}

void BeatStage::clear_transient() {
  rockets_.clear();
  particles_.clear();
  stars_.clear();
  cooldown_ = 0.0f;
  pulse_ = 0.0f;
  snow_spawn_ = 0.0f;
  bar_levels_.fill(0.0f);
}

void BeatStage::set_active(bool active) {
  active_ = active;
  if (!active) {
    clear_transient();
    if (GTK_IS_DRAWING_AREA(area_)) {
      gtk_widget_queue_draw(GTK_WIDGET(area_));
    }
  } else if (effect_timer_ <= 0.0f) {
    pick_next_effect();
  }
}

void BeatStage::reshuffle_effect_deck() {
  const int count = static_cast<int>(Effect::Count);
  for (int i = 0; i < count; ++i) {
    effect_deck_[static_cast<std::size_t>(i)] = static_cast<Effect>(i);
  }
  std::shuffle(effect_deck_.begin(), effect_deck_.end(), rng_);
  // Don't open a new deck on the effect we just finished.
  if (count > 1 && effect_deck_[0] == effect_) {
    std::uniform_int_distribution<int> swap_to(1, count - 1);
    std::swap(effect_deck_[0],
              effect_deck_[static_cast<std::size_t>(swap_to(rng_))]);
  }
  effect_deck_pos_ = 0;
}

void BeatStage::pick_next_effect() {
  if (effect_deck_pos_ >= effect_deck_.size()) {
    reshuffle_effect_deck();
  }
  effect_ = effect_deck_[effect_deck_pos_++];
  effect_duration_ =
      std::uniform_real_distribution<float>(9.0f, 18.0f)(rng_);
  effect_timer_ = effect_duration_;
  clear_transient();
  hue_ = std::fmod(
      hue_ + std::uniform_real_distribution<float>(0.12f, 0.37f)(rng_), 1.0f);

  if (effect_ == Effect::StarField) {
    seed_starfield(110);
  } else if (effect_ == Effect::ColorSnow) {
    const int width =
        GTK_IS_DRAWING_AREA(area_) ? gtk_widget_get_width(GTK_WIDGET(area_)) : 0;
    const int height = GTK_IS_DRAWING_AREA(area_)
                           ? gtk_widget_get_height(GTK_WIDGET(area_))
                           : 0;
    if (width > 8 && height > 8) {
      spawn_snow(width, height, 0.45f, 48);
    }
  }
}

void BeatStage::color_from_hue(float hue, float sat, float val, float& r,
                               float& g, float& b) const {
  hue = std::fmod(std::fmod(hue, 1.0f) + 1.0f, 1.0f);
  sat = std::clamp(sat, 0.0f, 1.0f);
  val = std::clamp(val, 0.0f, 1.0f);
  const float c = val * sat;
  const float x = c * (1.0f - std::fabs(std::fmod(hue * 6.0f, 2.0f) - 1.0f));
  const float m = val - c;
  float rp = 0;
  float gp = 0;
  float bp = 0;
  const int sector = static_cast<int>(hue * 6.0f) % 6;
  switch (sector) {
    case 0:
      rp = c;
      gp = x;
      break;
    case 1:
      rp = x;
      gp = c;
      break;
    case 2:
      gp = c;
      bp = x;
      break;
    case 3:
      gp = x;
      bp = c;
      break;
    case 4:
      rp = x;
      bp = c;
      break;
    default:
      rp = c;
      bp = x;
      break;
  }
  r = rp + m;
  g = gp + m;
  b = bp + m;
}

void BeatStage::current_palette(float& r, float& g, float& b, float& r2,
                                float& g2, float& b2) const {
  color_from_hue(hue_, 0.9f, 1.0f, r, g, b);
  color_from_hue(std::fmod(hue_ + 0.18f, 1.0f), 0.85f, 0.95f, r2, g2, b2);
}

void BeatStage::on_levels(float bass, float overall) {
  if (!active_) {
    return;
  }
  bass_ = bass;
  overall_ = overall;
  audio::BeatOnsetState onset{bass_ema_, cooldown_};
  const auto hit = audio::detect_beat_onset(onset, bass, overall, 0.0f);
  bass_ema_ = onset.bass_ema;
  cooldown_ = onset.cooldown;
  pulse_ = std::max(pulse_, hit.pulse);
  hue_ = std::fmod(hue_ + 0.0025f + overall * 0.01f + bass * 0.004f, 1.0f);

  for (std::size_t i = 0; i < bar_levels_.size(); ++i) {
    const float t = static_cast<float>(i) / (bar_levels_.size() - 1);
    const float target =
        std::clamp(overall * (0.35f + 0.65f * (1.0f - std::fabs(t - bass))) +
                       bass * (i < 4 ? 0.55f : 0.15f),
                   0.0f, 1.0f);
    if (target > bar_levels_[i]) {
      bar_levels_[i] = target;
    } else {
      bar_levels_[i] = bar_levels_[i] * 0.78f + target * 0.22f;
    }
  }

  if (hit.hit) {
    const int width =
        GTK_IS_DRAWING_AREA(area_) ? gtk_widget_get_width(GTK_WIDGET(area_)) : 0;
    const int height = GTK_IS_DRAWING_AREA(area_)
                           ? gtk_widget_get_height(GTK_WIDGET(area_))
                           : 0;
    if (width > 8 && height > 8) {
      on_beat_hit(width, height, hit.energy);
    }
  }
}

void BeatStage::on_beat_hit(int width, int height, float energy) {
  pulse_ = std::max(pulse_, energy);
  switch (effect_) {
    case Effect::Fireworks:
      // Instant burst on the hit; rockets are a secondary delayed bloom.
      spawn_burst(width, height, energy);
      if (energy > 0.55f) {
        launch_rocket(width, height, energy);
      }
      if (energy > 0.85f) {
        launch_rocket(width, height, energy * 0.8f);
      }
      break;
    case Effect::Fountain:
      spawn_fountain(width, height, energy);
      break;
    case Effect::Rain:
      spawn_rain(width, height, energy);
      break;
    case Effect::ColorSnow:
      spawn_snow(width, height, energy, 10 + static_cast<int>(energy * 22.0f));
      break;
    case Effect::StarField:
      // Warp boost is driven by pulse_ in tick/draw; sprinkle fresh stars.
      for (int i = 0; i < 6 + static_cast<int>(energy * 10.0f); ++i) {
        Star star;
        reset_star(star, true);
        stars_.push_back(star);
      }
      break;
    case Effect::PulseRings:
    case Effect::NeonBars:
    case Effect::Starburst:
    case Effect::Orbits:
    case Effect::Tunnel:
    case Effect::Count:
      break;
  }
}

void BeatStage::launch_rocket(int width, int height, float energy) {
  float r = 1;
  float g = 1;
  float b = 1;
  float r2 = 1;
  float g2 = 1;
  float b2 = 1;
  current_palette(r, g, b, r2, g2, b2);
  const bool alt = std::uniform_int_distribution<int>(0, 1)(rng_) == 1;

  Rocket rocket;
  rocket.x = std::uniform_real_distribution<float>(width * 0.12f,
                                                   width * 0.88f)(rng_);
  // Start mid-low and explode quickly so secondary blooms stay near the beat.
  rocket.y = height * 0.72f;
  rocket.vx = std::uniform_real_distribution<float>(-40.0f, 40.0f)(rng_);
  rocket.vy = -(320.0f + energy * 220.0f);
  rocket.fuse =
      std::uniform_real_distribution<float>(0.28f, 0.48f)(rng_) * height;
  rocket.r = alt ? r2 : r;
  rocket.g = alt ? g2 : g;
  rocket.b = alt ? b2 : b;
  rockets_.push_back(rocket);
}

void BeatStage::explode(const Rocket& rocket) {
  std::uniform_real_distribution<float> angle(0.0f, kTau);
  std::uniform_real_distribution<float> speed(35.0f, 230.0f);
  std::uniform_real_distribution<float> life(0.4f, 1.15f);
  const int count = 26 + static_cast<int>(speed(rng_) / 14.0f);
  for (int i = 0; i < count; ++i) {
    const float a = angle(rng_);
    const float s = speed(rng_);
    Particle p;
    p.x = rocket.x;
    p.y = rocket.y;
    p.vx = std::cos(a) * s;
    p.vy = std::sin(a) * s;
    p.max_life = life(rng_);
    p.life = p.max_life;
    p.r = rocket.r;
    p.g = rocket.g;
    p.b = rocket.b;
    p.size = std::uniform_real_distribution<float>(1.4f, 3.1f)(rng_);
    particles_.push_back(p);
  }
}

void BeatStage::spawn_burst(int width, int height, float energy) {
  float r = 1;
  float g = 1;
  float b = 1;
  float r2 = 1;
  float g2 = 1;
  float b2 = 1;
  current_palette(r, g, b, r2, g2, b2);
  Rocket flash;
  flash.x = std::uniform_real_distribution<float>(width * 0.18f,
                                                  width * 0.82f)(rng_);
  flash.y = std::uniform_real_distribution<float>(height * 0.22f,
                                                  height * 0.62f)(rng_);
  const bool alt = std::uniform_int_distribution<int>(0, 1)(rng_) == 1;
  flash.r = alt ? r2 : r;
  flash.g = alt ? g2 : g;
  flash.b = alt ? b2 : b;
  explode(flash);
  if (energy > 0.75f) {
    Rocket flash2 = flash;
    flash2.x += std::uniform_real_distribution<float>(-60.0f, 60.0f)(rng_);
    flash2.y += std::uniform_real_distribution<float>(-40.0f, 40.0f)(rng_);
    flash2.r = alt ? r : r2;
    flash2.g = alt ? g : g2;
    flash2.b = alt ? b : b2;
    explode(flash2);
  }
}

void BeatStage::spawn_fountain(int width, int height, float energy) {
  float r = 1;
  float g = 1;
  float b = 1;
  float r2 = 1;
  float g2 = 1;
  float b2 = 1;
  current_palette(r, g, b, r2, g2, b2);
  const int count = 14 + static_cast<int>(energy * 18.0f);
  for (int i = 0; i < count; ++i) {
    Particle p;
    p.x = width * 0.5f + std::uniform_real_distribution<float>(-18.0f, 18.0f)(rng_);
    p.y = height - 6.0f;
    p.vx = std::uniform_real_distribution<float>(-70.0f, 70.0f)(rng_);
    p.vy = -(160.0f + energy * 220.0f +
             std::uniform_real_distribution<float>(0.0f, 80.0f)(rng_));
    p.max_life = std::uniform_real_distribution<float>(0.5f, 1.1f)(rng_);
    p.life = p.max_life;
    const bool alt = (i & 1) == 0;
    p.r = alt ? r : r2;
    p.g = alt ? g : g2;
    p.b = alt ? b : b2;
    p.size = std::uniform_real_distribution<float>(1.6f, 3.4f)(rng_);
    particles_.push_back(p);
  }
}

void BeatStage::spawn_rain(int width, int height, float energy) {
  float r = 1;
  float g = 1;
  float b = 1;
  float r2 = 1;
  float g2 = 1;
  float b2 = 1;
  current_palette(r, g, b, r2, g2, b2);
  const int count = 10 + static_cast<int>(energy * 20.0f);
  for (int i = 0; i < count; ++i) {
    Particle p;
    p.x = std::uniform_real_distribution<float>(0.0f, static_cast<float>(width))(
        rng_);
    p.y = -8.0f;
    p.vx = std::uniform_real_distribution<float>(-20.0f, 20.0f)(rng_);
    p.vy = 180.0f + energy * 260.0f;
    p.max_life = std::uniform_real_distribution<float>(0.6f, 1.3f)(rng_);
    p.life = p.max_life;
    const bool alt = (i & 1) == 0;
    p.r = alt ? r : r2;
    p.g = alt ? g : g2;
    p.b = alt ? b : b2;
    p.size = std::uniform_real_distribution<float>(1.2f, 2.6f)(rng_);
    particles_.push_back(p);
  }
  (void)height;
}

void BeatStage::spawn_snow(int width, int height, float energy, int count) {
  float r = 1;
  float g = 1;
  float b = 1;
  float r2 = 1;
  float g2 = 1;
  float b2 = 1;
  current_palette(r, g, b, r2, g2, b2);
  for (int i = 0; i < count; ++i) {
    Particle p;
    p.x = std::uniform_real_distribution<float>(0.0f, static_cast<float>(width))(
        rng_);
    p.y = std::uniform_real_distribution<float>(-height * 0.15f,
                                                height * 0.35f)(rng_);
    p.vx = std::uniform_real_distribution<float>(-28.0f, 28.0f)(rng_);
    p.vy = 28.0f + energy * 70.0f +
           std::uniform_real_distribution<float>(0.0f, 45.0f)(rng_);
    p.max_life = std::uniform_real_distribution<float>(2.2f, 4.8f)(rng_);
    p.life = p.max_life;
    // Spread flake hues across the current palette + neighbors.
    float fr = 1;
    float fg = 1;
    float fb = 1;
    color_from_hue(std::fmod(hue_ + (i % 5) * 0.08f, 1.0f), 0.75f, 1.0f, fr,
                   fg, fb);
    if ((i % 3) == 0) {
      p.r = r2;
      p.g = g2;
      p.b = b2;
    } else if ((i & 1) == 0) {
      p.r = fr;
      p.g = fg;
      p.b = fb;
    } else {
      p.r = r;
      p.g = g;
      p.b = b;
    }
    p.size = std::uniform_real_distribution<float>(2.0f, 5.2f)(rng_);
    particles_.push_back(p);
  }
}

void BeatStage::reset_star(Star& star, bool scatter_depth) {
  star.x = std::uniform_real_distribution<float>(-0.95f, 0.95f)(rng_);
  star.y = std::uniform_real_distribution<float>(-0.95f, 0.95f)(rng_);
  star.z = scatter_depth
               ? std::uniform_real_distribution<float>(0.12f, 1.0f)(rng_)
               : 1.0f;
  float r = 1;
  float g = 1;
  float b = 1;
  color_from_hue(
      std::fmod(hue_ + std::uniform_real_distribution<float>(0.0f, 0.35f)(rng_),
                1.0f),
      0.55f, 1.0f, r, g, b);
  star.r = r;
  star.g = g;
  star.b = b;
  star.size = std::uniform_real_distribution<float>(1.1f, 2.6f)(rng_);
}

void BeatStage::seed_starfield(int count) {
  stars_.clear();
  stars_.reserve(static_cast<std::size_t>(count));
  for (int i = 0; i < count; ++i) {
    Star star;
    reset_star(star, true);
    stars_.push_back(star);
  }
}

void BeatStage::tick_fireworks() {
  for (auto& rocket : rockets_) {
    rocket.x += rocket.vx * kDt;
    rocket.y += rocket.vy * kDt;
    rocket.vy += 95.0f * kDt;
    Particle trail;
    trail.x = rocket.x;
    trail.y = rocket.y;
    trail.vx = -rocket.vx * 0.08f;
    trail.vy = 18.0f;
    trail.max_life = 0.22f;
    trail.life = trail.max_life;
    trail.r = rocket.r;
    trail.g = rocket.g;
    trail.b = rocket.b;
    trail.size = 1.3f;
    particles_.push_back(trail);
  }

  std::vector<Rocket> survivors;
  survivors.reserve(rockets_.size());
  for (const auto& rocket : rockets_) {
    if (rocket.y <= rocket.fuse || rocket.vy >= -18.0f) {
      explode(rocket);
    } else {
      survivors.push_back(rocket);
    }
  }
  rockets_.swap(survivors);

  for (auto& p : particles_) {
    p.x += p.vx * kDt;
    p.y += p.vy * kDt;
    p.vx *= 0.985f;
    p.vy = p.vy * 0.985f + 55.0f * kDt;
    p.life -= kDt;
  }
}

void BeatStage::tick_fountain() {
  for (auto& p : particles_) {
    p.x += p.vx * kDt;
    p.y += p.vy * kDt;
    p.vx *= 0.99f;
    p.vy = p.vy * 0.99f + 140.0f * kDt;
    p.life -= kDt;
  }
}

void BeatStage::tick_rain() {
  for (auto& p : particles_) {
    p.x += p.vx * kDt;
    p.y += p.vy * kDt;
    p.life -= kDt;
  }
}

void BeatStage::tick_snow(int width, int height) {
  snow_spawn_ -= kDt;
  if (active_ && snow_spawn_ <= 0.0f && width > 8 && height > 8) {
    spawn_snow(width, height, std::max(0.25f, overall_), 3);
    snow_spawn_ = 0.08f + (1.0f - std::clamp(overall_, 0.0f, 1.0f)) * 0.10f;
  }

  const float gust = pulse_ * 55.0f;
  for (auto& p : particles_) {
    p.x += (p.vx + std::sin(time_ * 1.7f + p.y * 0.02f) * 18.0f) * kDt;
    p.y += (p.vy + gust) * kDt;
    p.life -= kDt;
    if (p.x < -12.0f) {
      p.x = static_cast<float>(width) + 8.0f;
    } else if (p.x > width + 12.0f) {
      p.x = -8.0f;
    }
    if (p.y > height + 12.0f) {
      p.life = 0.0f;
    }
  }
}

void BeatStage::tick_starfield(int width, int height) {
  (void)width;
  (void)height;
  if (stars_.empty() && active_) {
    seed_starfield(110);
  }
  const float speed = 0.55f + pulse_ * 1.8f + overall_ * 0.45f;
  for (auto& star : stars_) {
    star.z -= speed * kDt;
    if (star.z <= 0.05f) {
      reset_star(star, false);
    }
  }
  if (stars_.size() > 160) {
    stars_.resize(160);
  }
}

void BeatStage::tick_orbits(float energy) {
  for (auto& o : orbiters_) {
    o.speed = 0.7f + energy * 2.8f + o.radius * 0.01f;
    o.angle += o.speed * kDt;
    o.radius = std::clamp(o.radius + (energy - 0.4f) * 12.0f * kDt, 18.0f, 110.0f);
  }
}

bool BeatStage::tick() {
  if (!GTK_IS_DRAWING_AREA(area_)) {
    return false;
  }
  if (!active_ && rockets_.empty() && particles_.empty() && stars_.empty() &&
      pulse_ <= 0.01f) {
    return false;
  }

  time_ += kDt;
  cooldown_ = std::max(0.0f, cooldown_ - kDt);
  pulse_ = std::max(0.0f, pulse_ * 0.86f - 0.012f);

  if (active_) {
    effect_timer_ -= kDt;
    if (effect_timer_ <= 0.0f) {
      pick_next_effect();
    }
  }

  const int width =
      GTK_IS_DRAWING_AREA(area_) ? gtk_widget_get_width(GTK_WIDGET(area_)) : 0;
  const int height =
      GTK_IS_DRAWING_AREA(area_) ? gtk_widget_get_height(GTK_WIDGET(area_)) : 0;

  switch (effect_) {
    case Effect::Fireworks:
      tick_fireworks();
      break;
    case Effect::Fountain:
      tick_fountain();
      break;
    case Effect::Rain:
      tick_rain();
      break;
    case Effect::ColorSnow:
      tick_snow(width, height);
      break;
    case Effect::StarField:
      tick_starfield(width, height);
      break;
    case Effect::Orbits:
      tick_orbits(std::max(pulse_, overall_));
      break;
    default:
      break;
  }

  particles_.erase(std::remove_if(particles_.begin(), particles_.end(),
                                  [](const Particle& p) {
                                    return p.life <= 0.0f;
                                  }),
                   particles_.end());
  if (particles_.size() > 900) {
    particles_.erase(particles_.begin(),
                     particles_.begin() +
                         static_cast<std::ptrdiff_t>(particles_.size() - 900));
  }

  gtk_widget_queue_draw(GTK_WIDGET(area_));
  return active_ || !rockets_.empty() || !particles_.empty() ||
         !stars_.empty() || pulse_ > 0.01f;
}

void BeatStage::draw_backdrop(cairo_t* cr, int width, int height) {
  float r = 0;
  float g = 0;
  float b = 0;
  color_from_hue(std::fmod(hue_ + 0.55f, 1.0f), 0.35f, 0.10f, r, g, b);
  cairo_set_source_rgb(cr, r * 0.45f, g * 0.45f, b * 0.55f + 0.04f);
  cairo_paint(cr);

  cairo_pattern_t* wash =
      cairo_pattern_create_radial(width * 0.5, height * 0.55, 8,
                                  width * 0.5, height * 0.55,
                                  std::max(width, height) * 0.7);
  float r2 = 0;
  float g2 = 0;
  float b2 = 0;
  color_from_hue(hue_, 0.7f, 0.35f + pulse_ * 0.35f, r2, g2, b2);
  cairo_pattern_add_color_stop_rgba(wash, 0.0, r2, g2, b2, 0.35);
  cairo_pattern_add_color_stop_rgba(wash, 1.0, 0.02, 0.03, 0.06, 0.0);
  cairo_set_source(cr, wash);
  cairo_paint(cr);
  cairo_pattern_destroy(wash);
}

void BeatStage::draw_fireworks(cairo_t* cr) {
  for (const auto& rocket : rockets_) {
    cairo_set_source_rgb(cr, rocket.r, rocket.g, rocket.b);
    cairo_arc(cr, rocket.x, rocket.y, 2.3, 0, kTau);
    cairo_fill(cr);
  }
  for (const auto& p : particles_) {
    const float a = std::clamp(p.life / p.max_life, 0.0f, 1.0f);
    cairo_set_source_rgba(cr, p.r, p.g, p.b, a);
    cairo_arc(cr, p.x, p.y, p.size * (0.6f + 0.4f * a), 0, kTau);
    cairo_fill(cr);
  }
}

void BeatStage::draw_pulse_rings(cairo_t* cr, int width, int height) {
  float r = 1;
  float g = 1;
  float b = 1;
  float r2 = 1;
  float g2 = 1;
  float b2 = 1;
  current_palette(r, g, b, r2, g2, b2);
  const float cx = width * 0.5f;
  const float cy = height * 0.52f;
  for (int i = 0; i < 6; ++i) {
    const float t = i / 6.0f;
    const float rad =
        18.0f + t * std::min(width, height) * 0.42f + pulse_ * 28.0f;
    const float a = (1.0f - t) * (0.18f + pulse_ * 0.55f);
    cairo_set_source_rgba(cr, (i & 1) ? r2 : r, (i & 1) ? g2 : g,
                          (i & 1) ? b2 : b, a);
    cairo_set_line_width(cr, 2.0 + pulse_ * 3.0);
    cairo_arc(cr, cx, cy, rad, 0, kTau);
    cairo_stroke(cr);
  }
  cairo_set_source_rgba(cr, r, g, b, 0.35 + pulse_ * 0.5);
  cairo_arc(cr, cx, cy, 6.0 + pulse_ * 10.0, 0, kTau);
  cairo_fill(cr);
}

void BeatStage::draw_neon_bars(cairo_t* cr, int width, int height) {
  float r = 1;
  float g = 1;
  float b = 1;
  float r2 = 1;
  float g2 = 1;
  float b2 = 1;
  current_palette(r, g, b, r2, g2, b2);
  const int n = static_cast<int>(bar_levels_.size());
  const float gap = 3.0f;
  const float bar_w = std::max(3.0f, (width - 16.0f - gap * (n - 1)) / n);
  for (int i = 0; i < n; ++i) {
    const float level = std::clamp(bar_levels_[static_cast<std::size_t>(i)] +
                                       pulse_ * 0.15f,
                                   0.0f, 1.0f);
    const float h = std::max(4.0f, level * (height - 24.0f));
    const float x = 8.0f + i * (bar_w + gap);
    const float y = height - 10.0f - h;
    const bool alt = (i & 1) == 0;
    cairo_set_source_rgba(cr, alt ? r : r2, alt ? g : g2, alt ? b : b2,
                          0.35 + level * 0.65);
    cairo_rectangle(cr, x, y, bar_w, h);
    cairo_fill(cr);
  }
}

void BeatStage::draw_starburst(cairo_t* cr, int width, int height) {
  float r = 1;
  float g = 1;
  float b = 1;
  float r2 = 1;
  float g2 = 1;
  float b2 = 1;
  current_palette(r, g, b, r2, g2, b2);
  const float cx = width * 0.5f;
  const float cy = height * 0.5f;
  const int rays = 18;
  for (int i = 0; i < rays; ++i) {
    const float a = time_ * (0.4f + pulse_) + i * (kTau / rays);
    const float len =
        std::min(width, height) * (0.18f + pulse_ * 0.38f + overall_ * 0.12f);
    cairo_set_source_rgba(cr, (i & 1) ? r2 : r, (i & 1) ? g2 : g,
                          (i & 1) ? b2 : b, 0.2 + pulse_ * 0.55);
    cairo_set_line_width(cr, 2.0 + pulse_ * 2.5);
    cairo_move_to(cr, cx, cy);
    cairo_line_to(cr, cx + std::cos(a) * len, cy + std::sin(a) * len);
    cairo_stroke(cr);
  }
}

void BeatStage::draw_fountain(cairo_t* cr) {
  for (const auto& p : particles_) {
    const float a = std::clamp(p.life / p.max_life, 0.0f, 1.0f);
    cairo_set_source_rgba(cr, p.r, p.g, p.b, a);
    cairo_arc(cr, p.x, p.y, p.size * (0.7f + 0.3f * a), 0, kTau);
    cairo_fill(cr);
  }
}

void BeatStage::draw_orbits(cairo_t* cr, int width, int height) {
  float r = 1;
  float g = 1;
  float b = 1;
  float r2 = 1;
  float g2 = 1;
  float b2 = 1;
  current_palette(r, g, b, r2, g2, b2);
  const float cx = width * 0.5f;
  const float cy = height * 0.5f;
  cairo_set_source_rgba(cr, r, g, b, 0.25 + pulse_ * 0.4);
  cairo_arc(cr, cx, cy, 8.0 + pulse_ * 8.0, 0, kTau);
  cairo_fill(cr);
  for (std::size_t i = 0; i < orbiters_.size(); ++i) {
    const auto& o = orbiters_[i];
    const float x = cx + std::cos(o.angle) * o.radius;
    const float y = cy + std::sin(o.angle) * o.radius * 0.72f;
    const bool alt = (i & 1) == 0;
    cairo_set_source_rgba(cr, alt ? r : r2, alt ? g : g2, alt ? b : b2,
                          0.45 + pulse_ * 0.45);
    cairo_arc(cr, x, y, o.size + pulse_ * 3.0f, 0, kTau);
    cairo_fill(cr);
  }
}

void BeatStage::draw_rain(cairo_t* cr) {
  for (const auto& p : particles_) {
    const float a = std::clamp(p.life / p.max_life, 0.0f, 1.0f);
    cairo_set_source_rgba(cr, p.r, p.g, p.b, a);
    cairo_set_line_width(cr, p.size);
    cairo_move_to(cr, p.x, p.y);
    cairo_line_to(cr, p.x + p.vx * 0.03f, p.y + 10.0f + pulse_ * 8.0f);
    cairo_stroke(cr);
  }
}

void BeatStage::draw_tunnel(cairo_t* cr, int width, int height) {
  float r = 1;
  float g = 1;
  float b = 1;
  float r2 = 1;
  float g2 = 1;
  float b2 = 1;
  current_palette(r, g, b, r2, g2, b2);
  const float cx = width * 0.5f;
  const float cy = height * 0.5f;
  for (int i = 0; i < 10; ++i) {
    const float t = (i + std::fmod(time_ * (1.2f + pulse_ * 2.0f), 1.0f)) / 10.0f;
    const float scale = t;
    const float w = width * scale * 0.9f;
    const float h = height * scale * 0.9f;
    const float a = (1.0f - t) * (0.15f + pulse_ * 0.5f);
    cairo_set_source_rgba(cr, (i & 1) ? r2 : r, (i & 1) ? g2 : g,
                          (i & 1) ? b2 : b, a);
    cairo_set_line_width(cr, 2.0);
    cairo_rectangle(cr, cx - w * 0.5f, cy - h * 0.5f, w, h);
    cairo_stroke(cr);
  }
}

void BeatStage::draw_color_snow(cairo_t* cr) {
  for (const auto& p : particles_) {
    const float a = std::clamp(p.life / p.max_life, 0.0f, 1.0f);
    const float s = p.size * (0.7f + 0.3f * a);
    cairo_set_source_rgba(cr, p.r, p.g, p.b, 0.35 + a * 0.55);
    cairo_arc(cr, p.x, p.y, s, 0, kTau);
    cairo_fill(cr);
    // Tiny cross arms so flakes read as snow, not just dots.
    cairo_set_line_width(cr, 1.1);
    cairo_set_source_rgba(cr, p.r, p.g, p.b, 0.25 + a * 0.45);
    cairo_move_to(cr, p.x - s, p.y);
    cairo_line_to(cr, p.x + s, p.y);
    cairo_move_to(cr, p.x, p.y - s);
    cairo_line_to(cr, p.x, p.y + s);
    cairo_stroke(cr);
  }
}

void BeatStage::draw_starfield(cairo_t* cr, int width, int height) {
  const float cx = width * 0.5f;
  const float cy = height * 0.5f;
  const float scale = 0.55f * std::min(width, height);
  const float streak = 6.0f + pulse_ * 28.0f + overall_ * 10.0f;
  for (const auto& star : stars_) {
    const float z = std::max(0.05f, star.z);
    const float sx = cx + (star.x / z) * scale;
    const float sy = cy + (star.y / z) * scale;
    const float pz = z + 0.045f + pulse_ * 0.04f;
    const float px = cx + (star.x / pz) * scale;
    const float py = cy + (star.y / pz) * scale;
    const float bright = std::clamp(1.15f - z, 0.15f, 1.0f);
    cairo_set_source_rgba(cr, star.r, star.g, star.b,
                          0.25 + bright * 0.7);
    cairo_set_line_width(cr, star.size * (0.6f + bright * 0.9f));
    cairo_move_to(cr, px, py);
    const float dx = sx - px;
    const float dy = sy - py;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len > 0.01f && streak > 0.0f) {
      const float extend = std::min(streak, len * (1.0f + pulse_ * 2.5f));
      cairo_line_to(cr, sx + dx / len * extend * 0.15f,
                    sy + dy / len * extend * 0.15f);
    } else {
      cairo_line_to(cr, sx, sy);
    }
    cairo_stroke(cr);
    cairo_set_source_rgba(cr, star.r, star.g, star.b, 0.4 + bright * 0.6);
    cairo_arc(cr, sx, sy, star.size * (0.5f + bright), 0, kTau);
    cairo_fill(cr);
  }
}

void BeatStage::draw_idle(cairo_t* cr, int width, int height) {
  cairo_set_source_rgba(cr, 0.75, 0.8, 0.9, 0.45);
  cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                         CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 13.0);
  const char* msg = "Play a song for visuals";
  cairo_text_extents_t extents{};
  cairo_text_extents(cr, msg, &extents);
  cairo_move_to(cr, (width - extents.width) * 0.5,
                height * 0.5 + extents.height * 0.5);
  cairo_show_text(cr, msg);
}

void BeatStage::draw(cairo_t* cr, int width, int height) {
  if (width <= 0 || height <= 0) {
    return;
  }
  draw_backdrop(cr, width, height);

  if (!active_ && rockets_.empty() && particles_.empty() && stars_.empty() &&
      pulse_ <= 0.02f) {
    draw_idle(cr, width, height);
    return;
  }

  switch (effect_) {
    case Effect::Fireworks:
      draw_fireworks(cr);
      break;
    case Effect::PulseRings:
      draw_pulse_rings(cr, width, height);
      break;
    case Effect::NeonBars:
      draw_neon_bars(cr, width, height);
      break;
    case Effect::Starburst:
      draw_starburst(cr, width, height);
      break;
    case Effect::Fountain:
      draw_fountain(cr);
      break;
    case Effect::Orbits:
      draw_orbits(cr, width, height);
      break;
    case Effect::Rain:
      draw_rain(cr);
      break;
    case Effect::Tunnel:
      draw_tunnel(cr, width, height);
      break;
    case Effect::ColorSnow:
      draw_color_snow(cr);
      break;
    case Effect::StarField:
      draw_starfield(cr, width, height);
      break;
    case Effect::Count:
      break;
  }
}

}  // namespace radio::ui
