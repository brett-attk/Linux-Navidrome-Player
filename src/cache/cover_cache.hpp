#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace radio::cache {

// On-disk cover art cache under ~/.cache/radio-player/covers.
// All methods are static (no instance state); filenames are SHA-256 of id|size.
class CoverCache {
 public:
  static constexpr int kTtlDays = 14;

  // Fresh hit -> bytes; miss / expired / I/O error -> std::nullopt.
  static std::optional<std::vector<unsigned char>> get(const std::string& cover_id,
                                                       int size);

  // Writes bytes to the cache directory.
  static void put(const std::string& cover_id, int size,
                  const std::vector<unsigned char>& bytes);

  // Deletes entries older than TTL. Safe to call periodically.
  static void purge_expired();

  static std::string directory();

 private:
  static std::string path_for(const std::string& cover_id, int size);
  static bool is_fresh(const std::string& path);
};

}  // namespace radio::cache
