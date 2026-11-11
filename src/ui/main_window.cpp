#include "ui/main_window.hpp"

#include "app_icon.hpp"
#include "ui/connect_dialog.hpp"
#include "ui/cover_loader.hpp"
#include "ui/eq_dialog.hpp"
#include "ui/beat_stage.hpp"
#include "util/safe.hpp"
#include "util/scrobble.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <unordered_set>

namespace radio::ui {
namespace {

constexpr std::int64_t kNsPerSec = 1000000000LL;

GtkWidget* make_list_row(const std::string& title, const std::string& subtitle) {
  auto* row = gtk_list_box_row_new();
  auto* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  gtk_widget_set_margin_top(box, 8);
  gtk_widget_set_margin_bottom(box, 8);
  gtk_widget_set_margin_start(box, 10);
  gtk_widget_set_margin_end(box, 10);
  auto* t = gtk_label_new(title.c_str());
  gtk_widget_set_halign(t, GTK_ALIGN_START);
  gtk_label_set_ellipsize(GTK_LABEL(t), PANGO_ELLIPSIZE_END);
  auto* s = gtk_label_new(subtitle.c_str());
  gtk_widget_set_halign(s, GTK_ALIGN_START);
  gtk_widget_add_css_class(s, "dim-label");
  gtk_label_set_ellipsize(GTK_LABEL(s), PANGO_ELLIPSIZE_END);
  gtk_box_append(GTK_BOX(box), t);
  gtk_box_append(GTK_BOX(box), s);
  gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
  return row;
}

void clear_list(GtkListBox* list) {
  while (auto* child = gtk_widget_get_first_child(GTK_WIDGET(list))) {
    gtk_list_box_remove(list, child);
  }
}

}  // namespace

MainWindow::MainWindow(AdwApplication* app) : app_(app) {
  config_ = ConfigStore::load();
  player_.set_volume(config_.volume);
  player_.set_eq(config_.eq);

  player_.set_state_callback([this](audio::State state) {
    if (!ui_alive_ || !GTK_IS_BUTTON(play_pause_button_)) {
      return;
    }
    const char* icon =
        state == audio::State::Playing ? "media-playback-pause-symbolic"
                                       : "media-playback-start-symbolic";
    gtk_button_set_icon_name(play_pause_button_, icon);
    sync_beat_stage_active();
  });

  player_.set_progress_callback([this](std::int64_t pos, std::int64_t dur) {
    if (!ui_alive_ || seeking_ || dur <= 0) {
      return;
    }
    update_progress_labels(pos, dur);
    // Submit once the listen threshold is crossed, even before track end.
    maybe_submit_scrobble(false);
  });

  player_.set_eos_callback([this] {
    if (!ui_alive_) {
      return;
    }
    maybe_submit_scrobble(true);
    on_next();
  });

  player_.set_error_callback([this](const std::string& err) {
    if (!ui_alive_) {
      return;
    }
    set_status("Playback error: " + radio::util::friendly_network_message(err), 5);
  });
}

MainWindow::~MainWindow() {
  if (ui_alive_) {
    on_ui_destroyed();
  }
  if (window_) {
    gtk_window_destroy(GTK_WINDOW(window_));
    window_ = nullptr;
  }
}

void MainWindow::present() {
  build_ui();
  if (config_.minimal_player) {
    set_minimal_player(true);
  }
  gtk_window_present(GTK_WINDOW(window_));
  if (config_.server_url.empty() || config_.username.empty()) {
    show_connect_dialog(true);
  } else {
    connect_with(config_);
  }
}

void MainWindow::clear_container(GtkWidget* box) {
  while (auto* child = gtk_widget_get_first_child(box)) {
    gtk_box_remove(GTK_BOX(box), child);
  }
}

std::vector<subsonic::Artist> MainWindow::artists_from_frequent_albums(
    const std::vector<subsonic::Album>& albums, std::size_t limit) {
  std::vector<subsonic::Artist> out;
  std::unordered_set<std::string> seen;
  for (const auto& album : albums) {
    if (album.artist_id.empty() || !seen.insert(album.artist_id).second) {
      continue;
    }
    subsonic::Artist artist;
    artist.id = album.artist_id;
    artist.name = album.artist.empty() ? "Unknown Artist" : album.artist;
    artist.cover_art = album.cover_art;
    artist.play_count = album.play_count;
    out.push_back(std::move(artist));
    if (out.size() >= limit) {
      break;
    }
  }
  return out;
}

void MainWindow::on_ui_destroyed() {
  if (!ui_alive_) {
    return;
  }
  ui_alive_ = false;
  ++home_generation_;
  if (status_clear_id_) {
    g_source_remove(status_clear_id_);
    status_clear_id_ = 0;
  }
  if (session_save_id_) {
    g_source_remove(session_save_id_);
    session_save_id_ = 0;
  }
  stop_beat_stage_timer();
  if (beat_stage_) {
    beat_stage_->detach();
    beat_stage_.reset();
  }
  try {
    save_playback_session();
  } catch (...) {
  }
  client_.abort_pending();
  player_.set_state_callback({});
  player_.set_progress_callback({});
  player_.set_eos_callback({});
  player_.set_error_callback({});
  player_.stop();

  play_pause_button_ = nullptr;
  seek_scale_ = nullptr;
  time_elapsed_ = nullptr;
  time_total_ = nullptr;
  status_label_ = nullptr;
  favorite_button_ = nullptr;
  clear_rating_button_ = nullptr;
  cover_image_ = nullptr;
  now_title_ = nullptr;
  now_subtitle_ = nullptr;
  home_sections_ = nullptr;
  minimal_button_ = nullptr;
  eq_button_ = nullptr;
  rating_box_ = nullptr;
  clear_rating_button_ = nullptr;
  volume_box_ = nullptr;
  volume_scale_ = nullptr;
  toolbar_view_ = nullptr;
  browse_paned_ = nullptr;
  player_bar_ = nullptr;
  rating_buttons_.clear();
  window_ = nullptr;
}

void MainWindow::build_ui() {
  window_ = ADW_APPLICATION_WINDOW(adw_application_window_new(GTK_APPLICATION(app_)));
  gtk_window_set_title(GTK_WINDOW(window_), "Radio Player");
  gtk_window_set_default_size(GTK_WINDOW(window_), 1100, 720);
  radio::apply_window_icon(GTK_WINDOW(window_));
  ui_alive_ = true;
  g_signal_connect_swapped(window_, "destroy",
                           G_CALLBACK(+[](gpointer self) {
                             static_cast<MainWindow*>(self)->on_ui_destroyed();
                           }),
                           this);

  toolbar_view_ = ADW_TOOLBAR_VIEW(adw_toolbar_view_new());
  adw_application_window_set_content(window_, GTK_WIDGET(toolbar_view_));

  auto* header = adw_header_bar_new();
  adw_toolbar_view_add_top_bar(toolbar_view_, header);

  auto* connect_btn = gtk_button_new_with_label("Server");
  g_signal_connect_swapped(connect_btn, "clicked",
                           G_CALLBACK(+[](gpointer self) {
                             static_cast<MainWindow*>(self)->show_connect_dialog(false);
                           }),
                           this);
  adw_header_bar_pack_start(ADW_HEADER_BAR(header), connect_btn);

  auto* refresh_btn = gtk_button_new_from_icon_name("view-refresh-symbolic");
  gtk_widget_set_tooltip_text(refresh_btn, "Refresh Home from server");
  g_signal_connect_swapped(refresh_btn, "clicked",
                           G_CALLBACK(+[](gpointer self) {
                             static_cast<MainWindow*>(self)->load_home();
                           }),
                           this);
  adw_header_bar_pack_start(ADW_HEADER_BAR(header), refresh_btn);

  status_label_ = GTK_LABEL(gtk_label_new(""));
  gtk_widget_add_css_class(GTK_WIDGET(status_label_), "dim-label");
  adw_header_bar_pack_end(ADW_HEADER_BAR(header), GTK_WIDGET(status_label_));

  auto* outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  adw_toolbar_view_set_content(toolbar_view_, outer);

  browse_paned_ = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_set_vexpand(browse_paned_, TRUE);
  gtk_box_append(GTK_BOX(outer), browse_paned_);
  auto* paned = browse_paned_;

  auto* left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_size_request(left, 260, -1);
  gtk_paned_set_start_child(GTK_PANED(paned), left);
  gtk_paned_set_resize_start_child(GTK_PANED(paned), FALSE);

  auto* switcher = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_add_css_class(switcher, "linked");
  gtk_widget_set_margin_top(switcher, 8);
  gtk_widget_set_margin_bottom(switcher, 8);
  gtk_widget_set_margin_start(switcher, 8);
  gtk_widget_set_margin_end(switcher, 8);
  gtk_box_append(GTK_BOX(left), switcher);

  home_btn_ = GTK_TOGGLE_BUTTON(gtk_toggle_button_new_with_label("Home"));
  artists_btn_ = GTK_TOGGLE_BUTTON(gtk_toggle_button_new_with_label("Artists"));
  albums_btn_ = GTK_TOGGLE_BUTTON(gtk_toggle_button_new_with_label("Albums"));
  gtk_toggle_button_set_group(artists_btn_, home_btn_);
  gtk_toggle_button_set_group(albums_btn_, home_btn_);
  gtk_toggle_button_set_active(home_btn_, TRUE);
  gtk_box_append(GTK_BOX(switcher), GTK_WIDGET(home_btn_));
  gtk_box_append(GTK_BOX(switcher), GTK_WIDGET(artists_btn_));
  gtk_box_append(GTK_BOX(switcher), GTK_WIDGET(albums_btn_));

  browse_stack_ = GTK_STACK(gtk_stack_new());
  gtk_widget_set_vexpand(GTK_WIDGET(browse_stack_), TRUE);
  gtk_box_append(GTK_BOX(left), GTK_WIDGET(browse_stack_));

  beat_stage_ = std::make_unique<BeatStage>();
  gtk_stack_add_named(browse_stack_, beat_stage_->widget(), "home");

  auto* artist_scroll = gtk_scrolled_window_new();
  artist_list_ = GTK_LIST_BOX(gtk_list_box_new());
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(artist_scroll),
                                GTK_WIDGET(artist_list_));
  gtk_stack_add_named(browse_stack_, artist_scroll, "artists");

