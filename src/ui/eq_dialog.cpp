#include "ui/eq_dialog.hpp"

#include <string>
#include <vector>

namespace radio::ui {

EqDialog::EqDialog(GtkWindow* parent, audio::EqSettings initial,
                   ChangedCb on_changed)
    : parent_(parent),
      settings_(std::move(initial)),
      on_changed_(std::move(on_changed)) {}

void EqDialog::present() {
  window_ = GTK_WINDOW(gtk_window_new());
  gtk_window_set_title(window_, "Equalizer");
  gtk_window_set_transient_for(window_, parent_);
  gtk_window_set_modal(window_, TRUE);
  gtk_window_set_default_size(window_, 640, 360);
  gtk_window_set_destroy_with_parent(window_, TRUE);

  auto* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_top(root, 16);
  gtk_widget_set_margin_bottom(root, 16);
  gtk_widget_set_margin_start(root, 16);
  gtk_widget_set_margin_end(root, 16);
  gtk_window_set_child(window_, root);

  auto* top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_box_append(GTK_BOX(root), top);

  auto* enable_label = gtk_label_new("Enable EQ");
  gtk_box_append(GTK_BOX(top), enable_label);
  enable_switch_ = GTK_SWITCH(gtk_switch_new());
  gtk_switch_set_active(enable_switch_, settings_.enabled);
  gtk_widget_set_valign(GTK_WIDGET(enable_switch_), GTK_ALIGN_CENTER);
  gtk_box_append(GTK_BOX(top), GTK_WIDGET(enable_switch_));

  auto* preset_label = gtk_label_new("Preset");
  gtk_widget_set_margin_start(preset_label, 12);
  gtk_box_append(GTK_BOX(top), preset_label);

  std::vector<const char*> names;
  names.reserve(audio::eq_presets().size() + 2);
  names.push_back("Custom");
  for (const auto& preset : audio::eq_presets()) {
    names.push_back(preset.name);
  }
  names.push_back(nullptr);
  preset_dropdown_ = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(names.data()));
  guint selected = 0;
  for (guint i = 0; i < static_cast<guint>(audio::eq_presets().size()); ++i) {
    if (settings_.preset == audio::eq_presets()[i].name) {
      selected = i + 1;
      break;
    }
  }
  if (settings_.preset == "Custom") {
    selected = 0;
  }
  gtk_drop_down_set_selected(preset_dropdown_, selected);
  gtk_widget_set_hexpand(GTK_WIDGET(preset_dropdown_), TRUE);
  gtk_box_append(GTK_BOX(top), GTK_WIDGET(preset_dropdown_));

  auto* hint = gtk_label_new(
      "Turn EQ off to restore flat/standard playback. Changes are saved.");
  gtk_widget_add_css_class(hint, "dim-label");
  gtk_widget_set_halign(hint, GTK_ALIGN_START);
  gtk_box_append(GTK_BOX(root), hint);

  auto* bands = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_vexpand(bands, TRUE);
  gtk_widget_set_halign(bands, GTK_ALIGN_CENTER);
  gtk_box_append(GTK_BOX(root), bands);

  band_scales_.clear();
  for (int i = 0; i < audio::kEqBands; ++i) {
    auto* col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_append(GTK_BOX(bands), col);

    auto* scale = GTK_SCALE(gtk_scale_new_with_range(
        GTK_ORIENTATION_VERTICAL, -12.0, 12.0, 0.5));
    gtk_range_set_inverted(GTK_RANGE(scale), TRUE);
    gtk_scale_set_draw_value(scale, TRUE);
    gtk_scale_set_value_pos(scale, GTK_POS_BOTTOM);
    gtk_widget_set_vexpand(GTK_WIDGET(scale), TRUE);
    gtk_widget_set_size_request(GTK_WIDGET(scale), 36, 180);
    gtk_range_set_value(GTK_RANGE(scale),
                        settings_.gains_db[static_cast<std::size_t>(i)]);
    gtk_box_append(GTK_BOX(col), GTK_WIDGET(scale));

    auto* label = gtk_label_new(audio::kEqBandLabels[static_cast<std::size_t>(i)]);
    gtk_widget_add_css_class(label, "dim-label");
    gtk_box_append(GTK_BOX(col), label);
    band_scales_.push_back(scale);

    g_signal_connect(
        scale, "value-changed",
        G_CALLBACK(+[](GtkRange* range, gpointer self) {
          auto* dlg = static_cast<EqDialog*>(self);
          if (dlg->updating_) {
            return;
          }
          // Identify band index.
          for (std::size_t i = 0; i < dlg->band_scales_.size(); ++i) {
            if (GTK_RANGE(dlg->band_scales_[i]) == range) {
              dlg->settings_.gains_db[i] = gtk_range_get_value(range);
              dlg->settings_.preset = "Custom";
              dlg->updating_ = true;
              gtk_drop_down_set_selected(dlg->preset_dropdown_, 0);
              dlg->updating_ = false;
              dlg->emit_changed();
              break;
            }
          }
        }),
        this);
  }

