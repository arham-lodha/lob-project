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

size_t HierarchicalBitset::find_next_set_bit(size_t pos) const {
  // Hot case: any set bit >= pos in the same tier-0 word
  {
    size_t w = pos / 64, b = pos % 64;
    uint64_t mask = words_[tier_offsets_[0] + w] & (~0ULL << b);
    if (mask)
      return w * 64 + __builtin_ctzll(mask);
  }

  // Tier-0 word exhausted — walk up tiers.
  // word_idx is the next lower-tier word index to find a set bit in.
  size_t word_idx = pos / 64 + 1;
  for (int t = 1; t < num_tiers_; ++t) {
    size_t tw = word_idx / 64;
    size_t tb = word_idx % 64;
    size_t tier_size = (t + 1 < num_tiers_) ? (tier_offsets_[t + 1] - tier_offsets_[t])
                                             : (words_.size() - tier_offsets_[t]);
    if (tw >= tier_size) {
      word_idx = tw + 1;
      continue;
    }
    uint64_t mask = words_[tier_offsets_[t] + tw] & (~0ULL << tb);
    if (mask) {
      word_idx = tw * 64 + __builtin_ctzll(mask);
      for (int s = t - 1; s >= 0; --s)
        word_idx = word_idx * 64 + __builtin_ctzll(words_[tier_offsets_[s] + word_idx]);
      return word_idx;
    }
    word_idx = tw + 1;
  }
  return SIZE_MAX;
}

size_t HierarchicalBitset::find_prev_set_bit(size_t pos) const {
  // Hot case: any set bit <= pos in the same tier-0 word
  {
    size_t w = pos / 64, b = pos % 64;
    uint64_t mask = words_[tier_offsets_[0] + w] & (~0ULL >> (63 - b));
    if (mask)
      return w * 64 + (63 - __builtin_clzll(mask));
  }

  // Tier-0 word exhausted — walk up tiers.
  if (pos < 64)
    return SIZE_MAX;
  size_t word_idx = pos / 64 - 1;
  for (int t = 1; t < num_tiers_; ++t) {
    size_t tw = word_idx / 64;
    size_t tb = word_idx % 64;
    uint64_t mask = words_[tier_offsets_[t] + tw] & (~0ULL >> (63 - tb));
    if (mask) {
      word_idx = tw * 64 + (63 - __builtin_clzll(mask));
      for (int s = t - 1; s >= 0; --s)
        word_idx = word_idx * 64 + (63 - __builtin_clzll(words_[tier_offsets_[s] + word_idx]));
      return word_idx;
    }
    if (tw == 0)
      return SIZE_MAX;
    word_idx = tw - 1;
  }
  return SIZE_MAX;
}

bool HierarchicalBitset::any() const {
  return words_[tier_offsets_[num_tiers_ - 1]] != 0;
}

size_t HierarchicalBitset::num_levels() const { return num_tiers_; }

} // namespace lob
