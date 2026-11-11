#pragma once

#include "subsonic/models.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace radio::subsonic {

struct LibrarySnapshot {
  // Full artist list for the Artists browse pane
  std::vector<Artist> artists;

  // Home dashboard sections
  std::vector<Album> home_recent;
  std::vector<Album> home_frequent;
  std::vector<Album> home_newest;
  std::vector<Album> home_highest;
  Starred home_starred;
  std::vector<Song> home_random_songs;

  std::string error;
  bool ok = false;
};

// Periodically pulls library + Home content on a dedicated worker thread using
// its own SoupSession, so UI/playback networking is never aborted or blocked.
// Results are delivered on the GLib main thread via the update callback.
class LibraryPoller {
 public:
  using UpdateCb = std::function<void(LibrarySnapshot)>;

  LibraryPoller();
  ~LibraryPoller();

  LibraryPoller(const LibraryPoller&) = delete;
  LibraryPoller& operator=(const LibraryPoller&) = delete;

  void set_credentials(std::string base_url, std::string username,
                       std::string password);
  void clear_credentials();
  void set_interval(std::chrono::seconds interval);
  void set_update_callback(UpdateCb cb);

  void start();
  void stop();
  // Wake the worker to sync soon (still off the UI thread).
  void request_sync();

 private:
  struct Credentials {
    std::string base_url;
    std::string username;
    std::string password;
  };

  void thread_main();
  LibrarySnapshot fetch_snapshot(const Credentials& creds) const;

  mutable std::mutex mu_;
  std::condition_variable cv_;
  Credentials creds_;
  UpdateCb update_cb_;
  std::chrono::seconds interval_{180};
  std::chrono::seconds initial_delay_{45};
  bool stop_ = true;
  bool sync_requested_ = false;
  std::thread worker_;
  std::atomic<bool> syncing_{false};
};

// Pure helpers shared with tests.
std::string build_token_api_url(
    const std::string& base_url, const std::string& username,
    const std::string& password, const std::string& endpoint,
    const std::vector<std::pair<std::string, std::string>>& extra = {});

bool parse_artists_response(const std::string& body, std::vector<Artist>& out,
                            std::string& error_out);
bool parse_album_list_response(const std::string& body,
                               std::vector<Album>& out, std::string& error_out);
bool parse_starred_response(const std::string& body, Starred& out,
                            std::string& error_out);
bool parse_random_songs_response(const std::string& body,
                                 std::vector<Song>& out, std::string& error_out);

bool library_artists_equivalent(const std::vector<Artist>& a,
                                const std::vector<Artist>& b);
bool library_albums_equivalent(const std::vector<Album>& a,
                               const std::vector<Album>& b);
bool library_songs_equivalent(const std::vector<Song>& a,
                              const std::vector<Song>& b);

}  // namespace radio::subsonic