  auto* album_scroll = gtk_scrolled_window_new();
  album_list_ = GTK_LIST_BOX(gtk_list_box_new());
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(album_scroll),
                                GTK_WIDGET(album_list_));
  gtk_stack_add_named(browse_stack_, album_scroll, "albums");

  g_signal_connect(home_btn_, "toggled",
                   G_CALLBACK(+[](GtkToggleButton* btn, gpointer self) {
                     if (gtk_toggle_button_get_active(btn)) {
                       static_cast<MainWindow*>(self)->set_nav(NavPage::Home);
                     }
                   }),
                   this);
  g_signal_connect(artists_btn_, "toggled",
                   G_CALLBACK(+[](GtkToggleButton* btn, gpointer self) {
                     if (gtk_toggle_button_get_active(btn)) {
                       static_cast<MainWindow*>(self)->set_nav(NavPage::Artists);
                     }
                   }),
                   this);
  g_signal_connect(albums_btn_, "toggled",
                   G_CALLBACK(+[](GtkToggleButton* btn, gpointer self) {
                     if (gtk_toggle_button_get_active(btn)) {
                       static_cast<MainWindow*>(self)->set_nav(NavPage::Albums);
                     }
                   }),
                   this);

  g_signal_connect(
      artist_list_, "row-activated",
      G_CALLBACK(+[](GtkListBox*, GtkListBoxRow* row, gpointer self) {
        auto* w = static_cast<MainWindow*>(self);
        const int idx = gtk_list_box_row_get_index(row);
        if (idx >= 0 && idx < static_cast<int>(w->artists_.size())) {
          w->show_artist_albums(w->artists_[static_cast<std::size_t>(idx)]);
        }
      }),
      this);

  g_signal_connect(
      album_list_, "row-activated",
      G_CALLBACK(+[](GtkListBox*, GtkListBoxRow* row, gpointer self) {
        auto* w = static_cast<MainWindow*>(self);
        const int idx = gtk_list_box_row_get_index(row);
        if (idx >= 0 && idx < static_cast<int>(w->albums_.size())) {
          w->show_album_songs(w->albums_[static_cast<std::size_t>(idx)]);
        }
      }),
      this);

  main_stack_ = GTK_STACK(gtk_stack_new());
  gtk_paned_set_end_child(GTK_PANED(paned), GTK_WIDGET(main_stack_));

  // Home page
  auto* home_scroll = gtk_scrolled_window_new();
  home_sections_ = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 18));
  gtk_widget_set_margin_top(GTK_WIDGET(home_sections_), 16);
  gtk_widget_set_margin_bottom(GTK_WIDGET(home_sections_), 16);
  gtk_widget_set_margin_start(GTK_WIDGET(home_sections_), 16);
  gtk_widget_set_margin_end(GTK_WIDGET(home_sections_), 16);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(home_scroll),
                                GTK_WIDGET(home_sections_));
  gtk_stack_add_named(main_stack_, home_scroll, "home");

  // Library page
  auto* right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  content_title_ = GTK_LABEL(gtk_label_new("Select an artist or album"));
  gtk_widget_set_halign(GTK_WIDGET(content_title_), GTK_ALIGN_START);
  gtk_widget_add_css_class(GTK_WIDGET(content_title_), "title-2");
  gtk_widget_set_margin_top(GTK_WIDGET(content_title_), 12);
  gtk_widget_set_margin_start(GTK_WIDGET(content_title_), 12);
  gtk_widget_set_margin_end(GTK_WIDGET(content_title_), 12);
  gtk_box_append(GTK_BOX(right), GTK_WIDGET(content_title_));

  song_detail_label_ = GTK_LABEL(gtk_label_new(""));
  gtk_widget_set_halign(GTK_WIDGET(song_detail_label_), GTK_ALIGN_START);
  gtk_label_set_wrap(song_detail_label_, TRUE);
  gtk_widget_add_css_class(GTK_WIDGET(song_detail_label_), "dim-label");
  gtk_widget_set_margin_start(GTK_WIDGET(song_detail_label_), 12);
  gtk_widget_set_margin_end(GTK_WIDGET(song_detail_label_), 12);
  gtk_widget_set_margin_bottom(GTK_WIDGET(song_detail_label_), 8);
  gtk_box_append(GTK_BOX(right), GTK_WIDGET(song_detail_label_));

  auto* content_scroll = gtk_scrolled_window_new();
  gtk_widget_set_vexpand(content_scroll, TRUE);
  gtk_box_append(GTK_BOX(right), content_scroll);
  content_list_ = GTK_LIST_BOX(gtk_list_box_new());
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(content_scroll),
                                GTK_WIDGET(content_list_));
  gtk_stack_add_named(main_stack_, right, "library");

  g_signal_connect(
      content_list_, "row-activated",
      G_CALLBACK(+[](GtkListBox*, GtkListBoxRow* row, gpointer self) {
        auto* w = static_cast<MainWindow*>(self);
        const int idx = gtk_list_box_row_get_index(row);
        if (w->content_is_songs_) {
          if (idx >= 0 && idx < static_cast<int>(w->content_songs_.size())) {
            w->update_song_details(
                &w->content_songs_[static_cast<std::size_t>(idx)]);
            w->play_songs(w->content_songs_, static_cast<std::size_t>(idx));
          }
        } else if (idx >= 0 &&
                   idx < static_cast<int>(w->content_albums_.size())) {
          w->show_album_songs(w->content_albums_[static_cast<std::size_t>(idx)]);
        }
      }),
      this);

  g_signal_connect(
      content_list_, "row-selected",
      G_CALLBACK(+[](GtkListBox*, GtkListBoxRow* row, gpointer self) {
        auto* w = static_cast<MainWindow*>(self);
        if (!row || !w->content_is_songs_) {
          return;
        }
        const int idx = gtk_list_box_row_get_index(row);
        if (idx >= 0 && idx < static_cast<int>(w->content_songs_.size())) {
          w->update_song_details(
              &w->content_songs_[static_cast<std::size_t>(idx)]);
        }
      }),
      this);

  // Player bar
  player_bar_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
  gtk_widget_add_css_class(player_bar_, "toolbar");
  gtk_widget_set_margin_top(player_bar_, 8);
  gtk_widget_set_margin_bottom(player_bar_, 10);
  gtk_widget_set_margin_start(player_bar_, 12);
  gtk_widget_set_margin_end(player_bar_, 12);
  gtk_box_append(GTK_BOX(outer), player_bar_);

  auto* now_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_box_append(GTK_BOX(player_bar_), now_row);

  cover_image_ = GTK_IMAGE(gtk_image_new_from_icon_name("folder-music-symbolic"));
  gtk_image_set_pixel_size(cover_image_, 56);
  gtk_box_append(GTK_BOX(now_row), GTK_WIDGET(cover_image_));

  auto* now_text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  gtk_widget_set_hexpand(now_text, TRUE);
  gtk_box_append(GTK_BOX(now_row), now_text);
  now_title_ = GTK_LABEL(gtk_label_new("Nothing playing"));
  gtk_widget_set_halign(GTK_WIDGET(now_title_), GTK_ALIGN_START);
  gtk_label_set_ellipsize(now_title_, PANGO_ELLIPSIZE_END);
  now_subtitle_ = GTK_LABEL(gtk_label_new(""));
  gtk_widget_set_halign(GTK_WIDGET(now_subtitle_), GTK_ALIGN_START);
  gtk_widget_add_css_class(GTK_WIDGET(now_subtitle_), "dim-label");
  gtk_label_set_ellipsize(now_subtitle_, PANGO_ELLIPSIZE_END);
  gtk_box_append(GTK_BOX(now_text), GTK_WIDGET(now_title_));
  gtk_box_append(GTK_BOX(now_text), GTK_WIDGET(now_subtitle_));

  auto* seek_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_box_append(GTK_BOX(player_bar_), seek_row);
  time_elapsed_ = GTK_LABEL(gtk_label_new("0:00"));
  time_total_ = GTK_LABEL(gtk_label_new("0:00"));
  seek_scale_ =
      GTK_SCALE(gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 1, 0.001));
  gtk_scale_set_draw_value(seek_scale_, FALSE);
  gtk_widget_set_hexpand(GTK_WIDGET(seek_scale_), TRUE);
  gtk_box_append(GTK_BOX(seek_row), GTK_WIDGET(time_elapsed_));
  gtk_box_append(GTK_BOX(seek_row), GTK_WIDGET(seek_scale_));
  gtk_box_append(GTK_BOX(seek_row), GTK_WIDGET(time_total_));

  g_signal_connect(
      seek_scale_, "change-value",
      G_CALLBACK(+[](GtkRange*, GtkScrollType, gdouble value,
                     gpointer self) -> gboolean {
        auto* w = static_cast<MainWindow*>(self);
        w->seeking_ = true;
        w->player_.seek(value);
        w->seeking_ = false;
        return FALSE;
      }),
      this);

  auto* controls_row = gtk_center_box_new();
  gtk_box_append(GTK_BOX(player_bar_), controls_row);

  minimal_button_ = GTK_TOGGLE_BUTTON(gtk_toggle_button_new());
  gtk_button_set_icon_name(GTK_BUTTON(minimal_button_),
                           "view-restore-symbolic");
  gtk_widget_set_tooltip_text(GTK_WIDGET(minimal_button_),
                              "Minimal player view");
  gtk_widget_add_css_class(GTK_WIDGET(minimal_button_), "circular");
  gtk_widget_set_size_request(GTK_WIDGET(minimal_button_), 44, 44);
  gtk_widget_set_margin_end(GTK_WIDGET(minimal_button_), 8);
  gtk_center_box_set_start_widget(GTK_CENTER_BOX(controls_row),
                                  GTK_WIDGET(minimal_button_));

  auto* controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_center_box_set_center_widget(GTK_CENTER_BOX(controls_row), controls);

  shuffle_button_ = GTK_TOGGLE_BUTTON(gtk_toggle_button_new());
  gtk_button_set_icon_name(GTK_BUTTON(shuffle_button_),
                           "media-playlist-shuffle-symbolic");
  gtk_box_append(GTK_BOX(controls), GTK_WIDGET(shuffle_button_));

  auto* prev = gtk_button_new_from_icon_name("media-skip-backward-symbolic");
  play_pause_button_ =
      GTK_BUTTON(gtk_button_new_from_icon_name("media-playback-start-symbolic"));
  auto* next = gtk_button_new_from_icon_name("media-skip-forward-symbolic");
  gtk_box_append(GTK_BOX(controls), prev);
  gtk_box_append(GTK_BOX(controls), GTK_WIDGET(play_pause_button_));
  gtk_box_append(GTK_BOX(controls), next);

  repeat_button_ =
      GTK_BUTTON(gtk_button_new_from_icon_name("media-playlist-repeat-symbolic"));
  gtk_box_append(GTK_BOX(controls), GTK_WIDGET(repeat_button_));

  favorite_button_ = GTK_TOGGLE_BUTTON(gtk_toggle_button_new());
  gtk_button_set_icon_name(GTK_BUTTON(favorite_button_),
                           "non-starred-symbolic");
  gtk_widget_set_tooltip_text(GTK_WIDGET(favorite_button_),
                              "Favorite / unfavorite on server");
  gtk_widget_set_sensitive(GTK_WIDGET(favorite_button_), FALSE);
  gtk_box_append(GTK_BOX(controls), GTK_WIDGET(favorite_button_));

  rating_box_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_add_css_class(rating_box_, "linked");
  gtk_widget_set_tooltip_text(rating_box_, "Your rating (saved on Navidrome)");
  gtk_box_append(GTK_BOX(controls), rating_box_);
  rating_buttons_.clear();
  GtkToggleButton* rating_group = nullptr;
  for (int i = 1; i <= 5; ++i) {
    auto* btn = GTK_TOGGLE_BUTTON(gtk_toggle_button_new_with_label(
        std::to_string(i).c_str()));
    if (rating_group) {
      gtk_toggle_button_set_group(btn, rating_group);
    } else {
      rating_group = btn;
    }
    gtk_widget_set_sensitive(GTK_WIDGET(btn), FALSE);
    gtk_box_append(GTK_BOX(rating_box_), GTK_WIDGET(btn));
    rating_buttons_.push_back(btn);
    g_signal_connect(
        btn, "toggled",
        G_CALLBACK(+[](GtkToggleButton* button, gpointer self) {
          auto* w = static_cast<MainWindow*>(self);
          if (w->updating_annotation_ui_ ||
              !gtk_toggle_button_get_active(button)) {
            return;
          }
          const char* label = gtk_button_get_label(GTK_BUTTON(button));
          w->on_set_rating(label ? std::atoi(label) : 0);
        }),
        this);
  }

  clear_rating_button_ = GTK_BUTTON(gtk_button_new_with_label("0"));
  gtk_widget_set_tooltip_text(GTK_WIDGET(clear_rating_button_), "Clear rating");
  gtk_widget_set_sensitive(GTK_WIDGET(clear_rating_button_), FALSE);
  gtk_box_append(GTK_BOX(controls), GTK_WIDGET(clear_rating_button_));
  g_signal_connect_swapped(clear_rating_button_, "clicked",
                           G_CALLBACK(+[](gpointer self) {
                             static_cast<MainWindow*>(self)->on_set_rating(0);
                           }),
                           this);

  volume_box_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_widget_set_margin_start(volume_box_, 24);
  gtk_box_append(GTK_BOX(controls), volume_box_);
  gtk_box_append(GTK_BOX(volume_box_),
                 gtk_image_new_from_icon_name("audio-volume-high-symbolic"));
  volume_scale_ =
      GTK_SCALE(gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 1, 0.01));
  gtk_widget_set_size_request(GTK_WIDGET(volume_scale_), 120, -1);
  gtk_scale_set_draw_value(volume_scale_, FALSE);
  gtk_range_set_value(GTK_RANGE(volume_scale_), config_.volume);
  gtk_box_append(GTK_BOX(volume_box_), GTK_WIDGET(volume_scale_));

  eq_button_ = GTK_BUTTON(gtk_button_new_with_label("EQ"));
  gtk_widget_set_tooltip_text(GTK_WIDGET(eq_button_), "Open equalizer");
  gtk_widget_add_css_class(GTK_WIDGET(eq_button_), "circular");
  gtk_widget_set_size_request(GTK_WIDGET(eq_button_), 44, 44);
  gtk_widget_set_margin_start(GTK_WIDGET(eq_button_), 8);
  gtk_center_box_set_end_widget(GTK_CENTER_BOX(controls_row),
                                GTK_WIDGET(eq_button_));
  if (config_.eq.enabled) {
    gtk_widget_add_css_class(GTK_WIDGET(eq_button_), "suggested-action");
  }

  auto* css = gtk_css_provider_new();
  gtk_css_provider_load_from_string(
      css,
      ".circular {"
      "  border-radius: 999px;"
      "  min-width: 44px;"
      "  min-height: 44px;"
      "  padding: 0;"
      "  font-weight: 700;"
      "}"
      ".beat-stage {"
      "  background-color: #080a14;"
      "  border-radius: 8px;"
      "  margin: 8px;"
      "}");
  gtk_style_context_add_provider_for_display(
      gdk_display_get_default(), GTK_STYLE_PROVIDER(css),
      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref(css);

  g_signal_connect_swapped(eq_button_, "clicked",
                           G_CALLBACK(+[](gpointer self) {
                             static_cast<MainWindow*>(self)->on_open_eq();
                           }),
                           this);
  g_signal_connect_swapped(minimal_button_, "toggled",
                           G_CALLBACK(+[](gpointer self) {
                             static_cast<MainWindow*>(self)
                                 ->on_toggle_minimal_player();
                           }),
                           this);

  g_signal_connect_swapped(prev, "clicked",
                           G_CALLBACK(+[](gpointer s) {
                             static_cast<MainWindow*>(s)->on_prev();
                           }),
                           this);
  g_signal_connect_swapped(play_pause_button_, "clicked",
                           G_CALLBACK(+[](gpointer s) {
                             static_cast<MainWindow*>(s)->on_play_pause();
                           }),
                           this);
  g_signal_connect_swapped(next, "clicked",
                           G_CALLBACK(+[](gpointer s) {
                             static_cast<MainWindow*>(s)->on_next();
                           }),
                           this);
  g_signal_connect_swapped(shuffle_button_, "toggled",
                           G_CALLBACK(+[](gpointer s) {
                             static_cast<MainWindow*>(s)->on_shuffle();
                           }),
                           this);
  g_signal_connect_swapped(repeat_button_, "clicked",
                           G_CALLBACK(+[](gpointer s) {
                             static_cast<MainWindow*>(s)->on_repeat();
                           }),
                           this);
  g_signal_connect(
      favorite_button_, "toggled",
      G_CALLBACK(+[](GtkToggleButton*, gpointer self) {
        auto* w = static_cast<MainWindow*>(self);
        if (!w->updating_annotation_ui_) {
          w->on_toggle_favorite();
        }
      }),
      this);
  g_signal_connect(
      volume_scale_, "value-changed",
      G_CALLBACK(+[](GtkRange* range, gpointer self) {
        auto* w = static_cast<MainWindow*>(self);
        w->player_.set_volume(gtk_range_get_value(range));
        w->config_.volume = gtk_range_get_value(range);
        ConfigStore::save(w->config_);
      }),
      this);

  set_nav(NavPage::Home);

  session_save_id_ = g_timeout_add_seconds(
      5,
      +[](gpointer self) -> gboolean {
        auto* w = static_cast<MainWindow*>(self);
        if (!w->ui_alive_) {
          w->session_save_id_ = 0;
          return G_SOURCE_REMOVE;
        }
        if (w->player_.state() != audio::State::Stopped &&
            w->queue_.current()) {
          w->save_playback_session();
        }
        return G_SOURCE_CONTINUE;
      },
      this);
}

