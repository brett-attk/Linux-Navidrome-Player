#include "app_icon.hpp"
#include "cache/cover_cache.hpp"
#include "ui/main_window.hpp"

#include <adwaita.h>

#include <memory>

namespace {

// Owns the single main window for the process lifetime.
std::unique_ptr<radio::ui::MainWindow> g_window;

void on_activate(GApplication* app, gpointer) {
  radio::register_app_icon_theme();
  // GApplication may emit "activate" more than once (e.g. re-launch); reuse window.
  if (!g_window) {
    g_window = std::make_unique<radio::ui::MainWindow>(ADW_APPLICATION(app));
  }
  g_window->present();
}

}  // namespace

int main(int argc, char** argv) {
  adw_init();
  g_set_prgname(radio::kAppIconName);
  g_set_application_name("Radio Player");
  radio::cache::CoverCache::purge_expired();
  auto* app = adw_application_new(radio::kAppIconName,
                                  G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(app, "activate", G_CALLBACK(on_activate), nullptr);
  // Blocks here until the app quits; returns process exit code.
  const int status = g_application_run(G_APPLICATION(app), argc, argv);
  g_window.reset();  // destroy C++ window before dropping the GApplication ref
  g_object_unref(app);
  return status;
}
