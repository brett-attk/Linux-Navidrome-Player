#include "minitest.hpp"
#include "test_helpers.hpp"

#include "cache/cover_cache.hpp"

#include <cstdlib>
#include <filesystem>
#include <system_error>

namespace {

struct EnvCache {
  fs::path root;
  explicit EnvCache(const std::string& prefix) {
    root = make_temp_dir(prefix);
    setenv("XDG_CACHE_HOME", (root / "cache").c_str(), 1);
  }
  ~EnvCache() {
    std::error_code ec;
    fs::remove_all(root, ec);
  }
};

}  // namespace

TEST(CoverCache, put_get_roundtrip) {
  EnvCache env("radio-cover");
  const std::vector<unsigned char> bytes = {1, 2, 3, 4, 5, 9};
  radio::cache::CoverCache::put("cover-1", 120, bytes);
  auto got = radio::cache::CoverCache::get("cover-1", 120);
  CHECK_TRUE(got.has_value());
  CHECK_EQ(got->size(), bytes.size());
  CHECK_EQ((*got)[0], static_cast<unsigned char>(1));
  CHECK_EQ((*got)[5], static_cast<unsigned char>(9));
}

TEST(CoverCache, miss_and_empty_id) {
  EnvCache env("radio-cover-miss");
  CHECK_FALSE(radio::cache::CoverCache::get("missing", 64).has_value());
  CHECK_FALSE(radio::cache::CoverCache::get("", 64).has_value());
  radio::cache::CoverCache::put("", 64, {1, 2, 3});  // no-op
  CHECK_FALSE(radio::cache::CoverCache::get("", 64).has_value());
}

TEST(CoverCache, different_sizes_are_separate) {
  EnvCache env("radio-cover-size");
  radio::cache::CoverCache::put("same", 64, {1});
  radio::cache::CoverCache::put("same", 128, {2, 2});
  auto a = radio::cache::CoverCache::get("same", 64);
  auto b = radio::cache::CoverCache::get("same", 128);
  CHECK_TRUE(a.has_value());
  CHECK_TRUE(b.has_value());
  CHECK_EQ(a->size(), 1u);
  CHECK_EQ(b->size(), 2u);
}

TEST(CoverCache, directory_under_xdg_cache) {
  EnvCache env("radio-cover-dir");
  const auto dir = radio::cache::CoverCache::directory();
  CHECK(dir.find("radio-player") != std::string::npos);
  CHECK(dir.find("covers") != std::string::npos);
}
