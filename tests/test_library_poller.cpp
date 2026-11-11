#include "minitest.hpp"

#include "subsonic/library_poller.hpp"

TEST(LibraryPoller, build_token_api_url_shape) {
  const auto url = radio::subsonic::build_token_api_url(
      "http://host:4533/", "brett", "secret", "getArtists");
  CHECK(url.find("http://host:4533/rest/getArtists?") == 0u);
  CHECK(url.find("u=brett") != std::string::npos);
  CHECK(url.find("&t=") != std::string::npos);
  CHECK(url.find("&s=") != std::string::npos);
  CHECK(url.find("c=radio-player-sync") != std::string::npos);
  CHECK(url.find("f=json") != std::string::npos);
}

TEST(LibraryPoller, build_token_api_url_rejects_incomplete) {
  CHECK_TRUE(radio::subsonic::build_token_api_url("", "u", "p", "getArtists")
                 .empty());
  CHECK_TRUE(radio::subsonic::build_token_api_url("http://h", "", "p",
                                                  "getArtists")
                 .empty());
}

TEST(LibraryPoller, parse_artists_response) {
  const char* body = R"({
    "subsonic-response": {
      "status": "ok",
      "version": "1.16.1",
      "artists": {
        "index": [
          {"name": "A", "artist": [
            {"id": "1", "name": "Alpha", "albumCount": 2},
            {"id": "2", "name": "Amber", "albumCount": 1}
          ]},
          {"name": "B", "artist": {"id": "3", "name": "Beta", "albumCount": 4}}
        ]
      }
    }
  })";
  std::vector<radio::subsonic::Artist> artists;
  std::string error;
  CHECK_TRUE(radio::subsonic::parse_artists_response(body, artists, error));
  CHECK_TRUE(error.empty());
  CHECK_EQ(artists.size(), 3u);
  CHECK_EQ(artists[0].name, std::string("Alpha"));
  CHECK_EQ(artists[2].id, std::string("3"));
}

TEST(LibraryPoller, parse_album_list_response) {
  const char* body = R"({
    "subsonic-response": {
      "status": "ok",
      "version": "1.16.1",
      "albumList2": {
        "album": [
          {"id": "a1", "name": "One", "artist": "A"},
          {"id": "a2", "name": "Two", "artist": "B"}
        ]
      }
    }
  })";
  std::vector<radio::subsonic::Album> albums;
  std::string error;
  CHECK_TRUE(radio::subsonic::parse_album_list_response(body, albums, error));
  CHECK_EQ(albums.size(), 2u);
  CHECK_EQ(albums[1].name, std::string("Two"));
}

TEST(LibraryPoller, parse_api_error) {
  const char* body = R"({
    "subsonic-response": {
      "status": "failed",
      "error": {"code": 40, "message": "Wrong username or password"}
    }
  })";
  std::vector<radio::subsonic::Artist> artists;
  std::string error;
  CHECK_FALSE(radio::subsonic::parse_artists_response(body, artists, error));
  CHECK(error.find("Wrong username") != std::string::npos);
  CHECK_TRUE(artists.empty());
}

TEST(LibraryPoller, parse_starred_and_random) {
  const char* starred = R"({
    "subsonic-response": {
      "status": "ok",
      "starred2": {
        "artist": {"id": "a1", "name": "Fav Artist"},
        "album": [{"id": "al1", "name": "Fav Album", "artist": "X"}],
        "song": [{"id": "s1", "title": "Fav Song"}]
      }
    }
  })";
  radio::subsonic::Starred out;
  std::string error;
  CHECK_TRUE(radio::subsonic::parse_starred_response(starred, out, error));
  CHECK_EQ(out.artists.size(), 1u);
  CHECK_EQ(out.albums.size(), 1u);
  CHECK_EQ(out.songs.size(), 1u);
  CHECK_EQ(out.songs[0].title, std::string("Fav Song"));

  const char* random = R"({
    "subsonic-response": {
      "status": "ok",
      "randomSongs": {
        "song": [
          {"id": "r1", "title": "One"},
          {"id": "r2", "title": "Two"}
        ]
      }
    }
  })";
  std::vector<radio::subsonic::Song> songs;
  CHECK_TRUE(
      radio::subsonic::parse_random_songs_response(random, songs, error));
  CHECK_EQ(songs.size(), 2u);
  CHECK_EQ(songs[1].id, std::string("r2"));
}

TEST(LibraryPoller, equivalence_helpers) {
  radio::subsonic::Artist a;
  a.id = "1";
  a.name = "A";
  a.album_count = 2;
  radio::subsonic::Artist b = a;
  CHECK_TRUE(radio::subsonic::library_artists_equivalent({a}, {b}));
  b.album_count = 3;
  CHECK_FALSE(radio::subsonic::library_artists_equivalent({a}, {b}));

  radio::subsonic::Album al;
  al.id = "x";
  al.name = "N";
  al.artist = "A";
  radio::subsonic::Album bl = al;
  CHECK_TRUE(radio::subsonic::library_albums_equivalent({al}, {bl}));
  bl.name = "Other";
  CHECK_FALSE(radio::subsonic::library_albums_equivalent({al}, {bl}));
}