void MainWindow::set_nav(NavPage page) {
  current_nav_ = page;
  switch (page) {
    case NavPage::Home:
      gtk_stack_set_visible_child_name(browse_stack_, "home");
      gtk_stack_set_visible_child_name(main_stack_, "home");
      load_home();
      break;
    case NavPage::Artists:
      gtk_stack_set_visible_child_name(browse_stack_, "artists");
      gtk_stack_set_visible_child_name(main_stack_, "library");
      if (artists_.empty()) {
        load_artists();
      }
      break;
    case NavPage::Albums:
      gtk_stack_set_visible_child_name(browse_stack_, "albums");
      gtk_stack_set_visible_child_name(main_stack_, "library");
      if (albums_.empty()) {
        load_albums();
      }
      break;
  }
  sync_beat_stage_active();
}

GtkWidget* MainWindow::make_home_section(const char* title, GtkWidget* row) {
  auto* section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  auto* label = gtk_label_new(title);
  gtk_widget_set_halign(label, GTK_ALIGN_START);
  gtk_widget_add_css_class(label, "title-3");
  gtk_box_append(GTK_BOX(section), label);

  auto* scroll = gtk_scrolled_window_new();
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                 GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), row);
  gtk_widget_set_size_request(scroll, -1, 170);
  gtk_box_append(GTK_BOX(section), scroll);
  return section;
}

