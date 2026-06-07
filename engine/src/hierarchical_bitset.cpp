#include "hierarchical_bitset.hpp"
#include <cassert>

namespace lob {

HierarchicalBitset::HierarchicalBitset(size_t num_bits)
    : num_bits_(num_bits), num_tiers_(0) {
  assert(num_bits > 0);

  uint32_t sizes[4];
  size_t n = num_bits;
  do {
    n = (n + 63) / 64;
    sizes[num_tiers_++] = static_cast<uint32_t>(n);
  } while (n > 1);
  assert(num_tiers_ <= 4 && "Price range too wide — reduce window or increase tick size");

  uint32_t total = 0;
  for (uint8_t t = 0; t < num_tiers_; ++t) {
    tier_offsets_[t] = total;
    total += sizes[t];
  }
  words_.assign(total, 0);
}

void HierarchicalBitset::set_bit(size_t index) {
  assert(index < num_bits_);
  size_t idx = index;
  for (uint8_t t = 0; t < num_tiers_; ++t) {
    size_t word = idx / 64;
    uint64_t &w = words_[tier_offsets_[t] + word];
    uint64_t before = w;
    w |= (1ULL << (idx % 64));
    if (before != 0)
      break;
    idx = word;
  }
}

void HierarchicalBitset::reset_bit(size_t index) {
  assert(index < num_bits_);
  size_t idx = index;
  for (uint8_t t = 0; t < num_tiers_; ++t) {
    size_t word = idx / 64;
    size_t bit = idx % 64;
    uint64_t &w = words_[tier_offsets_[t] + word];
    w &= ~(1ULL << bit);
    if (w != 0)
      break;
    idx = word;
  }
}

size_t HierarchicalBitset::find_first_set_bit() const {
  assert(any() && "find_first_set_bit called on empty bitset");
  size_t word_idx = 0;
  for (int t = num_tiers_ - 1; t >= 0; --t) {
    word_idx = word_idx * 64 + __builtin_ctzll(words_[tier_offsets_[t] + word_idx]);
  }
  return word_idx;
}

size_t HierarchicalBitset::find_last_set_bit() const {
  assert(any() && "find_last_set_bit called on empty bitset");
  size_t word_idx = 0;
  for (int t = num_tiers_ - 1; t >= 0; --t) {
    word_idx = word_idx * 64 + (63 - __builtin_clzll(words_[tier_offsets_[t] + word_idx]));
  }
  return word_idx;
}

bool HierarchicalBitset::any() const {
  return words_[tier_offsets_[num_tiers_ - 1]] != 0;
}

size_t HierarchicalBitset::num_levels() const { return num_tiers_; }

} // namespace lob
