#include "discovery/lan_discovery.hpp"

#include <glib.h>
#include <libsoup/soup.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace radio::discovery {
namespace {

// Discovery runs on a background std::thread. GTK widgets may only be touched
// on the main loop, so we marshal results with g_idle_add (run ASAP on main).
struct CallbackBundle {
  LanDiscovery::ProgressCb progress;
  LanDiscovery::ResultCb done;
};

void post_progress(CallbackBundle* bundle, std::string status) {
  auto* payload = new std::pair<CallbackBundle*, std::string>(bundle, std::move(status));
  g_idle_add(
      +[](gpointer data) -> gboolean {
        auto* p = static_cast<std::pair<CallbackBundle*, std::string>*>(data);
        try {
          if (p->first && p->first->progress) {
            p->first->progress(p->second);
          }
        } catch (...) {
        }
        delete p;
        return G_SOURCE_REMOVE;  // one-shot idle callback
      },
      payload);
}

void post_done(CallbackBundle* bundle, std::vector<ServerInfo> servers) {
  auto* payload =
      new std::pair<CallbackBundle*, std::vector<ServerInfo>>(bundle, std::move(servers));
  g_idle_add(
      +[](gpointer data) -> gboolean {
        auto* p =
            static_cast<std::pair<CallbackBundle*, std::vector<ServerInfo>>*>(data);
        try {
          if (p->first && p->first->done) {
            p->first->done(std::move(p->second));
          }
        } catch (...) {
        }
        delete p->first;
        delete p;
        return G_SOURCE_REMOVE;
      },
      payload);
}

std::string normalize_url(std::string url) {
  while (!url.empty() && url.back() == '/') {
    url.pop_back();
  }
  return url;
}

bool looks_like_subsonic(const std::string& body) {
  return body.find("subsonic-response") != std::string::npos;
}

bool probe_http(const std::string& url) {
  SoupSession* session = soup_session_new();
  g_object_set(session, "timeout", 2, nullptr);
  const std::string ping = url + "/rest/ping?v=1.16.1&c=radio-player&f=json";
  SoupMessage* msg = soup_message_new("GET", ping.c_str());
  if (!msg) {
    g_object_unref(session);
    return false;
  }

  GError* error = nullptr;
  GBytes* bytes =
      soup_session_send_and_read(session, msg, nullptr, &error);
  bool ok = false;
  if (!error && bytes) {
    gsize size = 0;
    const char* data = static_cast<const char*>(g_bytes_get_data(bytes, &size));
    std::string body(data, size);
    ok = looks_like_subsonic(body);
    g_bytes_unref(bytes);
  }
  if (error) {
    g_error_free(error);
  }
  g_object_unref(msg);
  g_object_unref(session);
  return ok;
}

bool tcp_open(const std::string& ip, int port, int timeout_ms) {
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return false;
  }
  int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(port));
  if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
    ::close(fd);
    return false;
  }

  int rc = ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
  if (rc == 0) {
    ::close(fd);
    return true;
  }
  if (errno != EINPROGRESS) {
    ::close(fd);
    return false;
  }

  fd_set wset;
  FD_ZERO(&wset);
  FD_SET(fd, &wset);
  timeval tv{};
  tv.tv_sec = timeout_ms / 1000;
  tv.tv_usec = (timeout_ms % 1000) * 1000;
  rc = ::select(fd + 1, nullptr, &wset, nullptr, &tv);
  if (rc <= 0) {
    ::close(fd);
    return false;
  }
  int so_error = 0;
  socklen_t len = sizeof(so_error);
  getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &len);
  ::close(fd);
  return so_error == 0;
}

std::vector<std::string> local_subnet_hosts() {
  std::set<std::string> hosts;
  ifaddrs* ifaddr = nullptr;
  if (getifaddrs(&ifaddr) != 0) {
    return {};
  }

  for (ifaddrs* ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
    if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) {
      continue;
    }
    auto* addr = reinterpret_cast<sockaddr_in*>(ifa->ifa_addr);
    auto* netmask = reinterpret_cast<sockaddr_in*>(ifa->ifa_netmask);
    if (!netmask) {
      continue;
    }
    const uint32_t ip = ntohl(addr->sin_addr.s_addr);
    const uint32_t mask = ntohl(netmask->sin_addr.s_addr);
    if ((ip & 0xff000000u) == 0x7f000000u) {
      continue;  // skip loopback
    }
    // Only scan typical home /24 networks to keep discovery snappy.
    if (__builtin_popcount(mask) < 24) {
      continue;
    }
    const uint32_t network = ip & mask;
    const uint32_t broadcast = network | ~mask;
    for (uint32_t host = network + 1; host < broadcast; ++host) {
      in_addr in{};
      in.s_addr = htonl(host);
      char buf[INET_ADDRSTRLEN] = {};
      inet_ntop(AF_INET, &in, buf, sizeof(buf));
      hosts.insert(buf);
    }
  }
  freeifaddrs(ifaddr);
  return {hosts.begin(), hosts.end()};
}