void MainWindow::load_tile_cover(GtkImage* image, const std::string& cover_id) {
  CoverLoader::load(image, client_, cover_id, 120, 96);
}

GtkWidget* MainWindow::make_album_tile(const subsonic::Album& album) {
  auto* button = gtk_button_new();
  gtk_widget_set_size_request(button, 120, -1);
  auto* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
  auto* image = GTK_IMAGE(gtk_image_new());
  gtk_image_set_pixel_size(image, 96);
  if (!CoverLoader::apply_cached(image, album.cover_art, 120, 96)) {
    gtk_image_set_from_icon_name(image, "media-optical-cd-audio-symbolic");
    CoverLoader::load_async(image, client_, album.cover_art, 120, 96);
  }
  gtk_box_append(GTK_BOX(box), GTK_WIDGET(image));
  auto* title =
      gtk_label_new(annotate_title(album.name, album.annotations).c_str());
  gtk_label_set_ellipsize(GTK_LABEL(title), PANGO_ELLIPSIZE_END);
  gtk_label_set_width_chars(GTK_LABEL(title), 14);
  gtk_label_set_max_width_chars(GTK_LABEL(title), 14);
  std::string sub = album.artist;
  const auto badge = format_annotations(album.annotations);
  if (!badge.empty()) {
    sub += " · " + badge;
  }
  auto* subtitle = gtk_label_new(sub.c_str());
  gtk_widget_add_css_class(subtitle, "dim-label");
  gtk_label_set_ellipsize(GTK_LABEL(subtitle), PANGO_ELLIPSIZE_END);
  gtk_label_set_width_chars(GTK_LABEL(subtitle), 14);
  gtk_box_append(GTK_BOX(box), title);
  gtk_box_append(GTK_BOX(box), subtitle);
  gtk_button_set_child(GTK_BUTTON(button), box);

  // GTK signal handlers are C function pointers, so they cannot capture locals.
  // Heap-allocate (window*, album*) and free it in the GClosure notify below.
  auto* album_copy = new subsonic::Album(album);
  auto album_clicked = +[](GtkButton*, gpointer data) {
    auto* payload =
        static_cast<std::pair<MainWindow*, subsonic::Album*>*>(data);
    payload->first->show_album_songs(*payload->second);
    gtk_toggle_button_set_active(payload->first->artists_btn_, FALSE);
    gtk_toggle_button_set_active(payload->first->albums_btn_, TRUE);
    gtk_stack_set_visible_child_name(payload->first->main_stack_, "library");
    gtk_stack_set_visible_child_name(payload->first->browse_stack_, "albums");
  };
  auto album_destroy = +[](gpointer data, GClosure*) {
    auto* payload =
        static_cast<std::pair<MainWindow*, subsonic::Album*>*>(data);
    delete payload->second;
    delete payload;
  };
  g_signal_connect_data(button, "clicked", G_CALLBACK(album_clicked),
                        new std::pair<MainWindow*, subsonic::Album*>(this,
                                                                    album_copy),
                        album_destroy, static_cast<GConnectFlags>(0));
  return button;
}

GtkWidget* MainWindow::make_artist_tile(const subsonic::Artist& artist) {
  auto* button = gtk_button_new();
  gtk_widget_set_size_request(button, 120, -1);
  auto* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
  auto* image = GTK_IMAGE(gtk_image_new());
  gtk_image_set_pixel_size(image, 96);
  if (!CoverLoader::apply_cached(image, artist.cover_art, 120, 96)) {
    gtk_image_set_from_icon_name(image, "avatar-default-symbolic");
    CoverLoader::load_async(image, client_, artist.cover_art, 120, 96);
  }
  gtk_box_append(GTK_BOX(box), GTK_WIDGET(image));
  auto* title =
      gtk_label_new(annotate_title(artist.name, artist.annotations).c_str());
  gtk_label_set_ellipsize(GTK_LABEL(title), PANGO_ELLIPSIZE_END);
  gtk_label_set_width_chars(GTK_LABEL(title), 14);
  gtk_box_append(GTK_BOX(box), title);
  const auto badge = format_annotations(artist.annotations);
  if (!badge.empty()) {
    auto* subtitle = gtk_label_new(badge.c_str());
    gtk_widget_add_css_class(subtitle, "dim-label");
    gtk_label_set_ellipsize(GTK_LABEL(subtitle), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(box), subtitle);
  }
  gtk_button_set_child(GTK_BUTTON(button), box);

  auto* artist_copy = new subsonic::Artist(artist);
  auto artist_clicked = +[](GtkButton*, gpointer data) {
    auto* payload =
        static_cast<std::pair<MainWindow*, subsonic::Artist*>*>(data);
    payload->first->show_artist_albums(*payload->second);
    gtk_toggle_button_set_active(payload->first->home_btn_, FALSE);
    gtk_toggle_button_set_active(payload->first->artists_btn_, TRUE);
    gtk_stack_set_visible_child_name(payload->first->main_stack_, "library");
    gtk_stack_set_visible_child_name(payload->first->browse_stack_, "artists");
  };
  auto artist_destroy = +[](gpointer data, GClosure*) {
    auto* payload =
        static_cast<std::pair<MainWindow*, subsonic::Artist*>*>(data);
    delete payload->second;
    delete payload;
  };
  g_signal_connect_data(
      button, "clicked", G_CALLBACK(artist_clicked),
      new std::pair<MainWindow*, subsonic::Artist*>(this, artist_copy),
      artist_destroy, static_cast<GConnectFlags>(0));
  return button;
}

GtkWidget* MainWindow::make_song_tile(const subsonic::Song& song,
                                      const std::vector<subsonic::Song>& context,
                                      std::size_t index) {
  auto* button = gtk_button_new();
  gtk_widget_set_size_request(button, 120, -1);
  auto* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
  auto* image = GTK_IMAGE(gtk_image_new());
  gtk_image_set_pixel_size(image, 96);
  if (!CoverLoader::apply_cached(image, song.cover_art, 120, 96)) {
    gtk_image_set_from_icon_name(image, "audio-x-generic-symbolic");
    CoverLoader::load_async(image, client_, song.cover_art, 120, 96);
  }
  gtk_box_append(GTK_BOX(box), GTK_WIDGET(image));
  auto* title =
      gtk_label_new(annotate_title(song.title, song.annotations).c_str());
  gtk_label_set_ellipsize(GTK_LABEL(title), PANGO_ELLIPSIZE_END);
  gtk_label_set_width_chars(GTK_LABEL(title), 14);
  std::string sub = song.artist;
  const auto badge = format_annotations(song.annotations);
  if (!badge.empty()) {
    sub += " · " + badge;
  }
  auto* subtitle = gtk_label_new(sub.c_str());
  gtk_widget_add_css_class(subtitle, "dim-label");
  gtk_label_set_ellipsize(GTK_LABEL(subtitle), PANGO_ELLIPSIZE_END);
  gtk_box_append(GTK_BOX(box), title);
  gtk_box_append(GTK_BOX(box), subtitle);
  gtk_button_set_child(GTK_BUTTON(button), box);

  struct SongPayload {
    MainWindow* window;
    std::vector<subsonic::Song> songs;
    std::size_t index;
  };
  auto* payload = new SongPayload{this, context, index};
  auto song_clicked = +[](GtkButton*, gpointer data) {
    auto* p = static_cast<SongPayload*>(data);
    p->window->play_songs(p->songs, p->index);
  };
  auto song_destroy = +[](gpointer data, GClosure*) {
    delete static_cast<SongPayload*>(data);
  };
  g_signal_connect_data(button, "clicked", G_CALLBACK(song_clicked), payload,
                        song_destroy, static_cast<GConnectFlags>(0));
  return button;
}

