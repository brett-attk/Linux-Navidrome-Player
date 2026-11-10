#pragma once

#include "audio/eq_settings.hpp"

#include <functional>
#include <string>
#include <vector>

#include <gtk/gtk.h>

namespace radio::ui {

// Self-owned dialog: `new EqDialog(...); dlg->present();` then it `delete this`
// when the window closes (see on_closed). Callers must not stack-allocate it.
class EqDialog {
 public:
  using ChangedCb = std::function<void(const audio::EqSettings&)>;

  EqDialog(GtkWindow* parent, audio::EqSettings initial, ChangedCb on_changed);
  void present();

 private:
  void rebuild_from_settings();
  void emit_changed();
  void apply_preset(const std::string& name);
  void on_closed();

  GtkWindow* parent_ = nullptr;
  GtkWindow* window_ = nullptr;
  GtkSwitch* enable_switch_ = nullptr;
  GtkDropDown* preset_dropdown_ = nullptr;
  std::vector<GtkScale*> band_scales_;
  audio::EqSettings settings_;
  ChangedCb on_changed_;
  bool updating_ = false;  // suppress recursive signal storms while syncing widgets
  bool closed_ = false;    // on_closed may run from both close-request and destroy
};

}  // namespace radio::ui
