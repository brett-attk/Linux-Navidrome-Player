#pragma once

#include <functional>
#include <string>
#include <vector>

namespace radio::discovery {

struct ServerInfo {
  std::string name;
  std::string url;
  std::string source;  // "udp" or "probe"
};

class LanDiscovery {
 public:
  using ProgressCb = std::function<void(const std::string& status)>;
  using ResultCb = std::function<void(std::vector<ServerInfo>)>;

  // Runs on a background thread; callbacks are invoked on the GTK main loop.
  static void start(ProgressCb progress, ResultCb done, unsigned timeout_ms = 2500);
};

}  // namespace radio::discovery
