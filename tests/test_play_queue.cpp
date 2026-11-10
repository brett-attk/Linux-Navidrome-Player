#include "minitest.hpp"
#include "test_helpers.hpp"

#include "queue/play_queue.hpp"

using radio::queue::PlayQueue;
using radio::queue::RepeatMode;

TEST(PlayQueue, replace_and_current) {
  PlayQueue q;
  q.replace({make_song("a"), make_song("b"), make_song("c")}, 1);
  CHECK_FALSE(q.empty());
  CHECK_EQ(q.size(), 3u);
  CHECK(q.current() != nullptr);
  CHECK_EQ(q.current()->id, std::string("b"));
  CHECK_EQ(q.current_index(), 1);
}

TEST(PlayQueue, next_and_previous_linear) {
  PlayQueue q;
  q.replace({make_song("a"), make_song("b"), make_song("c")}, 0);
  auto n = q.next();
  CHECK(n.has_value());
  CHECK_EQ(n->id, std::string("b"));
  n = q.next();
  CHECK_EQ(n->id, std::string("c"));

  // Still on the last track — previous should step back.
  auto p = q.previous();
  CHECK(p.has_value());
  CHECK_EQ(p->id, std::string("b"));

  // Advance to end again; with Repeat Off, next clears current.
  CHECK(q.next().has_value());
  CHECK_FALSE(q.next().has_value());
  CHECK(q.current() == nullptr);
}

TEST(PlayQueue, repeat_all_wraps) {
  PlayQueue q;
  q.replace({make_song("a"), make_song("b")}, 1);
  q.set_repeat(RepeatMode::All);
  auto n = q.next();
  CHECK(n.has_value());
  CHECK_EQ(n->id, std::string("a"));
}

TEST(PlayQueue, repeat_one_stays) {
  PlayQueue q;
  q.replace({make_song("a"), make_song("b")}, 0);
  q.set_repeat(RepeatMode::One);
  auto n = q.next();
  CHECK(n.has_value());
  CHECK_EQ(n->id, std::string("a"));
  CHECK_EQ(q.current_index(), 0);
}

TEST(PlayQueue, cycle_repeat) {
  PlayQueue q;
  CHECK(q.repeat() == RepeatMode::Off);
  q.cycle_repeat();
  CHECK(q.repeat() == RepeatMode::All);
  q.cycle_repeat();
  CHECK(q.repeat() == RepeatMode::One);
  q.cycle_repeat();
  CHECK(q.repeat() == RepeatMode::Off);
}

TEST(PlayQueue, shuffle_keeps_current_song) {
  PlayQueue q;
  q.replace({make_song("a"), make_song("b"), make_song("c"), make_song("d")},
            2);
  const std::string before = q.current()->id;
  q.set_shuffle(true);
  CHECK_TRUE(q.shuffle());
  CHECK_EQ(q.current()->id, before);
  q.set_shuffle(false);
  CHECK_FALSE(q.shuffle());
  CHECK_EQ(q.current()->id, before);
}

TEST(PlayQueue, shuffle_replace_starts_with_selected) {
  PlayQueue q;
  q.set_shuffle(true);
  q.replace({make_song("a"), make_song("b"), make_song("c")}, 2);
  CHECK_EQ(q.current()->id, std::string("c"));
  CHECK_EQ(q.current_index(), 0);
}

TEST(PlayQueue, append_and_clear) {
  PlayQueue q;
  q.append({make_song("a")});
  CHECK_EQ(q.current()->id, std::string("a"));
  q.append({make_song("b")});
  CHECK_EQ(q.size(), 2u);
  q.clear();
  CHECK_TRUE(q.empty());
  CHECK(q.current() == nullptr);
}

TEST(PlayQueue, restore_valid_order) {
  PlayQueue q;
  q.restore({make_song("a"), make_song("b"), make_song("c")}, {2, 0, 1}, 1,
            true, RepeatMode::All);
  CHECK_TRUE(q.shuffle());
  CHECK(q.repeat() == RepeatMode::All);
  CHECK_EQ(q.current()->id, std::string("a"));
  CHECK_EQ(q.order()[0], 2u);
}

TEST(PlayQueue, restore_invalid_order_rebuilds) {
  PlayQueue q;
  q.restore({make_song("a"), make_song("b")}, {0, 0}, 0, false, RepeatMode::Off);
  CHECK_EQ(q.order().size(), 2u);
  CHECK_EQ(q.order()[0], 0u);
  CHECK_EQ(q.order()[1], 1u);
}

TEST(PlayQueue, update_annotations) {
  PlayQueue q;
  q.replace({make_song("a"), make_song("b")}, 0);
  radio::subsonic::Annotations ann;
  ann.starred = true;
  ann.user_rating = 4;
  CHECK_TRUE(q.update_annotations("b", ann));
  CHECK_FALSE(q.update_annotations("missing", ann));
  q.replace({make_song("a"), make_song("b")}, 1);
  // after replace annotations reset; set again on current path
  q.update_annotations("b", ann);
  CHECK_TRUE(q.current()->annotations.starred);
  CHECK_EQ(q.current()->annotations.user_rating, 4);
}
