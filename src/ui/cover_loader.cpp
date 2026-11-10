#include "ui/cover_loader.hpp"

#include "cache/cover_cache.hpp"

#include <gdk-pixbuf/gdk-pixbuf.h>

#include <list>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace radio::ui {
namespace {

constexpr std::size_t kMemoryCacheLimit = 256;

// Simple LRU (Least Recently Used) cache for decoded cover textures.
// - `order` is newest-first keys
// - `entries` maps key -> {texture, iterator into order}
// On hit we move that key to the front; when over limit we drop the back.
struct MemoryCache {
  std::mutex mu;  // covers can be touched from async completions
  std::list<std::string> order;
  std::unordered_map<std::string,
                     std::pair<GdkTexture*, std::list<std::string>::iterator>>
      entries;

  ~MemoryCache() {
    for (auto& [_, entry] : entries) {
      g_object_unref(entry.first);
    }
  }
};

// Function-local static = created once on first call (Meyer's singleton).
MemoryCache& memory_cache() {
  static MemoryCache cache;
  return cache;
}

std::string memory_key(const std::string& cover_id, int display_px) {
  return cover_id + "|" + std::to_string(display_px);
}

GdkTexture* memory_get(const std::string& cover_id, int display_px) {
  auto& cache = memory_cache();
  std::lock_guard lock(cache.mu);  // unlocks automatically when leaving scope
  const auto key = memory_key(cover_id, display_px);
  auto it = cache.entries.find(key);
  if (it == cache.entries.end()) {
    return nullptr;
  }
  // O(1) "mark as most recently used": move list node to the front.
  cache.order.splice(cache.order.begin(), cache.order, it->second.second);
  // Caller owns this extra ref and must g_object_unref it.
  return GDK_TEXTURE(g_object_ref(it->second.first));
}

void memory_put(const std::string& cover_id, int display_px,
                GdkTexture* texture) {
  if (!texture || cover_id.empty()) {
    return;
  }
  auto& cache = memory_cache();
  std::lock_guard lock(cache.mu);
  const auto key = memory_key(cover_id, display_px);
  auto it = cache.entries.find(key);
  if (it != cache.entries.end()) {
    g_object_unref(it->second.first);
    cache.order.erase(it->second.second);
    cache.entries.erase(it);
  }
  cache.order.push_front(key);
  cache.entries.emplace(
      key, std::make_pair(GDK_TEXTURE(g_object_ref(texture)), cache.order.begin()));
  while (cache.entries.size() > kMemoryCacheLimit) {
    const std::string& old = cache.order.back();
    auto old_it = cache.entries.find(old);
    if (old_it != cache.entries.end()) {
      g_object_unref(old_it->second.first);
      cache.entries.erase(old_it);
    }
    cache.order.pop_back();
  }
}

GdkTexture* texture_from_bytes(const std::vector<unsigned char>& bytes,
                               int display_px) {
  if (bytes.empty() || display_px <= 0) {
    return nullptr;
  }

  try {
    GError* error = nullptr;
    GdkPixbufLoader* loader = gdk_pixbuf_loader_new();
    if (!gdk_pixbuf_loader_write(loader, bytes.data(), bytes.size(), &error) ||
        !gdk_pixbuf_loader_close(loader, &error)) {
      if (error) {
        g_error_free(error);
      }
      g_object_unref(loader);
      return nullptr;
    }

    GdkPixbuf* pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);
    if (!pixbuf) {
      g_object_unref(loader);
      return nullptr;
    }

    GdkPixbuf* scaled = gdk_pixbuf_scale_simple(pixbuf, display_px, display_px,
                                                GDK_INTERP_BILINEAR);
    if (!scaled) {
      g_object_unref(loader);
      return nullptr;
    }
    GdkPixbuf* rgba = gdk_pixbuf_get_has_alpha(scaled)
                          ? GDK_PIXBUF(g_object_ref(scaled))
                          : gdk_pixbuf_add_alpha(scaled, FALSE, 0, 0, 0);
    g_object_unref(scaled);
    if (!rgba) {
      g_object_unref(loader);
      return nullptr;
    }

    const int width = gdk_pixbuf_get_width(rgba);
    const int height = gdk_pixbuf_get_height(rgba);
    const int stride = gdk_pixbuf_get_rowstride(rgba);
    if (width <= 0 || height <= 0 || stride <= 0) {
      g_object_unref(rgba);
      g_object_unref(loader);
      return nullptr;
    }
    const gsize len = static_cast<gsize>(stride) * static_cast<gsize>(height);
    GBytes* gbytes = g_bytes_new(gdk_pixbuf_get_pixels(rgba), len);
    GdkTexture* texture =
        gdk_memory_texture_new(width, height, GDK_MEMORY_R8G8B8A8, gbytes, stride);
    g_bytes_unref(gbytes);
    g_object_unref(rgba);
    g_object_unref(loader);
    return texture;
  } catch (...) {
    return nullptr;
  }
}

bool set_image_texture(GtkImage* image, GdkTexture* texture) {
  if (!image || !texture || !GTK_IS_IMAGE(image)) {
    return false;
  }
  gtk_image_set_from_paintable(image, GDK_PAINTABLE(texture));
  return true;
}

}  // namespace

bool CoverLoader::apply_cached(GtkImage* image, const std::string& cover_id,
                               int request_size, int display_px) {
  if (!image || cover_id.empty() || !GTK_IS_IMAGE(image)) {
    return false;
  }

  if (GdkTexture* cached = memory_get(cover_id, display_px)) {
    const bool ok = set_image_texture(image, cached);
    g_object_unref(cached);
    return ok;
  }

  auto bytes = cache::CoverCache::get(cover_id, request_size);
  if (!bytes) {
    return false;
  }
  GdkTexture* texture = texture_from_bytes(*bytes, display_px);
  if (!texture) {
    return false;
  }
  memory_put(cover_id, display_px, texture);
  const bool ok = set_image_texture(image, texture);
  g_object_unref(texture);
  return ok;
}

void CoverLoader::load_async(GtkImage* image, subsonic::Client& client,
                             const std::string& cover_id, int request_size,
                             int display_px) {
  if (!image || cover_id.empty() || !GTK_IS_IMAGE(image)) {
    return;
  }
  // Keep the GtkImage alive until the HTTP callback finishes. Home tiles can
  // be destroyed/rebuilt while a cover download is still in flight.
  g_object_ref(image);
  client.get_cover_art(
      cover_id, request_size,
      [image, cover_id, request_size, display_px](
          std::vector<unsigned char> bytes,
          std::optional<subsonic::ClientError> err) {
        const auto release = [image] { g_object_unref(image); };
        // GTK_IS_IMAGE: widget may already be finalized even with our ref edge cases.
        if (err || bytes.empty() || !GTK_IS_IMAGE(image)) {
          release();
          return;
        }
        try {
          cache::CoverCache::put(cover_id, request_size, bytes);
          GdkTexture* texture = texture_from_bytes(bytes, display_px);
          if (!texture) {
            release();
            return;
          }
          memory_put(cover_id, display_px, texture);
          set_image_texture(image, texture);
          g_object_unref(texture);
        } catch (...) {
          // Keep UI alive on decode/cache failures.
        }
        release();
      });
}

void CoverLoader::load(GtkImage* image, subsonic::Client& client,
                       const std::string& cover_id, int request_size,
                       int display_px) {
  if (apply_cached(image, cover_id, request_size, display_px)) {
    return;
  }
  load_async(image, client, cover_id, request_size, display_px);
}

}  // namespace radio::ui