void MainWindow::load_home() {
  if (!ui_alive_ || !home_sections_) {
    return;
  }
  if (!client_.configured()) {
    clear_container(GTK_WIDGET(home_sections_));
    auto* hint = gtk_label_new("Connect to a Navidrome server to load Home.");
    gtk_widget_add_css_class(hint, "dim-label");
    gtk_widget_set_halign(hint, GTK_ALIGN_START);
    gtk_box_append(home_sections_, hint);
    return;
  }

  set_status("Loading Home from server…", 0);
  // Bump generation so in-flight replies from a previous refresh are ignored.
  ++home_generation_;
  const int generation = home_generation_;
  home_loads_pending_ = 6;  // recent/frequent/newest/highest/starred/random
  home_recent_.clear();
  home_frequent_.clear();
  home_newest_.clear();
  home_highest_.clear();
  home_top_artists_.clear();
  home_starred_artists_.clear();
  home_starred_albums_.clear();
  home_starred_songs_.clear();
  home_random_songs_.clear();

  // Shared by all six Home requests: rebuild the UI only after the last reply
  // for *this* generation arrives. Captures `this` + generation by value.
  auto finish_one = [this, generation] {
    if (!ui_alive_ || generation != home_generation_ || !home_sections_) {
      return;
    }
    if (--home_loads_pending_ > 0) {
      return;  // still waiting on sibling requests
    }

    clear_container(GTK_WIDGET(home_sections_));

    auto add_album_row = [this](const char* title,
                                const std::vector<subsonic::Album>& albums) {
      if (albums.empty()) {
        return;
      }
      auto* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
      for (const auto& album : albums) {
        gtk_box_append(GTK_BOX(row), make_album_tile(album));
      }
      gtk_box_append(home_sections_, make_home_section(title, row));
    };
    auto add_artist_row = [this](const char* title,
                                 const std::vector<subsonic::Artist>& artists) {
      if (artists.empty()) {
        return;
      }
      auto* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
      for (const auto& artist : artists) {
        gtk_box_append(GTK_BOX(row), make_artist_tile(artist));
      }
      gtk_box_append(home_sections_, make_home_section(title, row));
    };
    auto add_song_row = [this](const char* title,
                               const std::vector<subsonic::Song>& songs) {
      if (songs.empty()) {
        return;
      }
      auto* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
      for (std::size_t i = 0; i < songs.size(); ++i) {
        gtk_box_append(GTK_BOX(row), make_song_tile(songs[i], songs, i));
      }
      gtk_box_append(home_sections_, make_home_section(title, row));
    };

    add_album_row("Recently Played", home_recent_);
    add_album_row("Most Played Albums", home_frequent_);
    add_artist_row("Top Artists", home_top_artists_);
    add_album_row("Recently Added", home_newest_);
    add_album_row("Highest Rated", home_highest_);
    add_artist_row("Favorite Artists", home_starred_artists_);
    add_album_row("Favorite Albums", home_starred_albums_);
    add_song_row("Favorite Songs", home_starred_songs_);
    add_song_row("Discover", home_random_songs_);
    set_status("Home updated from server");
  };

  client_.get_album_list(
      "recent", 20, 0,
      [this, generation, finish_one](std::vector<subsonic::Album> albums,
                                     std::optional<subsonic::ClientError> err) {
        if (!ui_alive_) {
          return;
        }
        if (generation == home_generation_ && !err) {
          home_recent_ = std::move(albums);
        } else if (generation == home_generation_ && err &&
                   radio::util::looks_like_network_error(err->message)) {
          set_status(radio::util::friendly_network_message(err->message), 5);
        }
        finish_one();
      });

  client_.get_album_list(
      "frequent", 30, 0,
      [this, generation, finish_one](std::vector<subsonic::Album> albums,
                                     std::optional<subsonic::ClientError> err) {
        if (!ui_alive_) {
          return;
        }
        if (generation == home_generation_ && !err) {
          home_frequent_ = std::move(albums);
          home_top_artists_ =
              artists_from_frequent_albums(home_frequent_, 20);
        }
        finish_one();
      });

  client_.get_album_list(
      "newest", 20, 0,
      [this, generation, finish_one](std::vector<subsonic::Album> albums,
                                     std::optional<subsonic::ClientError> err) {
        if (!ui_alive_) {
          return;
        }
        if (generation == home_generation_ && !err) {
          home_newest_ = std::move(albums);
        }
        finish_one();
      });

  client_.get_album_list(
      "highest", 20, 0,
      [this, generation, finish_one](std::vector<subsonic::Album> albums,
                                     std::optional<subsonic::ClientError> err) {
        if (!ui_alive_) {
          return;
        }
        if (generation == home_generation_ && !err) {
          home_highest_ = std::move(albums);
        }
        finish_one();
      });

  client_.get_starred(
      [this, generation, finish_one](subsonic::Starred starred,
                                     std::optional<subsonic::ClientError> err) {
        if (!ui_alive_) {
          return;
        }
        if (generation == home_generation_ && !err) {
          home_starred_artists_ = std::move(starred.artists);
          home_starred_albums_ = std::move(starred.albums);
          home_starred_songs_ = std::move(starred.songs);
          if (home_starred_songs_.size() > 20) {
            home_starred_songs_.resize(20);
          }
        }
        finish_one();
      });

  client_.get_random_songs(
      20,
      [this, generation, finish_one](std::vector<subsonic::Song> songs,
                                     std::optional<subsonic::ClientError> err) {
        if (!ui_alive_) {
          return;
        }
        if (generation == home_generation_ && !err) {
          home_random_songs_ = std::move(songs);
        }
        finish_one();
      });
}

void MainWindow::show_connect_dialog(bool force) {
  (void)force;
  auto* dlg = new ConnectDialog(GTK_WINDOW(window_), config_);
  dlg->present([this, dlg](std::optional<ConnectResult> result) {
    std::unique_ptr<ConnectDialog> guard(dlg);
    if (!result) {
      return;
    }
    config_.server_url = result->server_url;
    config_.username = result->username;
    config_.password = result->password;
    ConfigStore::save(config_);
    connect_with(config_);
  });
}

void MainWindow::connect_with(const AppConfig& cfg) {
  if (cfg.server_url.empty() || cfg.username.empty()) {
    set_status("Server URL and username are required", 5);
    show_connect_dialog(true);
    return;
  }
  set_status("Connecting…", 0);
  client_.set_credentials(cfg.server_url, cfg.username, cfg.password);
  client_.ping([this](std::optional<subsonic::ClientError> err) {
    if (!ui_alive_) {
      return;
    }
    if (err) {
      set_status("Connect failed: " +
                     radio::util::friendly_network_message(err->message),
                 5);
      show_connect_dialog(true);
      return;
    }
    set_status("Connected to " + client_.base_url());
    restore_playback_session();
    load_home();
    load_artists();
  });
}

void MainWindow::load_artists() {
  if (!client_.configured()) {
    set_status("Not connected to a server", 5);
    return;
  }
  set_status("Loading artists…", 0);
  client_.get_artists([this](std::vector<subsonic::Artist> artists,
                             std::optional<subsonic::ClientError> err) {
    if (!ui_alive_) {
      return;
    }
    if (err) {
      set_status(radio::util::friendly_network_message(err->message), 5);
      return;
    }
    artists_ = std::move(artists);
    clear_list(artist_list_);
    for (const auto& a : artists_) {
      std::string sub =
          std::to_string(a.album_count) +
          (a.album_count == 1 ? " album" : " albums");
      const auto badge = format_annotations(a.annotations);
      if (!badge.empty()) {
        sub += " · " + badge;
      }
      gtk_list_box_append(
          artist_list_,
          make_list_row(annotate_title(a.name, a.annotations), sub));
    }
    set_status(std::to_string(artists_.size()) + " artists");
  });
}

void MainWindow::load_albums() {
  if (!client_.configured()) {
    set_status("Not connected to a server", 5);
    return;
  }
  set_status("Loading albums…", 0);
  client_.get_album_list(
      "alphabeticalByName", 500, 0,
      [this](std::vector<subsonic::Album> albums,
             std::optional<subsonic::ClientError> err) {
        if (!ui_alive_) {
          return;
        }
        if (err) {
          set_status(radio::util::friendly_network_message(err->message), 5);
          return;
        }
        albums_ = std::move(albums);
        clear_list(album_list_);
        for (const auto& a : albums_) {
          std::string sub = a.artist;
          const auto badge = format_annotations(a.annotations);
          if (!badge.empty()) {
            sub += " · " + badge;
          }
          gtk_list_box_append(
              album_list_,
              make_list_row(annotate_title(a.name, a.annotations), sub));
        }
        set_status(std::to_string(albums_.size()) + " albums");
      });
}