std::vector<ServerInfo> jellyfin_udp_discover(unsigned timeout_ms) {
  std::vector<ServerInfo> found;
  int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) {
    return found;
  }

  int yes = 1;
  setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

  sockaddr_in bind_addr{};
  bind_addr.sin_family = AF_INET;
  bind_addr.sin_addr.s_addr = INADDR_ANY;
  bind_addr.sin_port = 0;
  ::bind(fd, reinterpret_cast<sockaddr*>(&bind_addr), sizeof(bind_addr));

  timeval tv{};
  tv.tv_sec = timeout_ms / 1000;
  tv.tv_usec = (timeout_ms % 1000) * 1000;
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  const char* payload = "Who is JellyfinServer?";
  sockaddr_in dest{};
  dest.sin_family = AF_INET;
  dest.sin_port = htons(7359);
  dest.sin_addr.s_addr = htonl(INADDR_BROADCAST);
  ::sendto(fd, payload, std::strlen(payload), 0,
           reinterpret_cast<sockaddr*>(&dest), sizeof(dest));

  // Also broadcast on each interface subnet broadcast address.
  ifaddrs* ifaddr = nullptr;
  if (getifaddrs(&ifaddr) == 0) {
    for (ifaddrs* ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
      if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET || !ifa->ifa_netmask) {
        continue;
      }
      auto* addr = reinterpret_cast<sockaddr_in*>(ifa->ifa_addr);
      auto* netmask = reinterpret_cast<sockaddr_in*>(ifa->ifa_netmask);
      const uint32_t ip = ntohl(addr->sin_addr.s_addr);
      const uint32_t mask = ntohl(netmask->sin_addr.s_addr);
      if ((ip & 0xff000000u) == 0x7f000000u) {
        continue;
      }
      sockaddr_in bcast{};
      bcast.sin_family = AF_INET;
      bcast.sin_port = htons(7359);
      bcast.sin_addr.s_addr = htonl(ip | ~mask);
      ::sendto(fd, payload, std::strlen(payload), 0,
               reinterpret_cast<sockaddr*>(&bcast), sizeof(bcast));
    }
    freeifaddrs(ifaddr);
  }

  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    char buf[2048];
    sockaddr_in from{};
    socklen_t fromlen = sizeof(from);
    const ssize_t n =
        ::recvfrom(fd, buf, sizeof(buf) - 1, 0,
                   reinterpret_cast<sockaddr*>(&from), &fromlen);
    if (n <= 0) {
      continue;
    }
    buf[n] = '\0';
    try {
      auto j = nlohmann::json::parse(buf);
      ServerInfo info;
      info.url = normalize_url(j.value("Address", ""));
      info.name = j.value("Name", "Navidrome");
      info.source = "udp";
      if (!info.url.empty()) {
        found.push_back(std::move(info));
      }
    } catch (...) {
      // ignore malformed replies
    }
  }

  ::close(fd);
  return found;
}

std::vector<ServerInfo> port_probe_discover(LanDiscovery::ProgressCb progress,
                                            unsigned timeout_ms) {
  std::vector<ServerInfo> found;
  auto hosts = local_subnet_hosts();
  if (progress) {
    // progress already marshaled by caller via post_progress when used from thread
  }

  std::mutex mu;
  std::atomic<std::size_t> checked{0};
  const unsigned workers = std::min<unsigned>(64, std::thread::hardware_concurrency() * 8 + 8);
  std::atomic<std::size_t> next{0};
  std::vector<std::thread> threads;

  for (unsigned w = 0; w < workers; ++w) {
    threads.emplace_back([&] {
      while (true) {
        const std::size_t i = next.fetch_add(1);
        if (i >= hosts.size()) {
          break;
        }
        const auto& ip = hosts[i];
        if (tcp_open(ip, 4533, 180)) {
          const std::string url = "http://" + ip + ":4533";
          if (probe_http(url)) {
            std::lock_guard lock(mu);
            found.push_back(ServerInfo{"Navidrome @ " + ip, url, "probe"});
          }
        }
        checked.fetch_add(1);
        (void)timeout_ms;
      }
    });
  }
  for (auto& t : threads) {
    t.join();
  }
  return found;
}

}  // namespace

void LanDiscovery::start(ProgressCb progress, ResultCb done, unsigned timeout_ms) {
  auto* bundle = new CallbackBundle{std::move(progress), std::move(done)};
  // .detach(): thread outlives this function; post_done() frees `bundle`.
  std::thread([bundle, timeout_ms] {
    try {
      // Strategy 1: Jellyfin-compatible UDP broadcast (Navidrome supports it).
      post_progress(bundle, "Broadcasting Jellyfin discovery…");
      auto udp = jellyfin_udp_discover(timeout_ms);

      // Strategy 2: TCP probe common Navidrome port on the local subnet.
      post_progress(bundle, "Scanning LAN for port 4533…");
      auto probed = port_probe_discover(bundle->progress, timeout_ms);

      std::map<std::string, ServerInfo> dedup;
      for (auto& s : udp) {
        dedup[normalize_url(s.url)] = std::move(s);
      }
      for (auto& s : probed) {
        auto key = normalize_url(s.url);
        if (!dedup.contains(key)) {
          dedup[key] = std::move(s);
        }
      }

      std::vector<ServerInfo> result;
      result.reserve(dedup.size());
      for (auto& [_, info] : dedup) {
        result.push_back(std::move(info));
      }
      std::sort(result.begin(), result.end(),
                [](const ServerInfo& a, const ServerInfo& b) {
                  return a.name < b.name;
                });

      post_progress(bundle,
                    result.empty() ? "No servers found"
                                   : ("Found " + std::to_string(result.size()) +
                                      " server(s)"));
      post_done(bundle, std::move(result));
    } catch (...) {
      post_progress(bundle, "Discovery failed");
      post_done(bundle, {});
    }
  }).detach();
}

}  // namespace radio::discovery
