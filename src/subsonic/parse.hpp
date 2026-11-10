#pragma once

#include "subsonic/models.hpp"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

namespace radio::subsonic {

Annotations parse_annotations(const nlohmann::json& j);
Song parse_song(const nlohmann::json& j);
Album parse_album(const nlohmann::json& j);
Artist parse_artist(const nlohmann::json& j);
std::optional<ClientError> parse_api_error(const nlohmann::json& root);

// Subsonic sometimes returns one object and sometimes an array for the same field.
template <typename T, typename ParseFn>
std::vector<T> parse_array_or_object(const nlohmann::json& node,
                                     ParseFn parse_one) {
  std::vector<T> out;
  if (node.is_array()) {
    out.reserve(node.size());
    for (const auto& item : node) {
      out.push_back(parse_one(item));
    }
  } else if (node.is_object()) {
    out.push_back(parse_one(node));
  }
  return out;
}

}  // namespace radio::subsonic
