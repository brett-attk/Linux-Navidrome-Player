#pragma once

#include "audio/player.hpp"
#include "config.hpp"
#include "queue/play_queue.hpp"
#include "session.hpp"
#include "subsonic/client.hpp"
#include "subsonic/models.hpp"
#include "ui/beat_stage.hpp"

#include <memory>

#include <adwaita.h>
#include <gtk/gtk.h>

#include <string>
#include <vector>

namespace radio::ui {

// Top-level window: browse UI + transport + Subsonic client + GStreamer player.
//
// Important lifetime rule: many lambdas capture `this` and run later (HTTP,
// timers). Before touching widgets they must check `ui_alive_`, which becomes
// false in on_ui_destroyed().
class MainWindow {
 public:
  // `explicit` prevents accidental MainWindow window = app; conversions.
  explicit MainWindow(AdwApplication* app);
  ~MainWindow();
  void present();

 private:
  enum class NavPage { Home, Artists, Albums };

  void build_ui();
  void on_ui_destroyed();
  void show_connect_dialog(bool force);
  void connect_with(const AppConfig& cfg);
  void set_nav(NavPage page);
  void load_home();
  void load_artists();
  void load_albums();
  void show_artist_albums(const subsonic::Artist& artist);
  void show_album_songs(const subsonic::Album& album);
  void update_song_details(const subsonic::Song* song);
  void sync_annotation_controls(const subsonic::Annotations& annotations,
                                bool enabled);
  void sync_playing_annotation_controls();
  void play_songs(std::vector<subsonic::Song> songs, std::size_t start_index);
  void play_current();
  void maybe_submit_scrobble(bool force = false);
  void save_playback_session();
  void restore_playback_session();
  void update_progress_labels(std::int64_t position_ns, std::int64_t duration_ns);
  void sync_transport_buttons();
  void on_prev();
  void on_play_pause();
  void on_next();
  void on_shuffle();
  void on_repeat();
  void on_toggle_favorite();
  void on_set_rating(int rating);
  void on_open_eq();
  void on_toggle_minimal_player();
  void set_minimal_player(bool minimal);
  void ensure_beat_stage_timer();
  void stop_beat_stage_timer();
  bool on_beat_stage_tick();
  void sync_beat_stage_active();
  void apply_eq_settings(const audio::EqSettings& settings);
  void apply_song_annotation_locally(const std::string& song_id,
                                     const subsonic::Annotations& annotations);
  void refresh_content_song_rows(const std::string& song_id);
  void set_status(const std::string& text, unsigned clear_after_seconds = 3);
  void set_now_playing(const subsonic::Song& song);
  void load_cover(const std::string& cover_id);
  void load_tile_cover(GtkImage* image, const std::string& cover_id);
  GtkWidget* make_home_section(const char* title, GtkWidget* row);
  GtkWidget* make_album_tile(const subsonic::Album& album);
  GtkWidget* make_artist_tile(const subsonic::Artist& artist);
  GtkWidget* make_song_tile(const subsonic::Song& song,
                            const std::vector<subsonic::Song>& context,
                            std::size_t index);
  void clear_container(GtkWidget* box);
  static std::string format_time(int seconds);
  static std::string format_ns(std::int64_t ns);
  static std::string format_rating_stars(int rating);
  static std::string format_annotations(const subsonic::Annotations& a);
  static std::string annotate_title(const std::string& title,
                                    const subsonic::Annotations& a);
  static std::string song_list_title(const subsonic::Song& song);
  static std::string song_list_subtitle(const subsonic::Song& song);
  static std::vector<subsonic::Artist> artists_from_frequent_albums(
      const std::vector<subsonic::Album>& albums, std::size_t limit);

  AdwApplication* app_ = nullptr;
  AdwApplicationWindow* window_ = nullptr;
  AdwToolbarView* toolbar_view_ = nullptr;
  GtkWidget* browse_paned_ = nullptr;
  GtkWidget* player_bar_ = nullptr;

  GtkStack* browse_stack_ = nullptr;
  GtkStack* main_stack_ = nullptr;
  GtkListBox* artist_list_ = nullptr;
  GtkListBox* album_list_ = nullptr;
  GtkListBox* content_list_ = nullptr;
  GtkLabel* content_title_ = nullptr;
  GtkLabel* song_detail_label_ = nullptr;
  GtkLabel* status_label_ = nullptr;
  GtkBox* home_sections_ = nullptr;
  GtkToggleButton* home_btn_ = nullptr;
  GtkToggleButton* artists_btn_ = nullptr;
  GtkToggleButton* albums_btn_ = nullptr;

  GtkLabel* now_title_ = nullptr;
  GtkLabel* now_subtitle_ = nullptr;
  GtkImage* cover_image_ = nullptr;
  GtkLabel* time_elapsed_ = nullptr;
  GtkLabel* time_total_ = nullptr;
  GtkScale* seek_scale_ = nullptr;
  GtkScale* volume_scale_ = nullptr;
  GtkButton* play_pause_button_ = nullptr;
  GtkToggleButton* shuffle_button_ = nullptr;
  GtkButton* repeat_button_ = nullptr;
  GtkToggleButton* favorite_button_ = nullptr;
  GtkWidget* rating_box_ = nullptr;
  GtkButton* clear_rating_button_ = nullptr;
  GtkWidget* volume_box_ = nullptr;
  GtkButton* eq_button_ = nullptr;
  GtkToggleButton* minimal_button_ = nullptr;
  std::vector<GtkToggleButton*> rating_buttons_;

  subsonic::Client client_;
  audio::Player player_;
  queue::PlayQueue queue_;
  AppConfig config_;

  std::vector<subsonic::Artist> artists_;
  std::vector<subsonic::Album> albums_;
  std::vector<subsonic::Album> content_albums_;
  std::vector<subsonic::Song> content_songs_;
  std::vector<subsonic::Album> home_recent_;
  std::vector<subsonic::Album> home_frequent_;
  std::vector<subsonic::Album> home_newest_;
  std::vector<subsonic::Album> home_highest_;
  std::vector<subsonic::Album> home_starred_albums_;
  std::vector<subsonic::Artist> home_top_artists_;
  std::vector<subsonic::Artist> home_starred_artists_;
  std::vector<subsonic::Song> home_random_songs_;
  std::vector<subsonic::Song> home_starred_songs_;
  subsonic::Song selected_song_;
  bool has_selected_song_ = false;
  bool content_is_songs_ = false;
  bool seeking_ = false;              // user is dragging the seek bar
  bool scrobbled_current_ = false;    // already sent submission=true for this track
  bool updating_annotation_ui_ = false;  // ignore toggle signals we caused ourselves
  bool updating_minimal_ui_ = false;     // ignore toggle signals we caused ourselves
  bool minimal_player_ = false;
  NavPage current_nav_ = NavPage::Home;
  int normal_window_width_ = 1100;
  int normal_window_height_ = 720;
  unsigned beat_stage_timer_id_ = 0;
  std::unique_ptr<BeatStage> beat_stage_;
  std::string current_song_id_;
  // Home loads several endpoints in parallel. `home_loads_pending_` counts
  // outstanding replies; `home_generation_` invalidates stale replies if the
  // user refreshes before older requests finish.
  int home_loads_pending_ = 0;
  int home_generation_ = 0;
  unsigned status_clear_id_ = 0;   // g_timeout source id (0 = none)
  unsigned session_save_id_ = 0;
  bool ui_alive_ = false;
  std::int64_t resume_position_ns_ = -1;  // restored position; consumed on play
};

}  // namespace radio::ui
