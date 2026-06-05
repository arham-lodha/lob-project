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
  bool any() const;
  size_t num_levels() const;
  size_t size() const { return num_bits_; }

private:
  size_t num_bits_;
  std::vector<std::vector<uint64_t>> levels_; // levels_[0]=bottom, levels_.back()=top
};

} // namespace lob