  auto* bottom = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_halign(bottom, GTK_ALIGN_END);
  gtk_box_append(GTK_BOX(root), bottom);
  auto* close_btn = gtk_button_new_with_label("Close");
  gtk_widget_add_css_class(close_btn, "suggested-action");
  gtk_box_append(GTK_BOX(bottom), close_btn);

  g_signal_connect(
      enable_switch_, "state-set",
      G_CALLBACK(+[](GtkSwitch*, gboolean state, gpointer self) -> gboolean {
        auto* dlg = static_cast<EqDialog*>(self);
        if (dlg->updating_) {
          return FALSE;
        }
        dlg->settings_.enabled = state;
        dlg->emit_changed();
        return FALSE;
      }),
      this);

  g_signal_connect(
      preset_dropdown_, "notify::selected",
      G_CALLBACK(+[](GObject*, GParamSpec*, gpointer self) {
        auto* dlg = static_cast<EqDialog*>(self);
        if (dlg->updating_) {
          return;
        }
        const guint selected = gtk_drop_down_get_selected(dlg->preset_dropdown_);
        if (selected == 0) {
          dlg->settings_.preset = "Custom";
          dlg->emit_changed();
          return;
        }
        const auto& presets = audio::eq_presets();
        const std::size_t idx = static_cast<std::size_t>(selected - 1);
        if (idx < presets.size()) {
          dlg->apply_preset(presets[idx].name);
        }
      }),
      this);

  g_signal_connect_swapped(close_btn, "clicked",
                           G_CALLBACK(+[](gpointer self) {
                             auto* dlg = static_cast<EqDialog*>(self);
                             if (dlg->window_) {
                               gtk_window_destroy(dlg->window_);
                             }
                           }),
                           this);
  g_signal_connect(
      window_, "close-request",
      G_CALLBACK(+[](GtkWindow*, gpointer self) -> gboolean {
        static_cast<EqDialog*>(self)->on_closed();
        return FALSE;
      }),
      this);
  g_signal_connect_swapped(window_, "destroy",
                           G_CALLBACK(+[](gpointer self) {
                             static_cast<EqDialog*>(self)->on_closed();
                           }),
                           this);

  rebuild_from_settings();
  gtk_window_present(window_);
}

void EqDialog::rebuild_from_settings() {
  updating_ = true;
  gtk_switch_set_active(enable_switch_, settings_.enabled);
  guint selected = 0;
  if (settings_.preset != "Custom") {
    for (guint i = 0; i < static_cast<guint>(audio::eq_presets().size()); ++i) {
      if (settings_.preset == audio::eq_presets()[i].name) {
        selected = i + 1;
        break;
      }
    }
  }
  gtk_drop_down_set_selected(preset_dropdown_, selected);
  for (std::size_t i = 0; i < band_scales_.size(); ++i) {
    gtk_range_set_value(GTK_RANGE(band_scales_[i]), settings_.gains_db[i]);
    gtk_widget_set_sensitive(GTK_WIDGET(band_scales_[i]), settings_.enabled);
  }
  gtk_widget_set_sensitive(GTK_WIDGET(preset_dropdown_), settings_.enabled);
  updating_ = false;
}

void EqDialog::apply_preset(const std::string& name) {
  const auto* preset = audio::find_eq_preset(name);
  if (!preset) {
    return;
  }
  settings_.preset = preset->name;
  settings_.gains_db = preset->gains_db;
  rebuild_from_settings();
  emit_changed();
}

void EqDialog::emit_changed() {
  for (auto* scale : band_scales_) {
    gtk_widget_set_sensitive(GTK_WIDGET(scale), settings_.enabled);
  }
  gtk_widget_set_sensitive(GTK_WIDGET(preset_dropdown_), settings_.enabled);
  if (on_changed_) {
    try {
      on_changed_(settings_);
    } catch (...) {
    }
  }
}

void EqDialog::on_closed() {
  if (closed_) {
    return;
  }
  closed_ = true;
  window_ = nullptr;
  delete this;
}

}  // namespace radio::ui
