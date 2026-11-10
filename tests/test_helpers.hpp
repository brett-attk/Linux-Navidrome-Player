#pragma once

#include "subsonic/models.hpp"

#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

inline radio::subsonic::Song make_song(const std::string& id,
                                       const std::string& title = "") {
  radio::subsonic::Song s;
  s.id = id;
  s.title = title.empty() ? ("Song " + id) : title;
  s.artist = "Artist";
  s.album = "Album";
  return s;
}

inline fs::path make_temp_dir(const std::string& prefix) {
  const auto base = fs::temp_directory_path() / (prefix + "-XXXXXX");
  std::string pattern = base.string();
  std::vector<char> buf(pattern.begin(), pattern.end());
  buf.push_back('\0');
  if (::mkdtemp(buf.data()) == nullptr) {
    throw std::runtime_error("mkdtemp failed");
  }
  return fs::path(buf.data());
}
