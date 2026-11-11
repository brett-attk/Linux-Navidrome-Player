#include "subsonic/library_poller.hpp"

#include "subsonic/parse.hpp"
#include "util/safe.hpp"

#include <glib.h>
#include <libsoup/soup.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <iterator>
#include <memory>
#include <random>
#include <sstream>
#include <utility>

namespace radio::subsonic {
namespace {

using json = nlohmann::json;

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

bool http_get_text(SoupSession* session, const std::string& url,
                   std::string& body_out, std::string& error_out) {
  body_out.clear();
  error_out.clear();
  if (!session || url.empty()) {
    error_out = "Invalid library sync request";
    return false;
  }

  SoupMessage* msg = soup_message_new("GET", url.c_str());
  if (!msg) {
    error_out = "Could not create library sync request";
    return false;
  }

  GError* error = nullptr;
  GBytes* bytes =
      soup_session_send_and_read(session, msg, nullptr, &error);
  g_object_unref(msg);

  if (error != nullptr) {
    error_out = soup_error_message(error);
    g_error_free(error);
    return false;
  }
  if (!bytes) {
    error_out = "Empty response from server";
    return false;
  }

  gsize size = 0;
  const auto* data = static_cast<const char*>(g_bytes_get_data(bytes, &size));
  if (data != nullptr && size > 0) {
    body_out.assign(data, size);
  }
  g_bytes_unref(bytes);
  if (body_out.empty()) {
    error_out = "Empty response from server";
    return false;
  }
  return true;
}

struct IdlePayload {
  LibraryPoller::UpdateCb cb;
  LibrarySnapshot snapshot;
};

}  // namespace

std::string build_token_api_url(
    const std::string& base_url, const std::string& username,
    const std::string& password, const std::string& endpoint,
    const std::vector<std::pair<std::string, std::string>>& extra) {
  if (base_url.empty() || username.empty() || endpoint.empty()) {
    return {};
  }
  const std::string salt = random_salt();
  const std::string token = md5_hex(password + salt);
  std::ostringstream oss;
  oss << trim_trailing_slash(base_url) << "/rest/" << endpoint
      << "?u=" << url_encode(username) << "&t=" << token << "&s=" << salt
      << "&v=1.16.1&c=radio-player-sync&f=json";
  for (const auto& [key, value] : extra) {
    oss << '&' << key << '=' << url_encode(value);
  }
  return oss.str();
}

bool parse_artists_response(const std::string& body, std::vector<Artist>& out,
                            std::string& error_out) {
  out.clear();
  error_out.clear();
  try {
    const auto root = json::parse(body);
    if (auto err = parse_api_error(root)) {
      error_out = err->message;
      return false;
    }
    const auto& index = root["subsonic-response"]["artists"]["index"];
    if (index.is_array()) {
      for (const auto& letter : index) {
        if (!letter.contains("artist")) {
          continue;
        }
        auto batch =
            parse_array_or_object<Artist>(letter["artist"], parse_artist);
        out.insert(out.end(), batch.begin(), batch.end());
      }
    }
    return true;
  } catch (const std::exception& ex) {
    error_out = ex.what();
    return false;
  } catch (...) {
    error_out = "Invalid JSON";
    return false;
  }
}

bool parse_album_list_response(const std::string& body,
                               std::vector<Album>& out, std::string& error_out) {
  out.clear();
  error_out.clear();
  try {
    const auto root = json::parse(body);
    if (auto err = parse_api_error(root)) {
      error_out = err->message;
      return false;
    }
    const auto& list = root["subsonic-response"]["albumList2"];
    if (list.contains("album")) {
      out = parse_array_or_object<Album>(list["album"], parse_album);
    }
    return true;
  } catch (const std::exception& ex) {
    error_out = ex.what();
    return false;
  } catch (...) {
    error_out = "Invalid JSON";
    return false;
  }
}

bool parse_starred_response(const std::string& body, Starred& out,
                            std::string& error_out) {
  out = {};
  error_out.clear();
  try {
    const auto root = json::parse(body);
    if (auto err = parse_api_error(root)) {
      error_out = err->message;
      return false;
    }
    const auto& node = root["subsonic-response"]["starred2"];
    if (node.contains("artist")) {
      out.artists = parse_array_or_object<Artist>(node["artist"], parse_artist);
    }
    if (node.contains("album")) {
      out.albums = parse_array_or_object<Album>(node["album"], parse_album);
    }
    if (node.contains("song")) {
      out.songs = parse_array_or_object<Song>(node["song"], parse_song);
    }
    return true;
  } catch (const std::exception& ex) {
    error_out = ex.what();
    return false;
  } catch (...) {
    error_out = "Invalid JSON";
    return false;
  }
}

bool parse_random_songs_response(const std::string& body,
                                 std::vector<Song>& out,
                                 std::string& error_out) {
  out.clear();
  error_out.clear();
  try {
    const auto root = json::parse(body);
    if (auto err = parse_api_error(root)) {
      error_out = err->message;
      return false;
    }
    const auto& random = root["subsonic-response"]["randomSongs"];
    if (random.contains("song")) {
      out = parse_array_or_object<Song>(random["song"], parse_song);
    }
    return true;
  } catch (const std::exception& ex) {
    error_out = ex.what();
    return false;
  } catch (...) {
    error_out = "Invalid JSON";
    return false;
  }
}

bool library_artists_equivalent(const std::vector<Artist>& a,
                                const std::vector<Artist>& b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (a[i].id != b[i].id || a[i].name != b[i].name ||
        a[i].album_count != b[i].album_count ||
        a[i].annotations.starred != b[i].annotations.starred ||
        a[i].annotations.user_rating != b[i].annotations.user_rating) {
      return false;
    }
  }
  return true;
}

