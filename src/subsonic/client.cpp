#include "subsonic/client.hpp"

#include "subsonic/parse.hpp"
#include "util/log.hpp"
#include "util/safe.hpp"

#include <glib.h>
#include <libsoup/soup.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <memory>
#include <optional>
#include <random>
#include <sstream>
#include <utility>

namespace radio::subsonic {
namespace {

using json = nlohmann::json;

// GLib helpers return heap strings (gchar*). Copy into std::string, then g_free.
std::string md5_hex(const std::string& input) {
  gchar* digest =
      g_compute_checksum_for_string(G_CHECKSUM_MD5, input.c_str(), input.size());
  std::string result = digest ? digest : "";
  g_free(digest);
  return result;
}

std::string url_encode(const std::string& value) {
  gchar* encoded = g_uri_escape_string(value.c_str(), nullptr, FALSE);
  std::string result = encoded ? encoded : value;
  g_free(encoded);
  return result;
}

std::string random_salt(std::size_t length = 12) {
  static constexpr char kAlphabet[] =
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  // thread_local: each thread gets its own RNG (safe if called from workers).
  thread_local std::mt19937 rng{std::random_device{}()};
  std::uniform_int_distribution<std::size_t> dist(0, sizeof(kAlphabet) - 2);
  std::string salt;
  salt.reserve(length);
  for (std::size_t i = 0; i < length; ++i) {
    salt.push_back(kAlphabet[dist(rng)]);
  }
  return salt;
}

std::string trim_trailing_slash(std::string value) {
  while (!value.empty() && value.back() == '/') {
    value.pop_back();
  }
  return value;
}

std::string soup_error_message(GError* error) {
  if (error != nullptr && error->message != nullptr) {
    return error->message;
  }
  return "Network request failed";
}

}  // namespace

Client::Client() {
  // In libsoup 3, max-conns* can only be set at construction time.
  session_ = soup_session_new_with_options("max-conns", 6, "max-conns-per-host",
                                           4, nullptr);
  auto* session = static_cast<SoupSession*>(session_);
  soup_session_set_timeout(session, 20);       // socket I/O seconds
  soup_session_set_idle_timeout(session, 30);  // keep-alive idle seconds
}

Client::~Client() {
  // Mark dead first so any callback that still fires after abort is a no-op.
  alive_->store(false);
  abort_pending();
  if (session_ != nullptr) {
    g_object_unref(session_);  // drop our reference; soup frees when count hits 0
    session_ = nullptr;
  }
}

void Client::abort_pending() {
  if (session_ != nullptr) {
    soup_session_abort(static_cast<SoupSession*>(session_));
  }
}

void Client::set_credentials(std::string base_url, std::string username,
                             std::string password) {
  // Cancel in-flight calls that still use the previous server/user.
  abort_pending();
  // std::move: transfer string buffers instead of copying (args are by-value).
  base_url_ = trim_trailing_slash(std::move(base_url));
  username_ = std::move(username);
  password_ = std::move(password);
}

bool Client::ensure_ready(DoneCb* done_cb) {
  if (!configured() || session_ == nullptr) {
    if (done_cb != nullptr && *done_cb) {
      util::invoke_nothrow(
          *done_cb,
          std::optional<ClientError>{ClientError{"Not connected to a server"}});
    }
    return false;
  }
  return true;
}

Client::AuthParams Client::make_auth() const {
  // Subsonic token auth: never send the raw password.
  //   s = random salt
  //   t = MD5(password + salt)
  AuthParams auth;
  auth.u = username_;
  auth.s = random_salt();
  auth.t = md5_hex(password_ + auth.s);
  return auth;
}

std::string Client::api_url(
    const std::string& endpoint,
    const std::vector<std::pair<std::string, std::string>>& extra,
    bool json_fmt) const {
  if (base_url_.empty()) {
    return {};
  }
  const auto auth = make_auth();
  std::ostringstream oss;
  oss << base_url_ << "/rest/" << endpoint << "?u=" << url_encode(auth.u)
      << "&t=" << auth.t << "&s=" << auth.s << "&v=1.16.1&c=radio-player";
  if (json_fmt) {
    oss << "&f=json";
  }
  for (const auto& [key, value] : extra) {
    oss << '&' << key << '=' << url_encode(value);
  }
  return oss.str();
}

void Client::request_text(const std::string& url, JsonHandler handler) {
  if (url.empty() || session_ == nullptr) {
    util::invoke_nothrow(
        handler, std::string{},
        std::optional<ClientError>{ClientError{"Invalid request URL"}});
    return;
  }

  SoupMessage* msg = soup_message_new("GET", url.c_str());
  if (msg == nullptr) {
    util::invoke_nothrow(
        handler, std::string{},
        std::optional<ClientError>{ClientError{"Could not create request"}});
    return;
  }

  // Soup's C API only accepts a void* user_data for the completion callback.
  // Heap-allocate anything the lambda needs (handler + alive flag).
  struct Payload {
    JsonHandler handler;
    std::shared_ptr<std::atomic<bool>> alive;
  };
  auto* payload = new Payload{std::move(handler), alive_};

  // +[] creates a plain C function pointer from a non-capturing lambda.
  // Capturing lambdas cannot convert to function pointers, so state goes in
  // Payload instead of the lambda capture list.
  soup_session_send_and_read_async(
      static_cast<SoupSession*>(session_), msg, G_PRIORITY_DEFAULT, nullptr,
      +[](GObject* source, GAsyncResult* result, gpointer user_data) {
        // unique_ptr deletes Payload when this scope exits (success or return).
        std::unique_ptr<Payload> payload(static_cast<Payload*>(user_data));
        if (!payload->alive || !payload->alive->load()) {
          return;
        }

        GError* error = nullptr;
        GBytes* bytes = soup_session_send_and_read_finish(SOUP_SESSION(source),
                                                          result, &error);
        std::string body;
        std::optional<ClientError> err;
        if (error != nullptr) {
          err = ClientError{soup_error_message(error), error->code};
          g_error_free(error);
        } else if (bytes != nullptr) {
          gsize size = 0;
          const auto* data =
              static_cast<const char*>(g_bytes_get_data(bytes, &size));
          if (data != nullptr && size > 0) {
            body.assign(data, size);
          }
          g_bytes_unref(bytes);
        }
        if (!err && body.empty()) {
          err = ClientError{"Empty response from server"};
        }
        if (err) {
          util::log_error("HTTP: " + err->message);
        }
        util::invoke_nothrow(payload->handler, body, err);
      },
      payload);
  // Session keeps its own ref on the message for the async op; drop ours.
  g_object_unref(msg);
}

void Client::request_bytes(const std::string& url, BytesCb handler) {
  if (url.empty() || session_ == nullptr) {
    util::invoke_nothrow(
        handler, std::vector<unsigned char>{},
        std::optional<ClientError>{ClientError{"Invalid request URL"}});
    return;
  }

  SoupMessage* msg = soup_message_new("GET", url.c_str());
  if (msg == nullptr) {
    util::invoke_nothrow(
        handler, std::vector<unsigned char>{},
        std::optional<ClientError>{ClientError{"Could not create request"}});
    return;
  }

  // Same async pattern as request_text, but returns raw bytes (cover art).
  struct Payload {
    BytesCb handler;
    std::shared_ptr<std::atomic<bool>> alive;
  };
  auto* payload = new Payload{std::move(handler), alive_};

  soup_session_send_and_read_async(
      static_cast<SoupSession*>(session_), msg, G_PRIORITY_DEFAULT, nullptr,
      +[](GObject* source, GAsyncResult* result, gpointer user_data) {
        std::unique_ptr<Payload> payload(static_cast<Payload*>(user_data));
        if (!payload->alive || !payload->alive->load()) {
          return;
        }

        GError* error = nullptr;
        GBytes* bytes = soup_session_send_and_read_finish(SOUP_SESSION(source),
                                                          result, &error);
        std::vector<unsigned char> out;
        std::optional<ClientError> err;
        if (error != nullptr) {
          err = ClientError{soup_error_message(error), error->code};
          g_error_free(error);
        } else if (bytes != nullptr) {
          gsize size = 0;
          const auto* data = static_cast<const unsigned char*>(
              g_bytes_get_data(bytes, &size));
          if (data != nullptr && size > 0) {
            out.assign(data, data + size);
          }
          g_bytes_unref(bytes);
        }
        if (!err && out.empty()) {
          err = ClientError{"Empty cover response"};
        }
        if (err) {
          util::log_warn("Cover HTTP: " + err->message);
        }
        // std::move(out) avoids copying the image buffer into the callback.
        util::invoke_nothrow(payload->handler, std::move(out), err);
      },
      payload);
  g_object_unref(msg);
}

void Client::ping(DoneCb cb) {
  if (!ensure_ready(&cb)) {
    return;
  }
  request_text(api_url("ping"),
               [cb = std::move(cb)](const std::string& body,
                                    std::optional<ClientError> transport) mutable {
                 if (transport) {
                   util::invoke_nothrow(cb, transport);
                   return;
                 }
                 try {
                   util::invoke_nothrow(cb, parse_api_error(json::parse(body)));
                 } catch (const std::exception& ex) {
                   util::invoke_nothrow(
                       cb, std::optional<ClientError>{ClientError{ex.what()}});
                 } catch (...) {
                   util::invoke_nothrow(
                       cb, std::optional<ClientError>{ClientError{"Invalid JSON"}});
                 }
               });
}

void Client::get_artists(ArtistsCb cb) {
  if (!configured()) {
    util::invoke_nothrow(
        cb, std::vector<Artist>{},
        std::optional<ClientError>{ClientError{"Not connected to a server"}});
    return;
  }
  request_text(
      api_url("getArtists"),
      [cb = std::move(cb)](const std::string& body,
                           std::optional<ClientError> transport) mutable {
        if (transport) {
          util::invoke_nothrow(cb, std::vector<Artist>{}, transport);
          return;
        }
        try {
          const auto root = json::parse(body);
          if (auto err = parse_api_error(root)) {
            util::invoke_nothrow(cb, std::vector<Artist>{}, err);
            return;
          }
          std::vector<Artist> artists;
          const auto& index = root["subsonic-response"]["artists"]["index"];
          if (index.is_array()) {
            for (const auto& letter : index) {
              if (!letter.contains("artist")) {
                continue;
              }
              auto batch =
                  parse_array_or_object<Artist>(letter["artist"], parse_artist);
              artists.insert(artists.end(), batch.begin(), batch.end());
            }
          }
          util::invoke_nothrow(cb, std::move(artists), std::nullopt);
        } catch (const std::exception& ex) {
          util::invoke_nothrow(
              cb, std::vector<Artist>{},
              std::optional<ClientError>{ClientError{ex.what()}});
        } catch (...) {
          util::invoke_nothrow(
              cb, std::vector<Artist>{},
              std::optional<ClientError>{ClientError{"Invalid JSON"}});
        }
      });
}

void Client::get_artist_albums(const std::string& artist_id, AlbumsCb cb) {
  if (!configured() || artist_id.empty()) {
    util::invoke_nothrow(
        cb, std::vector<Album>{},
        std::optional<ClientError>{ClientError{"Invalid artist request"}});
    return;
  }
  request_text(
      api_url("getArtist", {{"id", artist_id}}),
      [cb = std::move(cb)](const std::string& body,
                           std::optional<ClientError> transport) mutable {
        if (transport) {
          util::invoke_nothrow(cb, std::vector<Album>{}, transport);
          return;
        }
        try {
          const auto root = json::parse(body);
          if (auto err = parse_api_error(root)) {
            util::invoke_nothrow(cb, std::vector<Album>{}, err);
            return;
          }
          std::vector<Album> albums;
          const auto& artist = root["subsonic-response"]["artist"];
          if (artist.contains("album")) {
            albums = parse_array_or_object<Album>(artist["album"], parse_album);
          }
          util::invoke_nothrow(cb, std::move(albums), std::nullopt);
        } catch (const std::exception& ex) {
          util::invoke_nothrow(
              cb, std::vector<Album>{},
              std::optional<ClientError>{ClientError{ex.what()}});
        } catch (...) {
          util::invoke_nothrow(
              cb, std::vector<Album>{},
              std::optional<ClientError>{ClientError{"Invalid JSON"}});
        }
      });
}

void Client::get_album_list(const std::string& type, int size, int offset,
                            AlbumsCb cb) {
  if (!configured() || type.empty()) {
    util::invoke_nothrow(
        cb, std::vector<Album>{},
        std::optional<ClientError>{ClientError{"Invalid album list request"}});
    return;
  }
  const int safe_size = std::max(1, std::min(size, 500));
  const int safe_offset = std::max(0, offset);
  request_text(
      api_url("getAlbumList2",
              {{"type", type},
               {"size", std::to_string(safe_size)},
               {"offset", std::to_string(safe_offset)}}),
      [cb = std::move(cb)](const std::string& body,
                           std::optional<ClientError> transport) mutable {
        if (transport) {
          util::invoke_nothrow(cb, std::vector<Album>{}, transport);
          return;
        }
        try {
          const auto root = json::parse(body);
          if (auto err = parse_api_error(root)) {
            util::invoke_nothrow(cb, std::vector<Album>{}, err);
            return;
          }
          std::vector<Album> albums;
          const auto& list = root["subsonic-response"]["albumList2"];
          if (list.contains("album")) {
            albums = parse_array_or_object<Album>(list["album"], parse_album);
          }
          util::invoke_nothrow(cb, std::move(albums), std::nullopt);
        } catch (const std::exception& ex) {
          util::invoke_nothrow(
              cb, std::vector<Album>{},
              std::optional<ClientError>{ClientError{ex.what()}});
        } catch (...) {
          util::invoke_nothrow(
              cb, std::vector<Album>{},
              std::optional<ClientError>{ClientError{"Invalid JSON"}});
        }
      });
}

void Client::get_album_songs(const std::string& album_id, SongsCb cb) {
  if (!configured() || album_id.empty()) {
    util::invoke_nothrow(
        cb, std::vector<Song>{},
        std::optional<ClientError>{ClientError{"Invalid album request"}});
    return;
  }
  request_text(
      api_url("getAlbum", {{"id", album_id}}),
      [cb = std::move(cb)](const std::string& body,
                           std::optional<ClientError> transport) mutable {
        if (transport) {
          util::invoke_nothrow(cb, std::vector<Song>{}, transport);
          return;
        }
        try {
          const auto root = json::parse(body);
          if (auto err = parse_api_error(root)) {
            util::invoke_nothrow(cb, std::vector<Song>{}, err);
            return;
          }
          std::vector<Song> songs;
          const auto& album = root["subsonic-response"]["album"];
          if (album.contains("song")) {
            songs = parse_array_or_object<Song>(album["song"], parse_song);
          }
          util::invoke_nothrow(cb, std::move(songs), std::nullopt);
        } catch (const std::exception& ex) {
          util::invoke_nothrow(
              cb, std::vector<Song>{},
              std::optional<ClientError>{ClientError{ex.what()}});
        } catch (...) {
          util::invoke_nothrow(
              cb, std::vector<Song>{},
              std::optional<ClientError>{ClientError{"Invalid JSON"}});
        }
      });
}

void Client::get_starred(StarredCb cb) {
  if (!configured()) {
    util::invoke_nothrow(
        cb, Starred{},
        std::optional<ClientError>{ClientError{"Not connected to a server"}});
    return;
  }
  request_text(
      api_url("getStarred2"),
      [cb = std::move(cb)](const std::string& body,
                           std::optional<ClientError> transport) mutable {
        if (transport) {
          util::invoke_nothrow(cb, Starred{}, transport);
          return;
        }
        try {
          const auto root = json::parse(body);
          if (auto err = parse_api_error(root)) {
            util::invoke_nothrow(cb, Starred{}, err);
            return;
          }
          Starred starred;
          const auto& node = root["subsonic-response"]["starred2"];
          if (node.contains("artist")) {
            starred.artists =
                parse_array_or_object<Artist>(node["artist"], parse_artist);
          }
          if (node.contains("album")) {
            starred.albums =
                parse_array_or_object<Album>(node["album"], parse_album);
          }
          if (node.contains("song")) {
            starred.songs =
                parse_array_or_object<Song>(node["song"], parse_song);
          }
          util::invoke_nothrow(cb, std::move(starred), std::nullopt);
        } catch (const std::exception& ex) {
          util::invoke_nothrow(
              cb, Starred{},
              std::optional<ClientError>{ClientError{ex.what()}});
        } catch (...) {
          util::invoke_nothrow(
              cb, Starred{},
              std::optional<ClientError>{ClientError{"Invalid JSON"}});
        }
      });
}

void Client::get_random_songs(int size, SongsCb cb) {
  if (!configured()) {
    util::invoke_nothrow(
        cb, std::vector<Song>{},
        std::optional<ClientError>{ClientError{"Not connected to a server"}});
    return;
  }
  const int safe_size = std::max(1, std::min(size, 500));
  request_text(
      api_url("getRandomSongs", {{"size", std::to_string(safe_size)}}),
      [cb = std::move(cb)](const std::string& body,
                           std::optional<ClientError> transport) mutable {
        if (transport) {
          util::invoke_nothrow(cb, std::vector<Song>{}, transport);
          return;
        }
        try {
          const auto root = json::parse(body);
          if (auto err = parse_api_error(root)) {
            util::invoke_nothrow(cb, std::vector<Song>{}, err);
            return;
          }
          std::vector<Song> songs;
          const auto& random = root["subsonic-response"]["randomSongs"];
          if (random.contains("song")) {
            songs = parse_array_or_object<Song>(random["song"], parse_song);
          }
          util::invoke_nothrow(cb, std::move(songs), std::nullopt);
        } catch (const std::exception& ex) {
          util::invoke_nothrow(
              cb, std::vector<Song>{},
              std::optional<ClientError>{ClientError{ex.what()}});
        } catch (...) {
          util::invoke_nothrow(
              cb, std::vector<Song>{},
              std::optional<ClientError>{ClientError{"Invalid JSON"}});
        }
      });
}

void Client::get_cover_art(const std::string& cover_id, int size, BytesCb cb) {
  if (!configured() || cover_id.empty()) {
    util::invoke_nothrow(
        cb, std::vector<unsigned char>{},
        std::optional<ClientError>{ClientError{"Invalid cover request"}});
    return;
  }
  request_bytes(cover_url(cover_id, size), std::move(cb));
}

void Client::scrobble(const std::string& song_id, bool submission) {
  if (!configured() || song_id.empty()) {
    return;
  }
  request_text(api_url("scrobble",
                       {{"id", song_id},
                        {"submission", submission ? "true" : "false"}}),
               [](const std::string&, std::optional<ClientError>) {});
}

void Client::set_starred(const std::string& id, bool starred, DoneCb cb) {
  if (!configured() || id.empty()) {
    util::invoke_nothrow(
        cb, std::optional<ClientError>{ClientError{"Invalid star request"}});
    return;
  }
  const char* endpoint = starred ? "star" : "unstar";
  request_text(
      api_url(endpoint, {{"id", id}}),
      [cb = std::move(cb)](const std::string& body,
                           std::optional<ClientError> transport) mutable {
        if (transport) {
          util::invoke_nothrow(cb, transport);
          return;
        }
        try {
          util::invoke_nothrow(cb, parse_api_error(json::parse(body)));
        } catch (const std::exception& ex) {
          util::invoke_nothrow(
              cb, std::optional<ClientError>{ClientError{ex.what()}});
        } catch (...) {
          util::invoke_nothrow(
              cb, std::optional<ClientError>{ClientError{"Invalid JSON"}});
        }
      });
}

void Client::set_rating(const std::string& id, int rating, DoneCb cb) {
  if (!configured() || id.empty()) {
    util::invoke_nothrow(
        cb, std::optional<ClientError>{ClientError{"Invalid rating request"}});
    return;
  }
  const int safe_rating = std::max(0, std::min(rating, 5));
  request_text(
      api_url("setRating",
              {{"id", id}, {"rating", std::to_string(safe_rating)}}),
      [cb = std::move(cb)](const std::string& body,
                           std::optional<ClientError> transport) mutable {
        if (transport) {
          util::invoke_nothrow(cb, transport);
          return;
        }
        try {
          util::invoke_nothrow(cb, parse_api_error(json::parse(body)));
        } catch (const std::exception& ex) {
          util::invoke_nothrow(
              cb, std::optional<ClientError>{ClientError{ex.what()}});
        } catch (...) {
          util::invoke_nothrow(
              cb, std::optional<ClientError>{ClientError{"Invalid JSON"}});
        }
      });
}

std::string Client::stream_url(const std::string& song_id) const {
  if (!configured() || song_id.empty()) {
    return {};
  }
  return api_url("stream", {{"id", song_id}}, false);
}

std::string Client::cover_url(const std::string& cover_id, int size) const {
  if (!configured() || cover_id.empty()) {
    return {};
  }
  const int safe_size = std::max(32, std::min(size, 1200));
  return api_url("getCoverArt",
                 {{"id", cover_id}, {"size", std::to_string(safe_size)}}, false);
}

}  // namespace radio::subsonic
