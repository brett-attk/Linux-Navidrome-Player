#pragma once

#include <gtk/gtk.h>

#include <filesystem>
#include <string>
#include <vector>

namespace radio {

// Must match the filename under data/icons/hicolor/*/apps/ (no extension).
inline constexpr const char* kAppIconName = "ai.workspace.radioplayer";

// Teach GTK where to find our custom icon when not installed system-wide.
inline void register_app_icon_theme() {
  GdkDisplay* display = gdk_display_get_default();
  if (!display) {
    return;
  }
  GtkIconTheme* theme = gtk_icon_theme_get_for_display(display);
  if (!theme) {
    return;
  }

  std::vector<std::filesystem::path> candidates;

  // RADIO_ICON_DIR is injected by CMake for running from the build tree.
#ifdef RADIO_ICON_DIR
  candidates.emplace_back(RADIO_ICON_DIR);
#endif

  // Also try paths relative to the running binary (installed or local).
  char* exe = g_file_read_link("/proc/self/exe", nullptr);
  if (exe) {
    const std::filesystem::path bin = std::filesystem::path(exe).parent_path();
    g_free(exe);
    candidates.push_back(bin / "icons");
    candidates.push_back(bin / "../share/icons");
    candidates.push_back(bin / "../../data/icons");
    candidates.push_back(bin / "../data/icons");
  }

  for (const auto& path : candidates) {
    std::error_code ec;
    if (!std::filesystem::is_directory(path, ec)) {
      continue;
    }
    // Search path should contain theme folders such as hicolor/.
    gtk_icon_theme_add_search_path(theme, path.string().c_str());
  }
}

inline void apply_window_icon(GtkWindow* window) {
  if (!window) {
    return;
  }
  gtk_window_set_icon_name(window, kAppIconName);
}

}  // namespace radio