bool library_albums_equivalent(const std::vector<Album>& a,
                               const std::vector<Album>& b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (a[i].id != b[i].id || a[i].name != b[i].name ||
        a[i].artist != b[i].artist ||
        a[i].annotations.starred != b[i].annotations.starred ||
        a[i].annotations.user_rating != b[i].annotations.user_rating) {
      return false;
    }
  }
  return true;
}

bool library_songs_equivalent(const std::vector<Song>& a,
                              const std::vector<Song>& b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (a[i].id != b[i].id || a[i].title != b[i].title ||
        a[i].annotations.starred != b[i].annotations.starred ||
        a[i].annotations.user_rating != b[i].annotations.user_rating) {
      return false;
    }
  }
  return true;
}

LibraryPoller::LibraryPoller() = default;

LibraryPoller::~LibraryPoller() { stop(); }

void LibraryPoller::set_credentials(std::string base_url, std::string username,
                                    std::string password) {
  std::lock_guard<std::mutex> lock(mu_);
  creds_.base_url = trim_trailing_slash(std::move(base_url));
  creds_.username = std::move(username);
  creds_.password = std::move(password);
  // Do not force an immediate sync here — connect-time UI loads own the first
  // fetch; the worker follows on its delayed / periodic schedule.
  cv_.notify_all();
}

void LibraryPoller::clear_credentials() {
  std::lock_guard<std::mutex> lock(mu_);
  creds_ = {};
  sync_requested_ = false;
  cv_.notify_all();
}

void LibraryPoller::set_interval(std::chrono::seconds interval) {
  std::lock_guard<std::mutex> lock(mu_);
  interval_ = std::max(std::chrono::seconds(30), interval);
  cv_.notify_all();
}

void LibraryPoller::set_update_callback(UpdateCb cb) {
  std::lock_guard<std::mutex> lock(mu_);
  update_cb_ = std::move(cb);
}

void LibraryPoller::start() {
  std::lock_guard<std::mutex> lock(mu_);
  if (worker_.joinable()) {
    return;
  }
  stop_ = false;
  worker_ = std::thread([this] { thread_main(); });
}

void LibraryPoller::stop() {
  {
    std::lock_guard<std::mutex> lock(mu_);
    stop_ = true;
    sync_requested_ = false;
    cv_.notify_all();
  }
  if (worker_.joinable()) {
    worker_.join();
  }
}

void LibraryPoller::request_sync() {
  std::lock_guard<std::mutex> lock(mu_);
  sync_requested_ = true;
  cv_.notify_all();
}

