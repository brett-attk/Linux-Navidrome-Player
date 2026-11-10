#include "minitest.hpp"

#include "util/safe.hpp"

TEST(SafeUtil, detects_network_errors) {
  CHECK_TRUE(radio::util::looks_like_network_error("Connection refused"));
  CHECK_TRUE(radio::util::looks_like_network_error("TLS handshake failed"));
  CHECK_TRUE(radio::util::looks_like_network_error("Operation was cancelled"));
  CHECK_TRUE(radio::util::looks_like_network_error("Request timed out"));
  CHECK_FALSE(radio::util::looks_like_network_error("Wrong username or password"));
}

TEST(SafeUtil, friendly_message) {
  const auto msg =
      radio::util::friendly_network_message("Could not connect to host");
  CHECK(msg.find("Connection problem") != std::string::npos);
  CHECK_EQ(radio::util::friendly_network_message("Album not found"),
           std::string("Album not found"));
}

TEST(SafeUtil, invoke_nothrow_swallows) {
  int calls = 0;
  radio::util::invoke_nothrow([&] { ++calls; });
  CHECK_EQ(calls, 1);

  radio::util::invoke_nothrow([&] {
    ++calls;
    throw std::runtime_error("boom");
  });
  CHECK_EQ(calls, 2);

  std::function<void()> empty;
  radio::util::invoke_nothrow(empty);  // no-op
}
