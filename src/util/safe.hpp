#pragma once

#include <functional>
#include <string>
#include <utility>

namespace radio::util {

// Heuristic: map low-level soup/GIO error strings to a user-friendly message.
inline bool looks_like_network_error(const std::string& message) {
  // Immediately-invoked lambda: build a lowercase copy once, then search it.
  const auto lower = [&] {
    std::string out = message;
    for (char& c : out) {
      if (c >= 'A' && c <= 'Z') {
        c = static_cast<char>(c - 'A' + 'a');
      }
    }
    return out;
  }();
  return lower.find("network") != std::string::npos ||
         lower.find("resolv") != std::string::npos ||
         lower.find("connect") != std::string::npos ||
         lower.find("timed out") != std::string::npos ||
         lower.find("timeout") != std::string::npos ||
         lower.find("unreachable") != std::string::npos ||
         lower.find("connection refused") != std::string::npos ||
         lower.find("temporarily unavailable") != std::string::npos ||
         lower.find("cancelled") != std::string::npos ||
         lower.find("canceled") != std::string::npos ||
         lower.find("tls") != std::string::npos ||
         lower.find("ssl") != std::string::npos ||
         lower.find("socket") != std::string::npos;
}

inline std::string friendly_network_message(const std::string& message) {
  if (looks_like_network_error(message)) {
    return "Connection problem: check network or server, then retry";
  }
  return message;
}

// Call a std::function / lambda without letting exceptions escape.
// Fn&& + Args&&... + std::forward = "perfect forwarding": pass arguments
// through exactly as they were (lvalue stays lvalue, temporary stays temporary).
template <typename Fn, typename... Args>
void invoke_nothrow(Fn&& fn, Args&&... args) {
  try {
    // Empty std::function throws bad_function_call here; that is also swallowed.
    std::invoke(std::forward<Fn>(fn), std::forward<Args>(args)...);
  } catch (...) {
    // Keep GTK / GStreamer loops running even if a UI callback throws.
  }
}

}  // namespace radio::util
