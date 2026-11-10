#pragma once

#include "audio/eq_settings.hpp"

#include <string>

namespace radio {

struct AppConfig {
  std::string server_url;
  std::string username;
  std::string password;
  double volume = 0.8;
  audio::EqSettings eq;
};

class ConfigStore {
 public:
  static std::string path();
  static AppConfig load();
  static bool save(const AppConfig& config);
};

}  // namespace radio
