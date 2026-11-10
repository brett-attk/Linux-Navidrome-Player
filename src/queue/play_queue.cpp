#include "queue/play_queue.hpp"

#include <algorithm>
#include <numeric>
#include <random>

namespace radio::queue {

void PlayQueue::rebuild_order() {
  order_.resize(songs_.size());
  std::iota(order_.begin(), order_.end(), 0);  // 0,1,2,...,n-1
  if (shuffle_ && !order_.empty()) {
    std::mt19937 rng{std::random_device{}()};
    std::shuffle(order_.begin(), order_.end(), rng);
  }
}

void PlayQueue::replace(std::vector<subsonic::Song> songs,
                        std::size_t start_index) {
  songs_ = std::move(songs);
  rebuild_order();
  if (songs_.empty()) {
    current_ = -1;
    return;
  }
  start_index = std::min(start_index, songs_.size() - 1);
  if (shuffle_) {
    // Keep the clicked/selected track first in the shuffled order.
    auto it = std::find(order_.begin(), order_.end(), start_index);
    if (it != order_.end()) {
      std::iter_swap(order_.begin(), it);
    }
    current_ = 0;
  } else {
    current_ = static_cast<int>(start_index);
  }
}

void PlayQueue::append(std::vector<subsonic::Song> songs) {
  if (songs.empty()) {
    return;
  }
  const bool was_empty = songs_.empty();
  songs_.insert(songs_.end(), std::make_move_iterator(songs.begin()),
                std::make_move_iterator(songs.end()));
  rebuild_order();
  if (was_empty) {
    current_ = 0;
  }
}

void PlayQueue::clear() {
  songs_.clear();
  order_.clear();
  current_ = -1;
}

void PlayQueue::restore(std::vector<subsonic::Song> songs,
                        std::vector<std::size_t> order, int current,
                        bool shuffle, RepeatMode repeat) {
  songs_ = std::move(songs);
  shuffle_ = shuffle;
  repeat_ = repeat;
  if (songs_.empty()) {
    order_.clear();
    current_ = -1;
    return;
  }

  if (order.size() == songs_.size()) {
    bool valid = true;
    std::vector<bool> seen(songs_.size(), false);
    for (const auto idx : order) {
      if (idx >= songs_.size() || seen[idx]) {
        valid = false;
        break;
      }
      seen[idx] = true;
    }
    order_ = valid ? std::move(order) : std::vector<std::size_t>{};
  }
  if (order_.empty()) {
    rebuild_order();
  }
  if (current < 0 || current >= static_cast<int>(order_.size())) {
    current_ = 0;
  } else {
    current_ = current;
  }
}

const subsonic::Song* PlayQueue::current() const {
  if (current_ < 0 || current_ >= static_cast<int>(order_.size())) {
    return nullptr;
  }
  return &songs_[order_[static_cast<std::size_t>(current_)]];
}

subsonic::Song* PlayQueue::current_mutable() {
  if (current_ < 0 || current_ >= static_cast<int>(order_.size())) {
    return nullptr;
  }
  return &songs_[order_[static_cast<std::size_t>(current_)]];
}

bool PlayQueue::update_annotations(const std::string& song_id,
                                   const subsonic::Annotations& annotations) {
  bool updated = false;
  for (auto& song : songs_) {
    if (song.id == song_id) {
      song.annotations = annotations;
      updated = true;
    }
  }
  return updated;
}

std::optional<subsonic::Song> PlayQueue::next() {
  if (songs_.empty()) {
    return std::nullopt;  // empty optional = "no next track"
  }
  if (repeat_ == RepeatMode::One && current_ >= 0) {
    return *current();  // stay on the same song
  }

  if (current_ + 1 < static_cast<int>(order_.size())) {
    ++current_;
    return *current();
  }

  if (repeat_ == RepeatMode::All) {
    current_ = 0;  // wrap to start of order_
    return *current();
  }

  current_ = -1;
  return std::nullopt;
}

std::optional<subsonic::Song> PlayQueue::previous() {
  if (songs_.empty()) {
    return std::nullopt;
  }
  if (current_ > 0) {
    --current_;
  } else if (repeat_ == RepeatMode::All) {
    current_ = static_cast<int>(order_.size()) - 1;
  }
  if (current_ < 0) {
    return std::nullopt;
  }
  return *current();
}

void PlayQueue::set_shuffle(bool enabled) {
  if (shuffle_ == enabled) {
    return;
  }
  const subsonic::Song* cur = current();
  std::string id = cur ? cur->id : "";
  shuffle_ = enabled;
  rebuild_order();
  if (!id.empty()) {
    for (std::size_t i = 0; i < order_.size(); ++i) {
      if (songs_[order_[i]].id == id) {
        current_ = static_cast<int>(i);
        return;
      }
    }
  }
}

void PlayQueue::cycle_repeat() {
  switch (repeat_) {
    case RepeatMode::Off:
      repeat_ = RepeatMode::All;
      break;
    case RepeatMode::All:
      repeat_ = RepeatMode::One;
      break;
    case RepeatMode::One:
      repeat_ = RepeatMode::Off;
      break;
  }
}

}  // namespace radio::queue