void MainWindow::show_artist_albums(const subsonic::Artist& artist) {
  if (!ui_alive_ || artist.id.empty()) {
    return;
  }
  gtk_stack_set_visible_child_name(main_stack_, "library");
  set_status("Loading albums for " + artist.name + "…", 0);
  client_.get_artist_albums(
      artist.id,
      [this, name = artist.name](std::vector<subsonic::Album> albums,
                                 std::optional<subsonic::ClientError> err) {
        if (!ui_alive_) {
          return;
        }
        if (err) {
          set_status(radio::util::friendly_network_message(err->message), 5);
          return;
        }
        content_is_songs_ = false;
        content_albums_ = std::move(albums);
        content_songs_.clear();
        clear_list(content_list_);
        gtk_label_set_text(content_title_, name.c_str());
        gtk_label_set_text(song_detail_label_, "Select an album");
        for (const auto& a : content_albums_) {
          std::string sub = a.artist;
          if (a.year > 0) {
            sub += " · " + std::to_string(a.year);
          }
          const auto badge = format_annotations(a.annotations);
          if (!badge.empty()) {
            sub += " · " + badge;
          }
          gtk_list_box_append(
              content_list_,
              make_list_row(annotate_title(a.name, a.annotations), sub));
        }
        set_status(std::to_string(content_albums_.size()) + " albums");
      });
}

void MainWindow::show_album_songs(const subsonic::Album& album) {
  if (!ui_alive_ || album.id.empty()) {
    return;
  }
  gtk_stack_set_visible_child_name(main_stack_, "library");
  set_status("Loading songs…", 0);
  client_.get_album_songs(
      album.id,
      [this, album](std::vector<subsonic::Song> songs,
                    std::optional<subsonic::ClientError> err) {
        if (!ui_alive_) {
          return;
        }
        if (err) {
          set_status(radio::util::friendly_network_message(err->message), 5);
          return;
        }
        content_is_songs_ = true;
        content_songs_ = std::move(songs);
        content_albums_.clear();
        clear_list(content_list_);
        gtk_label_set_text(content_title_, album.name.c_str());
        gtk_label_set_text(song_detail_label_,
                           (album.artist + " · " +
                            std::to_string(content_songs_.size()) + " tracks")
                               .c_str());
        for (const auto& s : content_songs_) {
          gtk_list_box_append(content_list_,
                              make_list_row(song_list_title(s),
                                            song_list_subtitle(s)));
        }
        set_status(std::to_string(content_songs_.size()) + " songs");
      });
}

void MainWindow::update_song_details(const subsonic::Song* song) {
  if (!song) {
    has_selected_song_ = false;
    gtk_label_set_text(song_detail_label_, "");
    return;
  }
  selected_song_ = *song;
  has_selected_song_ = true;
  std::string detail = annotate_title(song->title, song->annotations);
  detail += " — " + song->artist;
  if (!song->album.empty()) {
    detail += " · " + song->album;
  }
  detail += " · " + format_time(song->duration);
  if (song->year > 0) {
    detail += " · " + std::to_string(song->year);
  }
  if (!song->genre.empty()) {
    detail += " · " + song->genre;
  }
  if (song->bit_rate > 0) {
    detail += " · " + std::to_string(song->bit_rate) + " kbps";
  }
  if (song->play_count > 0) {
    detail += " · " + std::to_string(song->play_count) + " plays";
  }
  const auto badge = format_annotations(song->annotations);
  if (!badge.empty()) {
    detail += " · " + badge;
  }
  if (song->annotations.average_rating > 0.0 &&
      song->annotations.user_rating == 0) {
    detail +=
        " · community " +
        format_rating_stars(
            static_cast<int>(std::lround(song->annotations.average_rating)));
  }
  if (!song->suffix.empty()) {
    detail += " · " + song->suffix;
  }
  if (GTK_IS_LABEL(song_detail_label_)) {
    gtk_label_set_text(song_detail_label_, detail.c_str());
  }
}

void MainWindow::sync_playing_annotation_controls() {
  if (const auto* song = queue_.current()) {
    sync_annotation_controls(song->annotations, true);
  } else {
    sync_annotation_controls({}, false);
  }
}

void MainWindow::sync_annotation_controls(
    const subsonic::Annotations& annotations, bool enabled) {
  if (!GTK_IS_TOGGLE_BUTTON(favorite_button_) ||
      !GTK_IS_BUTTON(clear_rating_button_)) {
    return;
  }
  updating_annotation_ui_ = true;
  gtk_widget_set_sensitive(GTK_WIDGET(favorite_button_), enabled);
  gtk_widget_set_sensitive(GTK_WIDGET(clear_rating_button_), enabled);
  gtk_toggle_button_set_active(favorite_button_, annotations.starred);
  gtk_button_set_icon_name(GTK_BUTTON(favorite_button_),
                           annotations.starred ? "starred-symbolic"
                                               : "non-starred-symbolic");
  const int rating = annotations.user_rating;
  for (std::size_t i = 0; i < rating_buttons_.size(); ++i) {
    auto* btn = rating_buttons_[i];
    if (!GTK_IS_TOGGLE_BUTTON(btn)) {
      continue;
    }
    const int value = static_cast<int>(i) + 1;
    gtk_widget_set_sensitive(GTK_WIDGET(btn), enabled);
    // Highlight only 1–5; rating 0 means none selected.
    const bool selected = rating >= 1 && rating <= 5 && rating == value;
    gtk_toggle_button_set_active(btn, selected);
    if (selected) {
      gtk_widget_add_css_class(GTK_WIDGET(btn), "suggested-action");
    } else {
      gtk_widget_remove_css_class(GTK_WIDGET(btn), "suggested-action");
    }
  }
  updating_annotation_ui_ = false;
}

void MainWindow::maybe_submit_scrobble(bool force) {
  // Navidrome increments play count only on scrobble(submission=true).
  if (current_song_id_.empty()) {
    return;
  }
  if (!util::scrobble_eligible(player_.position_ns(), player_.duration_ns(),
                               force, scrobbled_current_)) {
    return;
  }
  client_.scrobble(current_song_id_, true);
  scrobbled_current_ = true;
}

void MainWindow::play_songs(std::vector<subsonic::Song> songs,
                            std::size_t start_index) {
  maybe_submit_scrobble(false);
  resume_position_ns_ = -1;
  queue_.replace(std::move(songs), start_index);
  play_current();
}

void MainWindow::update_progress_labels(std::int64_t position_ns,
                                        std::int64_t duration_ns) {
  if (!GTK_IS_RANGE(seek_scale_) || !GTK_IS_LABEL(time_elapsed_) ||
      !GTK_IS_LABEL(time_total_) || duration_ns <= 0) {
    return;
  }
  const double frac =
      static_cast<double>(position_ns) / static_cast<double>(duration_ns);
  gtk_range_set_value(GTK_RANGE(seek_scale_), std::clamp(frac, 0.0, 1.0));
  gtk_label_set_text(time_elapsed_, format_ns(position_ns).c_str());
  gtk_label_set_text(time_total_, format_ns(duration_ns).c_str());
}

void MainWindow::sync_transport_buttons() {
  if (!GTK_IS_TOGGLE_BUTTON(shuffle_button_) || !GTK_IS_BUTTON(repeat_button_)) {
    return;
  }
  updating_annotation_ui_ = true;
  gtk_toggle_button_set_active(shuffle_button_, queue_.shuffle());
  updating_annotation_ui_ = false;

  const char* icon = "media-playlist-repeat-symbolic";
  switch (queue_.repeat()) {
    case queue::RepeatMode::Off:
      icon = "media-playlist-repeat-symbolic";
      gtk_widget_remove_css_class(GTK_WIDGET(repeat_button_), "suggested-action");
      break;
    case queue::RepeatMode::All:
      icon = "media-playlist-repeat-symbolic";
      gtk_widget_add_css_class(GTK_WIDGET(repeat_button_), "suggested-action");
      break;
    case queue::RepeatMode::One:
      icon = "media-playlist-repeat-song-symbolic";
      gtk_widget_add_css_class(GTK_WIDGET(repeat_button_), "suggested-action");
      break;
  }
  gtk_button_set_icon_name(repeat_button_, icon);
}

void MainWindow::save_playback_session() {
  if (client_.base_url().empty() || queue_.empty() || !queue_.current()) {
    return;
  }
  PlaybackSession session;
  session.server_url = client_.base_url();
  session.songs = queue_.songs();
  session.order = queue_.order();
  session.current = queue_.current_index();
  session.shuffle = queue_.shuffle();
  session.repeat = queue_.repeat();
  if (player_.state() != audio::State::Stopped) {
    session.position_ns = player_.position_ns();
  } else if (resume_position_ns_ > 0) {
    session.position_ns = resume_position_ns_;
  } else {
    session.position_ns = 0;
  }
  SessionStore::save(session);
}

