#include "minitest.hpp"

#include "subsonic/client.hpp"

TEST(ClientUrls, unconfigured_returns_empty) {
  radio::subsonic::Client client;
  CHECK_FALSE(client.configured());
  CHECK_TRUE(client.stream_url("song").empty());
  CHECK_TRUE(client.cover_url("cover", 100).empty());
  CHECK_TRUE(client.base_url().empty());
}

TEST(ClientUrls, configured_builds_urls) {
  radio::subsonic::Client client;
  client.set_credentials("http://example.local:4533/", "user", "pass");
  CHECK_TRUE(client.configured());
  CHECK_EQ(client.base_url(), std::string("http://example.local:4533"));
  CHECK_EQ(client.username(), std::string("user"));

  const auto stream = client.stream_url("abc123");
  CHECK(stream.find("http://example.local:4533/rest/stream?") == 0u);
  CHECK(stream.find("id=abc123") != std::string::npos);
  CHECK(stream.find("?u=user") != std::string::npos ||
        stream.find("&u=user") != std::string::npos);
  CHECK(stream.find("&t=") != std::string::npos);
  CHECK(stream.find("&s=") != std::string::npos);
  CHECK(stream.find("f=json") == std::string::npos);  // stream is binary

  const auto cover = client.cover_url("cov", 300);
  CHECK(cover.find("/rest/getCoverArt?") != std::string::npos);
  CHECK(cover.find("id=cov") != std::string::npos);
  CHECK(cover.find("size=300") != std::string::npos);
}

TEST(ClientUrls, empty_ids_rejected) {
  radio::subsonic::Client client;
  client.set_credentials("http://host", "u", "p");
  CHECK_TRUE(client.stream_url("").empty());
  CHECK_TRUE(client.cover_url("").empty());
}
