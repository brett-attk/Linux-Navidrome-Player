#include "cache/cover_cache.hpp"

#include <glib.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace fs = std::filesystem;

namespace radio::cache {
namespace {

constexpr auto kTtl = std::chrono::hours(24 * CoverCache::kTtlDays);

std::string xdg_cache_home() {
  const char* xdg = std::getenv("XDG_CACHE_HOME");
  if (xdg && *xdg) {
    return xdg;
  }
  const char* home = std::getenv("HOME");
  if (home && *home) {
    return (fs::path(home) / ".cache").string();
  }
  return ".cache";
}

std::string hash_key(const std::string& cover_id, int size) {
  const std::string raw = cover_id + "|" + std::to_string(size);
  gchar* digest =
      g_compute_checksum_for_string(G_CHECKSUM_SHA256, raw.c_str(), raw.size());
  std::string out = digest ? digest : "unknown";
  g_free(digest);
  return out;
}

}  // namespace

std::string CoverCache::directory() {
  return (fs::path(xdg_cache_home()) / "radio-player" / "covers").string();
}

std::string CoverCache::path_for(const std::string& cover_id, int size) {
  return (fs::path(directory()) / (hash_key(cover_id, size) + ".img")).string();
}

bool CoverCache::is_fresh(const std::string& path) {
  std::error_code ec;
  const auto mtime = fs::last_write_time(path, ec);
  if (ec) {
    return false;
  }
  const auto now = fs::file_time_type::clock::now();
  const auto age = std::chrono::duration_cast<std::chrono::seconds>(now - mtime);
  return age <= std::chrono::duration_cast<std::chrono::seconds>(kTtl);
}

std::optional<std::vector<unsigned char>> CoverCache::get(
    const std::string& cover_id, int size) {
  if (cover_id.empty()) {
    return std::nullopt;
  }
  try {
    const auto path = path_for(cover_id, size);
    std::error_code ec;
    if (!fs::is_regular_file(path, ec) || !is_fresh(path)) {
      if (!ec && fs::exists(path, ec)) {
        fs::remove(path, ec);
      }
      return std::nullopt;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
      return std::nullopt;
    }
    in.seekg(0, std::ios::end);
    const auto len = in.tellg();
    if (len <= 0 || len > 20 * 1024 * 1024) {
      return std::nullopt;
    }
    in.seekg(0, std::ios::beg);
    std::vector<unsigned char> bytes(static_cast<std::size_t>(len));
    in.read(reinterpret_cast<char*>(bytes.data()), len);
    if (!in) {
      return std::nullopt;
    }
    return bytes;
  } catch (...) {
    return std::nullopt;
  }
}

void CoverCache::put(const std::string& cover_id, int size,
                     const std::vector<unsigned char>& bytes) {
  if (cover_id.empty() || bytes.empty() || bytes.size() > 20 * 1024 * 1024) {
    return;
  }
  try {
    std::error_code ec;
    fs::create_directories(directory(), ec);
    if (ec) {
      return;
    }

    const auto path = path_for(cover_id, size);
    const auto tmp = path + ".tmp";
    {
      std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
      if (!out) {
        return;
      }
      out.write(reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
      if (!out) {
        fs::remove(tmp, ec);
        return;
      }
    }
    fs::rename(tmp, path, ec);
    if (ec) {
      fs::remove(tmp, ec);
    }
  } catch (...) {
  }
}

void CoverCache::purge_expired() {
  try {
    std::error_code ec;
    const auto dir = directory();
    if (!fs::is_directory(dir, ec)) {
      return;
    }
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
      if (ec) {
        break;
      }
      if (!entry.is_regular_file(ec)) {
        continue;
      }
      const auto path = entry.path().string();
      if (!is_fresh(path)) {
        fs::remove(path, ec);
      }
    }
  } catch (...) {
  }
}

}  // namespace radio::cache
