#include "config.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace radio {
namespace {

nlohmann::json eq_to_json(const audio::EqSettings& eq) {
  nlohmann::json bands = nlohmann::json::array();
  for (double gain : eq.gains_db) {
    bands.push_back(gain);
  }
  return nlohmann::json{
      {"enabled", eq.enabled},
      {"preset", eq.preset},
      {"bands", std::move(bands)},
  };
}

audio::EqSettings eq_from_json(const nlohmann::json& j) {
  audio::EqSettings eq;
  if (!j.is_object()) {
    return eq;
  }
  eq.enabled = j.value("enabled", false);
  eq.preset = j.value("preset", "Flat");
  if (j.contains("bands") && j["bands"].is_array()) {
    const auto& bands = j["bands"];
    for (std::size_t i = 0; i < audio::kEqBands && i < bands.size(); ++i) {
      eq.gains_db[i] = bands[i].get<double>();
    }
  } else if (const auto* preset = audio::find_eq_preset(eq.preset)) {
    eq.gains_db = preset->gains_db;
  }
  return eq;
}

}  // namespace

std::string ConfigStore::path() {
  const char* xdg = std::getenv("XDG_CONFIG_HOME");
  fs::path base;
  if (xdg && *xdg) {
    base = xdg;
  } else {
    const char* home = std::getenv("HOME");
    base = home ? fs::path(home) / ".config" : fs::path(".config");
  }
  return (base / "radio-player" / "config.json").string();
}

AppConfig ConfigStore::load() {
  AppConfig cfg;
  const auto file = path();
  std::ifstream in(file);
  if (!in) {
    return cfg;
  }

  try {
    nlohmann::json j;
    in >> j;
    cfg.server_url = j.value("server_url", "");
    cfg.username = j.value("username", "");
    cfg.password = j.value("password", "");
    cfg.volume = j.value("volume", 0.8);
    cfg.minimal_player = j.value("minimal_player", false);
    if (j.contains("eq")) {
      cfg.eq = eq_from_json(j["eq"]);
    }
  } catch (...) {
    // Keep defaults on parse errors.
  }
  return cfg;
}

bool ConfigStore::save(const AppConfig& config) {
  try {
    const auto file = path();
    std::error_code ec;
    fs::create_directories(fs::path(file).parent_path(), ec);
    if (ec) {
      return false;
    }

    nlohmann::json j{
        {"server_url", config.server_url},
        {"username", config.username},
        {"password", config.password},
        {"volume", config.volume},
        {"minimal_player", config.minimal_player},
        {"eq", eq_to_json(config.eq)},
    };

    const auto tmp = file + ".tmp";
    {
      std::ofstream out(tmp);
      if (!out) {
        return false;
      }
      out << j.dump(2);
      if (!out) {
        fs::remove(tmp, ec);
        return false;
      }
    }
    fs::rename(tmp, file, ec);
    if (ec) {
      fs::remove(tmp, ec);
      return false;
    }
    return true;
  } catch (...) {
    return false;
  }
}

}  // namespace radio