void LibraryPoller::thread_main() {
  bool first_cycle = true;
  while (true) {
    Credentials creds;
    UpdateCb cb;
    {
      std::unique_lock<std::mutex> lock(mu_);
      if (stop_) {
        return;
      }
      // First sync waits only the short connect-settle delay; later syncs use
      // the normal interval. An explicit request_sync() skips the wait.
      if (!sync_requested_) {
        const auto wait = first_cycle ? initial_delay_ : interval_;
        cv_.wait_for(lock, wait, [this] {
          return stop_ || sync_requested_;
        });
      }
      first_cycle = false;
      if (stop_) {
        return;
      }
      sync_requested_ = false;
      creds = creds_;
      cb = update_cb_;
    }

    if (creds.base_url.empty() || creds.username.empty() || !cb) {
      continue;
    }
    if (syncing_.exchange(true)) {
      continue;
    }

    LibrarySnapshot snapshot = fetch_snapshot(creds);
    syncing_.store(false);

    auto* payload = new IdlePayload{std::move(cb), std::move(snapshot)};
    g_idle_add_full(
        G_PRIORITY_LOW,
        +[](gpointer data) -> gboolean {
          std::unique_ptr<IdlePayload> payload(static_cast<IdlePayload*>(data));
          util::invoke_nothrow(payload->cb, std::move(payload->snapshot));
          return G_SOURCE_REMOVE;
        },
        payload, nullptr);
  }
}

LibrarySnapshot LibraryPoller::fetch_snapshot(const Credentials& creds) const {
  LibrarySnapshot snapshot;

  // Dedicated session: never shares the UI client's connections or abort().
  SoupSession* session = soup_session_new_with_options(
      "max-conns", 2, "max-conns-per-host", 2, nullptr);
  if (!session) {
    snapshot.error = "Could not create library sync session";
    return snapshot;
  }
  soup_session_set_timeout(session, 25);
  soup_session_set_idle_timeout(session, 10);

  std::string body;
  std::string error;

  auto fetch_album_type = [&](const char* type, int size,
                              std::vector<Album>& out) -> bool {
    const std::string url = build_token_api_url(
        creds.base_url, creds.username, creds.password, "getAlbumList2",
        {{"type", type},
         {"size", std::to_string(size)},
         {"offset", "0"}});
    return http_get_text(session, url, body, error) &&
           parse_album_list_response(body, out, error);
  };

  const std::string artists_url = build_token_api_url(
      creds.base_url, creds.username, creds.password, "getArtists");
  if (!http_get_text(session, artists_url, body, error) ||
      !parse_artists_response(body, snapshot.artists, error)) {
    snapshot.error = error.empty() ? "Artist sync failed" : error;
    g_object_unref(session);
    return snapshot;
  }

  constexpr int kPageSize = 500;
  int offset = 0;
  while (true) {
    const std::string albums_url = build_token_api_url(
        creds.base_url, creds.username, creds.password, "getAlbumList2",
        {{"type", "alphabeticalByName"},
         {"size", std::to_string(kPageSize)},
         {"offset", std::to_string(offset)}});
    std::vector<Album> page;
    if (!http_get_text(session, albums_url, body, error) ||
        !parse_album_list_response(body, page, error)) {
      snapshot.error = error.empty() ? "Album sync failed" : error;
      g_object_unref(session);
      return snapshot;
    }
    const int got = static_cast<int>(page.size());
    snapshot.albums.insert(snapshot.albums.end(),
                           std::make_move_iterator(page.begin()),
                           std::make_move_iterator(page.end()));
    if (got < kPageSize) {
      break;
    }
    offset += got;
  }

  // Home dashboard payloads (same endpoints the interactive Home load uses).
  if (!fetch_album_type("recent", 20, snapshot.home_recent) ||
      !fetch_album_type("frequent", 30, snapshot.home_frequent) ||
      !fetch_album_type("newest", 20, snapshot.home_newest) ||
      !fetch_album_type("highest", 20, snapshot.home_highest)) {
    snapshot.error = error.empty() ? "Home album sync failed" : error;
    g_object_unref(session);
    return snapshot;
  }

  const std::string starred_url = build_token_api_url(
      creds.base_url, creds.username, creds.password, "getStarred2");
  if (!http_get_text(session, starred_url, body, error) ||
      !parse_starred_response(body, snapshot.home_starred, error)) {
    snapshot.error = error.empty() ? "Starred sync failed" : error;
    g_object_unref(session);
    return snapshot;
  }
  if (snapshot.home_starred.songs.size() > 20) {
    snapshot.home_starred.songs.resize(20);
  }

  const std::string random_url = build_token_api_url(
      creds.base_url, creds.username, creds.password, "getRandomSongs",
      {{"size", "20"}});
  if (!http_get_text(session, random_url, body, error) ||
      !parse_random_songs_response(body, snapshot.home_random_songs, error)) {
    snapshot.error = error.empty() ? "Discover sync failed" : error;
    g_object_unref(session);
    return snapshot;
  }

  g_object_unref(session);
  snapshot.ok = true;
  return snapshot;
}

}  // namespace radio::subsonic
