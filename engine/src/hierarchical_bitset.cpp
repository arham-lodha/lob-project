#include "hierarchical_bitset.hpp"
#include <cassert>

namespace lob {

HierarchicalBitset::HierarchicalBitset(size_t num_bits) : num_bits_(num_bits) {
  assert(num_bits > 0);
  size_t n = num_bits;
  do {
    n = (n + 63) / 64;
    levels_.push_back(std::vector<uint64_t>(n, 0));
  } while (n > 1);
  assert(levels_.size() <= 4 && "Price range too wide — reduce window or increase tick size");
}

void HierarchicalBitset::set_bit(size_t index) {
  assert(index < num_bits_);
  size_t idx = index;
  for (auto &tier : levels_) {
    size_t word = idx / 64;
    uint64_t before = tier[word];
    tier[word] |= (1ULL << (idx % 64));
    if (before != 0)
      break; // upper tiers already set — stop propagating
    idx = word;
  }
}

void HierarchicalBitset::reset_bit(size_t index) {
  assert(index < num_bits_);
  size_t idx = index;
  for (size_t t = 0; t < levels_.size(); ++t) {
    size_t word = idx / 64;
    size_t bit = idx % 64;
    levels_[t][word] &= ~(1ULL << bit);
    if (levels_[t][word] != 0)
      break; // word still has other set bits — no need to propagate upward
    idx = word;
  }
}

size_t HierarchicalBitset::find_first_set_bit() const {
  assert(any() && "find_first_set_bit called on empty bitset");
  size_t word_idx = 0;
  for (int t = (int)levels_.size() - 1; t >= 0; --t) {
    word_idx = word_idx * 64 + __builtin_ctzll(levels_[t][word_idx]);
  }
  return word_idx;
}

size_t HierarchicalBitset::find_last_set_bit() const {
  assert(any() && "find_last_set_bit called on empty bitset");
  size_t word_idx = 0;
  for (int t = (int)levels_.size() - 1; t >= 0; --t) {
    word_idx = word_idx * 64 + (63 - __builtin_clzll(levels_[t][word_idx]));
  }
  return word_idx;
}

bool HierarchicalBitset::any() const { return levels_.back()[0] != 0; }

size_t HierarchicalBitset::num_levels() const { return levels_.size(); }

} // namespace lob