void MainWindow::restore_playback_session() {
  const auto session = SessionStore::load();
  if (session.songs.empty() || session.server_url != client_.base_url()) {
    return;
  }
  queue_.restore(session.songs, session.order, session.current, session.shuffle,
                 session.repeat);
  const auto* song = queue_.current();
  if (!song) {
    return;
  }

  resume_position_ns_ = std::max<std::int64_t>(session.position_ns, 0);
  current_song_id_ = song->id;
  scrobbled_current_ = false;
  set_now_playing(*song);
  update_song_details(song);
  sync_playing_annotation_controls();
  sync_transport_buttons();

  const std::int64_t duration_ns =
      song->duration > 0 ? static_cast<std::int64_t>(song->duration) * kNsPerSec
                         : 0;
  if (duration_ns > 0) {
    update_progress_labels(resume_position_ns_, duration_ns);
  } else if (resume_position_ns_ > 0) {
    update_progress_labels(resume_position_ns_, resume_position_ns_);
  }

  set_status("Ready to resume — press play");
}

void MainWindow::play_current() {
  const auto* song = queue_.current();
  if (!song) {
    sync_playing_annotation_controls();
    return;
  }
  const std::string uri = client_.stream_url(song->id);
  if (uri.empty()) {
    set_status("Cannot stream: not connected or missing song id", 5);
    return;
  }
  current_song_id_ = song->id;
  scrobbled_current_ = false;
  set_now_playing(*song);
  update_song_details(song);
  sync_playing_annotation_controls();
  const std::int64_t start_ns = resume_position_ns_;
  resume_position_ns_ = -1;
  player_.play_uri(uri, start_ns);
  // Now-playing notification (does not increment play count).
  client_.scrobble(song->id, false);
  set_status("Playing " + song->title);
  save_playback_session();
}

void MainWindow::apply_song_annotation_locally(
    const std::string& song_id, const subsonic::Annotations& annotations) {
  auto patch = [&](subsonic::Song& song) {
    if (song.id == song_id) {
      song.annotations = annotations;
    }
  };
  for (auto& song : content_songs_) {
    patch(song);
  }
  for (auto& song : home_starred_songs_) {
    patch(song);
  }
  for (auto& song : home_random_songs_) {
    patch(song);
  }
  queue_.update_annotations(song_id, annotations);
  if (has_selected_song_ && selected_song_.id == song_id) {
    selected_song_.annotations = annotations;
    update_song_details(&selected_song_);
  }
  refresh_content_song_rows(song_id);

  const auto* playing = queue_.current();
  if (playing && playing->id == song_id) {
    // Keep favorites list coherent with server starring for the playing track.
    if (annotations.starred) {
      bool found = false;
      for (const auto& song : home_starred_songs_) {
        if (song.id == song_id) {
          found = true;
          break;
        }
      }
      if (!found) {
        home_starred_songs_.insert(home_starred_songs_.begin(), *playing);
      }
    } else {
      home_starred_songs_.erase(
          std::remove_if(home_starred_songs_.begin(), home_starred_songs_.end(),
                         [&](const subsonic::Song& s) { return s.id == song_id; }),
          home_starred_songs_.end());
    }
  }
  sync_playing_annotation_controls();
}

void MainWindow::refresh_content_song_rows(const std::string& song_id) {
  if (!content_is_songs_ || !GTK_IS_LIST_BOX(content_list_) || song_id.empty()) {
    return;
  }
  for (std::size_t i = 0; i < content_songs_.size(); ++i) {
    const auto& song = content_songs_[i];
    if (song.id != song_id) {
      continue;
    }
    auto* row =
        gtk_list_box_get_row_at_index(content_list_, static_cast<int>(i));
    if (!row) {
      continue;
    }
    auto* box = gtk_list_box_row_get_child(row);
    if (!box) {
      continue;
    }
    auto* title_w = gtk_widget_get_first_child(box);
    auto* subtitle_w = title_w ? gtk_widget_get_next_sibling(title_w) : nullptr;
    if (!GTK_IS_LABEL(title_w) || !GTK_IS_LABEL(subtitle_w)) {
      continue;
    }
    gtk_label_set_text(GTK_LABEL(title_w), song_list_title(song).c_str());
    gtk_label_set_text(GTK_LABEL(subtitle_w), song_list_subtitle(song).c_str());
  }
}

void MainWindow::on_toggle_favorite() {
  const auto* playing = queue_.current();
  if (!playing) {
    return;
  }
  const bool starred = gtk_toggle_button_get_active(favorite_button_);
  const std::string id = playing->id;
  const auto base = playing->annotations;
  client_.set_starred(id, starred, [this, id, starred, base](
                                       std::optional<subsonic::ClientError> err) {
    if (!ui_alive_) {
      return;
    }
    if (err) {
      set_status("Favorite update failed: " +
                     radio::util::friendly_network_message(err->message),
                 5);
      sync_playing_annotation_controls();
      return;
    }
    auto annotations = base;
    annotations.starred = starred;
    if (starred && annotations.starred_at.empty()) {
      annotations.starred_at = "now";
    }
    if (!starred) {
      annotations.starred_at.clear();
    }
    apply_song_annotation_locally(id, annotations);
    set_status(starred ? "Saved favorite on server" : "Removed favorite on server");
  });
}

void MainWindow::on_set_rating(int rating) {
  const auto* playing = queue_.current();
  if (!playing) {
    return;
  }
  rating = std::clamp(rating, 0, 5);
  const std::string id = playing->id;
  const auto base = playing->annotations;
  client_.set_rating(id, rating, [this, id, rating, base](
                                     std::optional<subsonic::ClientError> err) {
    if (!ui_alive_) {
      return;
    }
    if (err) {
      set_status("Rating update failed: " +
                     radio::util::friendly_network_message(err->message),
                 5);
      sync_playing_annotation_controls();
      return;
    }
    auto annotations = base;
    annotations.user_rating = rating;
    apply_song_annotation_locally(id, annotations);
    set_status(rating == 0 ? "Cleared rating on server"
                           : ("Saved rating " + std::to_string(rating) +
                              "/5 on server"));
  });
}

void MainWindow::on_prev() {
  maybe_submit_scrobble(false);
  if (queue_.previous()) {
    play_current();
  }
}

void MainWindow::on_play_pause() {
  if (player_.state() == audio::State::Stopped) {
    if (queue_.current()) {
      play_current();
    } else if (content_is_songs_ && !content_songs_.empty()) {
      play_songs(content_songs_, 0);
    }
    return;
  }
  player_.toggle_pause();
  save_playback_session();
}

void MainWindow::on_next() {
  maybe_submit_scrobble(false);
  if (queue_.next()) {
    play_current();
  } else {
    player_.stop();
    current_song_id_.clear();
    resume_position_ns_ = -1;
    if (GTK_IS_LABEL(now_title_)) {
      gtk_label_set_text(now_title_, "Nothing playing");
    }
    if (GTK_IS_LABEL(now_subtitle_)) {
      gtk_label_set_text(now_subtitle_, "");
    }
    sync_playing_annotation_controls();
    SessionStore::clear();
  }
}

void MainWindow::on_shuffle() {
  if (updating_annotation_ui_) {
    return;
  }
  queue_.set_shuffle(gtk_toggle_button_get_active(shuffle_button_));
  save_playback_session();
}

void MainWindow::on_repeat() {
  queue_.cycle_repeat();
  sync_transport_buttons();
  save_playback_session();
}

void MainWindow::apply_eq_settings(const audio::EqSettings& settings) {
  config_.eq = settings;
  player_.set_eq(settings);
  ConfigStore::save(config_);
  if (GTK_IS_BUTTON(eq_button_)) {
    if (settings.enabled) {
      gtk_widget_add_css_class(GTK_WIDGET(eq_button_), "suggested-action");
    } else {
      gtk_widget_remove_css_class(GTK_WIDGET(eq_button_), "suggested-action");
    }
  }
}

void MainWindow::on_open_eq() {
  if (!ui_alive_ || !window_) {
    return;
  }
  auto* dialog = new EqDialog(
      GTK_WINDOW(window_), config_.eq,
      [this](const audio::EqSettings& settings) { apply_eq_settings(settings); });
  dialog->present();
}

void MainWindow::on_toggle_minimal_player() {
  if (updating_minimal_ui_ || !GTK_IS_TOGGLE_BUTTON(minimal_button_)) {
    return;
  }
  set_minimal_player(gtk_toggle_button_get_active(minimal_button_));
  ConfigStore::save(config_);
}

void MainWindow::ensure_beat_stage_timer() {
  if (beat_stage_timer_id_ || !ui_alive_) {
    return;
  }
  beat_stage_timer_id_ = g_timeout_add(
      16, +[](gpointer self) -> gboolean {
        return static_cast<MainWindow*>(self)->on_beat_stage_tick()
                   ? G_SOURCE_CONTINUE
                   : G_SOURCE_REMOVE;
      },
      this);
}

void MainWindow::stop_beat_stage_timer() {
  if (!beat_stage_timer_id_) {
    return;
  }
  g_source_remove(beat_stage_timer_id_);
  beat_stage_timer_id_ = 0;
}

