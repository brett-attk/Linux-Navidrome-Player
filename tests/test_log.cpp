#include "minitest.hpp"

#include "util/log.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

namespace fs = std::filesystem;

TEST(Log, writes_warnings_and_errors) {
  const auto dir =
      fs::temp_directory_path() / "radio-player-log-test";
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::create_directories(dir, ec);
  const auto path = (dir / "test.log").string();

  radio::util::log_shutdown();
  radio::util::log_init_for_test(path);
  radio::util::log_warn("hello warning");
  radio::util::log_error("hello error");
  radio::util::log_shutdown();

  std::ifstream in(path);
  CHECK_TRUE(static_cast<bool>(in));
  std::string contents((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
  CHECK(contents.find("[WARN] hello warning") != std::string::npos);
  CHECK(contents.find("[ERROR] hello error") != std::string::npos);

  fs::remove_all(dir, ec);
}
