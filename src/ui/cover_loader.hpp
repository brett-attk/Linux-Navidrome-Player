#pragma once

#include "subsonic/client.hpp"

#include <gtk/gtk.h>

#include <string>

namespace radio::ui {

class CoverLoader {
 public:
  // Apply cover immediately from memory or disk cache. Returns true if set.
  static bool apply_cached(GtkImage* image, const std::string& cover_id,
                           int request_size, int display_px);

  // Cache-miss path: keep placeholder, fetch via client, then update image.
  static void load_async(GtkImage* image, subsonic::Client& client,
                         const std::string& cover_id, int request_size,
                         int display_px);

  // Convenience: sync cache first, otherwise async fetch.
  static void load(GtkImage* image, subsonic::Client& client,
                   const std::string& cover_id, int request_size,
                   int display_px);
};

}  // namespace radio::ui