void MainWindow::sync_beat_stage_active() {
  if (!beat_stage_) {
    return;
  }
  const bool should_run =
      ui_alive_ && !minimal_player_ && current_nav_ == NavPage::Home &&
      player_.state() == audio::State::Playing;
  beat_stage_->set_active(should_run);
  if (should_run) {
    ensure_beat_stage_timer();
  }
}

bool MainWindow::on_beat_stage_tick() {
  if (!ui_alive_ || !beat_stage_) {
    beat_stage_timer_id_ = 0;
    return false;
  }

  const bool should_run = !minimal_player_ && current_nav_ == NavPage::Home &&
                          player_.state() == audio::State::Playing;
  if (should_run) {
    audio::BeatLevels levels;
    if (player_.pull_beat_levels(levels)) {
      beat_stage_->on_levels(levels.bass, levels.overall);
    }
  } else {
    beat_stage_->set_active(false);
  }

  const bool keep_going = beat_stage_->tick();
  if (!keep_going) {
    beat_stage_timer_id_ = 0;
  }
  return keep_going;
}

void MainWindow::set_minimal_player(bool minimal) {
  if (!ui_alive_) {
    return;
  }
  const bool changed = minimal_player_ != minimal;
  minimal_player_ = minimal;
  config_.minimal_player = minimal;

  updating_minimal_ui_ = true;
  if (GTK_IS_TOGGLE_BUTTON(minimal_button_)) {
    if (gtk_toggle_button_get_active(minimal_button_) != minimal) {
      gtk_toggle_button_set_active(minimal_button_, minimal);
    }
    gtk_button_set_icon_name(
        GTK_BUTTON(minimal_button_),
        minimal ? "view-fullscreen-symbolic" : "view-restore-symbolic");
    gtk_widget_set_tooltip_text(
        GTK_WIDGET(minimal_button_),
        minimal ? "Expand to full player" : "Minimal player view");
    if (minimal) {
      gtk_widget_add_css_class(GTK_WIDGET(minimal_button_), "suggested-action");
    } else {
      gtk_widget_remove_css_class(GTK_WIDGET(minimal_button_),
                                  "suggested-action");
    }
  }
  updating_minimal_ui_ = false;

  if (browse_paned_) {
    gtk_widget_set_visible(browse_paned_, !minimal);
    // Browse owns leftover vertical space; player bar stays natural height.
    gtk_widget_set_vexpand(browse_paned_, TRUE);
  }
  sync_beat_stage_active();
  if (toolbar_view_) {
    // Keep the title bar in mini mode so the window stays draggable.
    adw_toolbar_view_set_reveal_top_bars(toolbar_view_, TRUE);
    adw_toolbar_view_set_extend_content_to_top_edge(toolbar_view_, FALSE);
    adw_toolbar_view_set_extend_content_to_bottom_edge(toolbar_view_, FALSE);
  }
  if (rating_box_) {
    gtk_widget_set_visible(rating_box_, !minimal);
  }
  if (GTK_IS_BUTTON(clear_rating_button_)) {
    gtk_widget_set_visible(GTK_WIDGET(clear_rating_button_), !minimal);
  }
  if (volume_box_) {
    gtk_widget_set_margin_start(volume_box_, minimal ? 8 : 24);
  }
  if (GTK_IS_SCALE(volume_scale_)) {
    gtk_widget_set_size_request(GTK_WIDGET(volume_scale_),
                                minimal ? 72 : 120, -1);
  }
  if (GTK_IS_IMAGE(cover_image_)) {
    gtk_image_set_pixel_size(cover_image_, minimal ? 44 : 56);
  }
  if (player_bar_) {
    // Even inset on every side under the title bar.
    constexpr int kMiniPad = 12;
    gtk_widget_set_margin_top(player_bar_, minimal ? kMiniPad : 8);
    gtk_widget_set_margin_bottom(player_bar_, minimal ? kMiniPad : 10);
    gtk_widget_set_margin_start(player_bar_, minimal ? kMiniPad : 12);
    gtk_widget_set_margin_end(player_bar_, minimal ? kMiniPad : 12);
    // Never let the player bar eat leftover height — that left a blank grey
    // strip under the controls after leaving minimal mode.
    gtk_widget_set_vexpand(player_bar_, FALSE);
    gtk_widget_set_valign(player_bar_, GTK_ALIGN_END);
  }

  if (!window_) {
    return;
  }

  if (minimal) {
    if (changed && gtk_widget_get_mapped(GTK_WIDGET(window_))) {
      const int width = gtk_widget_get_width(GTK_WIDGET(window_));
      const int height = gtk_widget_get_height(GTK_WIDGET(window_));
      if (width > 1 && height > 1) {
        normal_window_width_ = width;
        normal_window_height_ = height;
      }
    }
    constexpr int kMiniPad = 12;
    constexpr int kMiniWidth = 520;
    constexpr int kHeaderAllowance = 48;
    int content_h = 0;
    if (player_bar_) {
      gtk_widget_measure(player_bar_, GTK_ORIENTATION_VERTICAL, kMiniWidth,
                         &content_h, nullptr, nullptr, nullptr);
    }
    // Title bar + player content + matching top/bottom pad.
    const int mini_height =
        std::max(180, kHeaderAllowance + content_h + (kMiniPad * 2));
    gtk_window_set_default_size(GTK_WINDOW(window_), kMiniWidth, mini_height);
  } else if (changed) {
    gtk_window_set_default_size(GTK_WINDOW(window_), normal_window_width_,
                                normal_window_height_);
  }
}

void MainWindow::set_status(const std::string& text,
                            unsigned clear_after_seconds) {
  if (status_clear_id_) {
    g_source_remove(status_clear_id_);
    status_clear_id_ = 0;
  }
  if (!ui_alive_ || !GTK_IS_LABEL(status_label_)) {
    return;
  }
  gtk_label_set_text(status_label_, text.c_str());
  if (text.empty() || clear_after_seconds == 0) {
    return;
  }
  status_clear_id_ = g_timeout_add_seconds(
      clear_after_seconds,
      +[](gpointer self) -> gboolean {
        auto* w = static_cast<MainWindow*>(self);
        w->status_clear_id_ = 0;
        if (w->ui_alive_ && GTK_IS_LABEL(w->status_label_)) {
          gtk_label_set_text(w->status_label_, "");
        }
        return G_SOURCE_REMOVE;
      },
      this);
}

void MainWindow::set_now_playing(const subsonic::Song& song) {
  if (GTK_IS_LABEL(now_title_)) {
    gtk_label_set_text(now_title_, song.title.c_str());
  }
  if (GTK_IS_LABEL(now_subtitle_)) {
    gtk_label_set_text(now_subtitle_,
                       (song.artist + " — " + song.album).c_str());
  }
  load_cover(song.cover_art);
}

void MainWindow::load_cover(const std::string& cover_id) {
  if (!GTK_IS_IMAGE(cover_image_)) {
    return;
  }
  if (cover_id.empty()) {
    gtk_image_set_from_icon_name(cover_image_, "folder-music-symbolic");
    return;
  }
  if (!CoverLoader::apply_cached(cover_image_, cover_id, 120, 56)) {
    gtk_image_set_from_icon_name(cover_image_, "folder-music-symbolic");
    CoverLoader::load_async(cover_image_, client_, cover_id, 120, 56);
  }
}

std::string MainWindow::format_time(int seconds) {
  if (seconds < 0) {
    seconds = 0;
  }
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%d:%02d", seconds / 60, seconds % 60);
  return buf;
}

std::string MainWindow::format_ns(std::int64_t ns) {
  const int seconds = static_cast<int>(ns / kNsPerSec);
  return format_time(seconds);
}

std::string MainWindow::format_rating_stars(int rating) {
  rating = std::clamp(rating, 0, 5);
  std::string out;
  out.reserve(15);
  for (int i = 1; i <= 5; ++i) {
    out += (i <= rating) ? "★" : "☆";
  }
  return out;
}

std::string MainWindow::format_annotations(const subsonic::Annotations& a) {
  std::string out;
  if (a.starred) {
    out += "Favorite";
  }
  if (a.user_rating > 0) {
    if (!out.empty()) {
      out += " · ";
    }
    out += format_rating_stars(a.user_rating);
  } else if (a.average_rating > 0.0) {
    if (!out.empty()) {
      out += " · ";
    }
    out += "avg " + format_rating_stars(
                        static_cast<int>(std::lround(a.average_rating)));
  }
  return out;
}

std::string MainWindow::annotate_title(const std::string& title,
                                       const subsonic::Annotations& a) {
  if (a.starred) {
    return std::string("♥ ") + title;
  }
  return title;
}

std::string MainWindow::song_list_title(const subsonic::Song& song) {
  std::string title = song.title;
  if (song.track > 0) {
    title = std::to_string(song.track) + ". " + title;
  }
  return annotate_title(title, song.annotations);
}

std::string MainWindow::song_list_subtitle(const subsonic::Song& song) {
  std::string sub = format_time(song.duration);
  const auto badge = format_annotations(song.annotations);
  if (!badge.empty()) {
    sub += " · " + badge;
  }
  return sub;
}

}  // namespace radio::ui
