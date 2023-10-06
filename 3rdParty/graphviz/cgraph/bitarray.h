/// \file
/// \brief API for compacted arrays of booleans
///
/// The straightforward way to construct a dynamic array of booleans is to
/// `calloc` an array of `bool` values. However, this wastes a lot of memory.
/// Typically 8 bits per byte, which really adds up for large arrays.
///
/// The following implements an alternative that stores 8 array elements per
/// byte. Using this over the `bool` implementation described above decreases
/// heap pressure and increases locality of reference, at the cost of a few
/// (inexpensive) shifts and masks.
///
/// As a bonus, short arrays are stored directly inline, avoiding heap
/// allocation altogether. This is essentially Small String Optimization applied
/// to a boolean array.
///
/// The above design is essentially what C++’s `std::vector<bool>` does.
///
/// This is deliberately implemented header-only so even Graphviz components
/// that do not link against cgraph can use it.

#pragma once

#include "alloc.h"

/// a compressed array of boolean values
///
/// Note that this complies with the zero-is-initialization idiom. That is, C99
/// zero initializing one of these (`bitarray_t b = {0}`) or `memset`ing one of
/// these to zero gives you a valid zero-length bit array.
typedef struct {
  union {
    uint8_t block[sizeof(uint8_t *)]; ///< inline storage for small arrays
    uint8_t *base; ///< start of the underlying allocated buffer
  };
  size_t size_bits; ///< extent in bits
} bitarray_t;

/// create an array of the given element length
static inline bitarray_t bitarray_new(size_t size_bits) {

  bitarray_t ba = {};
  ba.size_bits = size_bits;

  // if the array is small enough, we can use inline storage
  if (size_bits <= sizeof(ba.block) * 8) {
    // nothing to be done

    // otherwise we need to heap-allocate
  } else {
    size_t capacity = size_bits / 8 + (size_bits % 8 == 0 ? 0 : 1);
    ba.base = (uint8_t*) gv_calloc(capacity, sizeof(uint8_t));
  }

  return ba;
}

/// get the value of the given element
static inline bool bitarray_get(bitarray_t self, size_t index) {
  assert(index < self.size_bits && "out of bounds access");

  // determine if this array is stored inline or not
  const uint8_t *base;
  if (self.size_bits <= sizeof(self.block) * 8) {
    base = self.block;
  } else {
    base = self.base;
  }

  return (base[index / 8] >> (index % 8)) & 1;
}

/// set or clear the value of the given element
static inline void bitarray_set(bitarray_t *self, size_t index, bool value) {
  assert(index < self->size_bits && "out of bounds access");

  // determine if this array is stored inline or not
  uint8_t *base;
  if (self->size_bits <= sizeof(self->block) * 8) {
    base = self->block;
  } else {
    base = self->base;
  }

  if (value) {
    base[index / 8] |= (uint8_t)(UINT8_C(1) << (index % 8));
  } else {
    base[index / 8] &= (uint8_t) ~(UINT8_C(1) << (index % 8));
  }
}

/// free underlying resources and leave a bit array empty
static inline void bitarray_reset(bitarray_t *self) {
  assert(self != NULL);

  // is this array stored out of line?
  if (self->size_bits > sizeof(self->block) * 8)
    free(self->base);

  memset(self, 0, sizeof(*self));
}
