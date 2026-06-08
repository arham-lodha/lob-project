#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace lob {

class HierarchicalBitset {
public:
  explicit HierarchicalBitset(size_t num_bits);

  void set_bit(size_t index);
  void reset_bit(size_t index);
  size_t find_first_set_bit() const; // lowest set bit — undefined if !any()
  size_t find_last_set_bit() const;  // highest set bit — undefined if !any()
  size_t find_next_set_bit(size_t pos) const; // smallest set bit >= pos, SIZE_MAX if none
  size_t find_prev_set_bit(size_t pos) const; // largest set bit <= pos, SIZE_MAX if none
  bool any() const;
  size_t num_levels() const;
  size_t size() const { return num_bits_; }

private:
  size_t num_bits_;
  uint8_t num_tiers_;
  uint32_t tier_offsets_[4]; // start index in words_ for each tier
  std::vector<uint64_t> words_; // flat layout: [tier0][tier1][tier2][tier3]
};

} // namespace lob
