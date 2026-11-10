#pragma once

#include "subsonic/models.hpp"

#include <optional>
#include <vector>

namespace radio::queue {

enum class RepeatMode {
  Off,
  All,
  One,
};

// Playback order is split into two vectors:
//   songs_  = the actual track list (stable indices)
//   order_  = permutation of indices into songs_ (identity, or shuffled)
// current_ indexes into order_, so shuffle can reorder without moving Song objects.
class PlayQueue {
 public:
  void replace(std::vector<subsonic::Song> songs, std::size_t start_index = 0);
  void append(std::vector<subsonic::Song> songs);
  void restore(std::vector<subsonic::Song> songs, std::vector<std::size_t> order,
               int current, bool shuffle, RepeatMode repeat);

  void clear();
  bool empty() const { return songs_.empty(); }
  std::size_t size() const { return songs_.size(); }
  int current_index() const { return current_; }

  const subsonic::Song* current() const;
  subsonic::Song* current_mutable();
  bool update_annotations(const std::string& song_id,
                          const subsonic::Annotations& annotations);
  std::optional<subsonic::Song> next();
  std::optional<subsonic::Song> previous();

  void set_shuffle(bool enabled);
  bool shuffle() const { return shuffle_; }

  void cycle_repeat();
  void set_repeat(RepeatMode mode) { repeat_ = mode; }
  RepeatMode repeat() const { return repeat_; }

  const std::vector<subsonic::Song>& songs() const { return songs_; }
  const std::vector<std::size_t>& order() const { return order_; }

 private:
  void rebuild_order();

  std::vector<subsonic::Song> songs_;
  std::vector<std::size_t> order_;
  int current_ = -1;
  bool shuffle_ = false;
  RepeatMode repeat_ = RepeatMode::Off;
};

}  // namespace radio::queue
