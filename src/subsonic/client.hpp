#pragma once

#include "subsonic/models.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace radio::subsonic {

// Async Subsonic/Navidrome HTTP client (libsoup).
//
// Call style: methods return immediately and later invoke a callback on the
// GLib main thread with either data or an error. Example:
//   client.ping([](std::optional<ClientError> err) { ... });
//
// std::optional<T> means "maybe a T": has a value, or is empty (nullopt).
// std::function<...> stores any callable (lambda, function) with that signature.

class Client {
 public:
  // Callback type aliases keep method signatures readable.
  using DoneCb = std::function<void(std::optional<ClientError>)>;
  using ArtistsCb =
      std::function<void(std::vector<Artist>, std::optional<ClientError>)>;
  using AlbumsCb =
      std::function<void(std::vector<Album>, std::optional<ClientError>)>;
  using SongsCb =
      std::function<void(std::vector<Song>, std::optional<ClientError>)>;
  using StarredCb =
      std::function<void(Starred, std::optional<ClientError>)>;
  using BytesCb =
      std::function<void(std::vector<unsigned char>, std::optional<ClientError>)>;

  Client();
  ~Client();

  // Not copyable: owns a SoupSession and in-flight async requests.
  Client(const Client&) = delete;
  Client& operator=(const Client&) = delete;

  void set_credentials(std::string base_url, std::string username,
                       std::string password);
  void abort_pending();

  const std::string& base_url() const { return base_url_; }
  const std::string& username() const { return username_; }
  bool configured() const {
    return !base_url_.empty() && !username_.empty();
  }

  void ping(DoneCb cb);
  void get_artists(ArtistsCb cb);
  void get_artist_albums(const std::string& artist_id, AlbumsCb cb);
  void get_album_list(const std::string& type, int size, int offset,
                      AlbumsCb cb);
  void get_album_songs(const std::string& album_id, SongsCb cb);
  void get_starred(StarredCb cb);
  void get_random_songs(int size, SongsCb cb);
  void get_cover_art(const std::string& cover_id, int size, BytesCb cb);
  void scrobble(const std::string& song_id, bool submission);
  void set_starred(const std::string& id, bool starred, DoneCb cb = {});
  void set_rating(const std::string& id, int rating, DoneCb cb = {});

  std::string stream_url(const std::string& song_id) const;
  std::string cover_url(const std::string& cover_id, int size = 300) const;

 private:
  struct AuthParams {
    std::string u;
    std::string t;
    std::string s;
  };

  AuthParams make_auth() const;
  std::string api_url(const std::string& endpoint,
                      const std::vector<std::pair<std::string, std::string>>&
                          extra = {},
                      bool json = true) const;

  using JsonHandler =
      std::function<void(const std::string& body, std::optional<ClientError>)>;
  void request_text(const std::string& url, JsonHandler handler);
  void request_bytes(const std::string& url, BytesCb handler);
  bool ensure_ready(DoneCb* done_cb = nullptr);

  std::string base_url_;
  std::string username_;
  std::string password_;
  void* session_ = nullptr;  // Opaque SoupSession* (avoid pulling soup into header)

  // Shared "still alive?" flag copied into every async callback.
  // When ~Client runs we set it false so late HTTP completions do nothing.
  // shared_ptr keeps the flag alive even if Client is already destroyed while
  // a soup callback is still queued; atomic makes the bool safe across threads.
  std::shared_ptr<std::atomic<bool>> alive_ =
      std::make_shared<std::atomic<bool>>(true);
};

}  // namespace radio::subsonic
