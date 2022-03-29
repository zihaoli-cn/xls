// Copyright 2020 The XLS Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef XLS_DATA_STRUCTURES_INLINE_BITMAP_H_
#define XLS_DATA_STRUCTURES_INLINE_BITMAP_H_

#include <cstdint>

#include "absl/base/casts.h"
#include "absl/container/inlined_vector.h"
#include "xls/common/bits_util.h"
#include "xls/common/logging/logging.h"
#include "xls/common/math_util.h"

namespace xls {

// A bitmap that has 64-bits of inline storage by default.
class InlineBitmap {
 public:
  static InlineBitmap FromWord(uint64_t word, int64_t bit_count, bool fill) {
    InlineBitmap result(bit_count, fill);
    if (bit_count != 0) {
      result.data_[0] = word & result.MaskForWord(0);
    }
    return result;
  }

  explicit InlineBitmap(int64_t bit_count, bool fill = false)
      : bit_count_(bit_count),
        data_(CeilOfRatio(bit_count, kWordBits), fill ? -1ULL : 0ULL) {
    XLS_DCHECK_GE(bit_count, 0);
  }

  bool operator==(const InlineBitmap& other) const {
    if (bit_count_ != other.bit_count_) {
      return false;
    }
    for (int64_t wordno = 0; wordno < word_count(); ++wordno) {
      uint64_t mask = MaskForWord(wordno);
      uint64_t lhs = (data_[wordno] & mask);
      uint64_t rhs = (other.data_[wordno] & mask);
      if (lhs != rhs) {
        return false;
      }
    }
    return true;
  }
  bool operator!=(const InlineBitmap& other) const { return !(*this == other); }

  int64_t bit_count() const { return bit_count_; }
  bool IsAllOnes() const {
    for (int64_t wordno = 0; wordno < word_count(); ++wordno) {
      uint64_t mask = MaskForWord(wordno);
      if ((data_[wordno] & mask) != mask) {
        return false;
      }
    }
    return true;
  }
  bool IsAllZeroes() const {
    for (int64_t wordno = 0; wordno < word_count(); ++wordno) {
      uint64_t mask = MaskForWord(wordno);
      if ((data_[wordno] & mask) != 0) {
        return false;
      }
    }
    return true;
  }
  inline bool Get(int64_t index) const {
    XLS_DCHECK_GE(index, 0);
    XLS_DCHECK_LT(index, bit_count());
    uint64_t word = data_[index / kWordBits];
    uint64_t bitno = index % kWordBits;
    return (word >> bitno) & 1ULL;
  }
  inline void Set(int64_t index, bool value) {
    XLS_DCHECK_GE(index, 0);
    XLS_DCHECK_LT(index, bit_count());
    uint64_t& word = data_[index / kWordBits];
    uint64_t bitno = index % kWordBits;
    if (value) {
      word |= 1ULL << bitno;
    } else {
      word &= ~(1ULL << bitno);
    }
  }

  // Fast path for users of the InlineBitmap to get at the 64-bit word that
  // backs a group of 64 bits.
  uint64_t GetWord(int64_t wordno) const {
    if (wordno == 0 && word_count() == 0) {
      return 0;
    }
    XLS_DCHECK_LT(wordno, word_count());
    return data_[wordno];
  }

  // Sets a byte in the data underlying the bitmap.
  //
  // Setting byte i as {b_7, b_6, b_5, ..., b_0} sets the bit at i*8 to b_0, the
  // bit at i*8+1 to b_1, and so on.
  //
  // Note: the byte-to-word mapping is defined as little endian; i.e. if the
  // bytes are set via SetByte() and then the words are observed via GetWord(),
  // the user will observe that byte 0 is mapped to the least significant bits
  // of word 0, byte 7 is mapped to the most significant bits of word 0, byte 8
  // is mapped to the least significant bits of word 1, and so on.
  void SetByte(int64_t byteno, uint8_t value) {
    XLS_DCHECK_LT(byteno, byte_count());
    // Implementation note: this relies on the endianness of the machine.
    absl::bit_cast<uint8_t*>(data_.data())[byteno] = value;
    // Ensure the data is appropriately masked in case this byte writes to that
    // region of bits.
    MaskLastWord();
  }

  uint8_t GetByte(int64_t byteno) const {
    XLS_DCHECK_LT(byteno, byte_count());
    // Implementation note: this relies on the endianness of the machine.
    return absl::bit_cast<uint8_t*>(data_.data())[byteno];
  }

  // Compares against another InlineBitmap as if they were unsigned
  // two's complement integers. If equal, returns 0. If this is greater than
  // other, returns 1. If this is less than other, returns -1.
  int64_t UCmp(const InlineBitmap& other) const {
    int64_t bit_diff = bit_count_ - other.bit_count_;
    int64_t bit_min = std::min(bit_count_, other.bit_count_);

    int64_t my_idx = bit_count_ - 1;
    int64_t other_idx = other.bit_count_ - 1;

    while (bit_diff > 0) {
      if (Get(my_idx)) {
        return 1;
      }
      my_idx--;
      bit_diff--;
    }
    while (bit_diff < 0) {
      if (other.Get(other_idx)) {
        return -1;
      }
      other_idx--;
      bit_diff++;
    }

    for (int64_t i = 0; i < bit_min; i++) {
      bool my_word = Get(my_idx);
      bool other_word = other.Get(other_idx);
      if (my_word && !other_word) {
        return 1;
      }
      if (!my_word && other_word) {
        return -1;
      }
      my_idx--;
      other_idx--;
    }

    return 0;
  }

  int64_t byte_count() const { return CeilOfRatio(bit_count_, int64_t{8}); }

  template <typename H>
  friend H AbslHashValue(H h, const InlineBitmap& ib) {
    return H::combine(std::move(h), ib.bit_count_, ib.data_);
  }

 private:
  static constexpr int64_t kWordBits = 64;
  static constexpr int64_t kWordBytes = 8;
  int64_t word_count() const { return data_.size(); }

  void MaskLastWord() {
    int64_t last_wordno = word_count() - 1;
    data_[last_wordno] &= MaskForWord(last_wordno);
  }

  // Creates a mask for the valid bits in word "wordno".
  uint64_t MaskForWord(int64_t wordno) const {
    int64_t remainder = bit_count_ % kWordBits;
    return ((wordno < word_count() - 1) || remainder == 0) ? Mask(kWordBits)
                                                           : Mask(remainder);
  }

  int64_t bit_count_;
  absl::InlinedVector<uint64_t, 1> data_;
};

}  // namespace xls

#endif  // XLS_DATA_STRUCTURES_INLINE_BITMAP_H_
