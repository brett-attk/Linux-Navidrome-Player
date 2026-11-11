#include "minitest.hpp"

#include "util/log.hpp"
#include "util/safe.hpp"

#include <filesystem>

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
  const auto path =
      (std::filesystem::temp_directory_path() / "radio-player-safe-test.log")
          .string();
  radio::util::log_shutdown();
  radio::util::log_init_for_test(path);

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

  radio::util::log_shutdown();
  std::error_code ec;
  std::filesystem::remove(path, ec);
}
