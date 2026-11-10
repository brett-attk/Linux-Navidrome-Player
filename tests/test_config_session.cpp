#include "minitest.hpp"
#include "test_helpers.hpp"

#include "config.hpp"
#include "session.hpp"

#include <cstdlib>
#include <filesystem>
#include <system_error>

namespace {

struct EnvHome {
  fs::path root;
  explicit EnvHome(const std::string& prefix) {
    root = make_temp_dir(prefix);
    setenv("XDG_CONFIG_HOME", (root / "config").c_str(), 1);
  }
  ~EnvHome() {
    std::error_code ec;
    fs::remove_all(root, ec);
  }
};

}  // namespace

TEST(ConfigStore, save_and_load_roundtrip) {
  EnvHome env("radio-cfg");
  radio::AppConfig cfg;
  cfg.server_url = "http://navidrome.local:4533";
  cfg.username = "brett";
  cfg.password = "secret";
  cfg.volume = 0.42;
  cfg.eq.enabled = true;
  cfg.eq.preset = "Rock";
  if (const auto* rock = radio::audio::find_eq_preset("Rock")) {
    cfg.eq.gains_db = rock->gains_db;
  }
  CHECK_TRUE(radio::ConfigStore::save(cfg));

  const auto loaded = radio::ConfigStore::load();
  CHECK_EQ(loaded.server_url, cfg.server_url);
  CHECK_EQ(loaded.username, cfg.username);
  CHECK_EQ(loaded.password, cfg.password);
  CHECK_NEAR(loaded.volume, 0.42, 0.0001);
  CHECK_TRUE(loaded.eq.enabled);
  CHECK_EQ(loaded.eq.preset, std::string("Rock"));
  CHECK_NEAR(loaded.eq.gains_db[0], cfg.eq.gains_db[0], 0.0001);
}

TEST(ConfigStore, missing_file_returns_defaults) {
  EnvHome env("radio-cfg-miss");
  const auto cfg = radio::ConfigStore::load();
  CHECK_TRUE(cfg.server_url.empty());
  CHECK_NEAR(cfg.volume, 0.8, 0.0001);
  CHECK_FALSE(cfg.eq.enabled);
}

TEST(SessionStore, save_load_clear) {
  EnvHome env("radio-sess");
  radio::PlaybackSession session;
  session.server_url = "http://host:4533";
  session.songs = {make_song("1", "One"), make_song("2", "Two")};
  session.order = {1, 0};
  session.current = 0;
  session.position_ns = 123456789;
  session.shuffle = true;
  session.repeat = radio::queue::RepeatMode::One;
  session.songs[0].annotations.starred = true;
  session.songs[0].annotations.user_rating = 5;

  CHECK_TRUE(radio::SessionStore::save(session));
  const auto loaded = radio::SessionStore::load();
  CHECK_EQ(loaded.server_url, session.server_url);
  CHECK_EQ(loaded.songs.size(), 2u);
  CHECK_EQ(loaded.songs[0].title, std::string("One"));
  CHECK_TRUE(loaded.songs[0].annotations.starred);
  CHECK_EQ(loaded.songs[0].annotations.user_rating, 5);
  CHECK_EQ(loaded.order[0], 1u);
  CHECK_EQ(loaded.current, 0);
  CHECK_EQ(loaded.position_ns, static_cast<std::int64_t>(123456789));
  CHECK_TRUE(loaded.shuffle);
  CHECK(loaded.repeat == radio::queue::RepeatMode::One);

  radio::SessionStore::clear();
  const auto cleared = radio::SessionStore::load();
  CHECK_TRUE(cleared.songs.empty());
}
