#include "minitest.hpp"

#include "subsonic/parse.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;
using radio::subsonic::parse_album;
using radio::subsonic::parse_annotations;
using radio::subsonic::parse_api_error;
using radio::subsonic::parse_artist;
using radio::subsonic::parse_array_or_object;
using radio::subsonic::parse_song;

TEST(SubsonicParse, song_fields_and_annotations) {
  const json j = {
      {"id", "s1"},
      {"title", "Hello"},
      {"album", "World"},
      {"albumId", "al1"},
      {"artist", "Band"},
      {"artistId", "ar1"},
      {"coverArt", "c1"},
      {"genre", "Rock"},
      {"track", 3},
      {"discNumber", 2},
      {"year", 1999},
      {"duration", 241},
      {"bitRate", 320},
      {"playCount", 11},
      {"contentType", "audio/mpeg"},
      {"suffix", "mp3"},
      {"starred", "2024-01-02T03:04:05Z"},
      {"userRating", 5},
      {"averageRating", 4.2},
  };
  const auto song = parse_song(j);
  CHECK_EQ(song.id, std::string("s1"));
  CHECK_EQ(song.title, std::string("Hello"));
  CHECK_EQ(song.album_id, std::string("al1"));
  CHECK_EQ(song.track, 3);
  CHECK_EQ(song.disc_number, 2);
  CHECK_EQ(song.duration, 241);
  CHECK_EQ(song.play_count, 11);
  CHECK_TRUE(song.annotations.starred);
  CHECK_EQ(song.annotations.starred_at, std::string("2024-01-02T03:04:05Z"));
  CHECK_EQ(song.annotations.user_rating, 5);
  CHECK_NEAR(song.annotations.average_rating, 4.2, 0.0001);
}

TEST(SubsonicParse, album_and_artist) {
  const auto album = parse_album(json{
      {"id", "al"},
      {"name", "LP"},
      {"artist", "A"},
      {"artistId", "ar"},
      {"year", 2001},
      {"songCount", 10},
      {"duration", 999},
      {"playCount", 7},
  });
  CHECK_EQ(album.name, std::string("LP"));
  CHECK_EQ(album.song_count, 10);
  CHECK_EQ(album.play_count, 7);

  const auto artist = parse_artist(json{
      {"id", "ar"},
      {"name", "Artist Name"},
      {"albumCount", 4},
      {"playCount", 100},
      {"coverArt", "cov"},
  });
  CHECK_EQ(artist.album_count, 4);
  CHECK_EQ(artist.cover_art, std::string("cov"));
}

TEST(SubsonicParse, annotations_without_star) {
  const auto a = parse_annotations(json{{"userRating", 0}});
  CHECK_FALSE(a.starred);
  CHECK_TRUE(a.starred_at.empty());
}

TEST(SubsonicParse, api_error_ok_and_fail) {
  CHECK_FALSE(parse_api_error(json{{"subsonic-response", {{"status", "ok"}}}})
                  .has_value());

  auto err = parse_api_error(json{
      {"subsonic-response",
       {{"status", "failed"},
        {"error", {{"code", 40}, {"message", "Wrong user"}}}}}});
  CHECK_TRUE(err.has_value());
  CHECK_EQ(err->code, 40);
  CHECK_EQ(err->message, std::string("Wrong user"));

  err = parse_api_error(json{{"nope", 1}});
  CHECK_TRUE(err.has_value());
  CHECK_EQ(err->code, -1);
}

TEST(SubsonicParse, array_or_object) {
  const auto many = parse_array_or_object<radio::subsonic::Song>(
      json::array({json{{"id", "1"}, {"title", "A"}},
                   json{{"id", "2"}, {"title", "B"}}}),
      parse_song);
  CHECK_EQ(many.size(), 2u);
  CHECK_EQ(many[1].title, std::string("B"));

  const auto one = parse_array_or_object<radio::subsonic::Song>(
      json{{"id", "9"}, {"title", "Solo"}}, parse_song);
  CHECK_EQ(one.size(), 1u);
  CHECK_EQ(one[0].id, std::string("9"));
}
