// The MIT License (MIT)
//
// Copyright (c) 2017 Andy Turk
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
// the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>

/**
 * MemoryAllocator
 *
 * Abstract interface for a memory allocator.
 */
struct MemoryAllocator {
  /**
   * malloc
   *
   * Allocates a contiguous block of memory.
   *
   * Parameters:
   *  size - size in bytes of the requested allocation
   *
   * Returns
   *  A non-zero pointer to memory upon success
   *  nullptr if the request cannot be satisfied
   */
  virtual void *malloc(size_t size) = 0;

  /**
   * realloc
   *
   * Changes the size of a previously allocated block of memory, possibly
   * copying the contents to a new location. Passing 0 for the first argument
   * is the same as calling malloc(size). Passing 0 for the second argument
   * is the same as calling free(ptr).
   *
   * Parameters
   *
   *   ptr  - a non-zero pointer previously returned from malloc or realloc
   *   size - the requested size for the new block
   *
   * Returns
   *   Non-zero if the request could be satisfied with a new block or by using
   *   the existing block.
   *   Zero if the request could not be satisfied. If the return value is zero
   *   when both ptr and size are non-zero, the original block of memory is
   *   left intact.
   */
  virtual void *realloc(void *ptr, size_t new_size) = 0;

  /**
   * free
   *
   * Deallocates a block of memory previously returned by malloc() or realloc().
   * Passing 0 as the argument is allowed and does nothing.
   */
  virtual void free(void *ptr) = 0;
};

struct MallocTest; // forward

class Umm : public MemoryAllocator {
  friend struct MallocTest;

protected:
  using blockref_t = uint16_t;
  static constexpr blockref_t free_bit = 0x8000;
  static constexpr blockref_t free_mask = 0x7fff;

  struct __attribute__ ((packed)) used_block_t {
    blockref_t prev;
    blockref_t next;
    uint8_t data[4];
  };

  struct __attribute__ ((packed)) free_block_t {
    blockref_t prev;
    blockref_t next;
    blockref_t prev_free;
    blockref_t next_free;
  };
  
  static constexpr size_t block_size = sizeof(free_block_t);
  static constexpr size_t block_overhead = 2*sizeof(blockref_t);

  static bool is_free(const free_block_t &block) {
    return (block.prev & free_bit) != 0;
  }

  bool is_last_block(const free_block_t &block) const {
    return block.next == 0;
  }

  free_block_t &block_from_ptr(void *ptr) const {
    return *reinterpret_cast<free_block_t *>(reinterpret_cast<char *>(ptr) -
                                             offsetof(used_block_t, data));
  }

  void *ptr_from_block(free_block_t &block) const {
    return reinterpret_cast<used_block_t *>(&block)->data;
  }

  free_block_t &block_from_index(blockref_t index) const {
    return blocks_[index];
  }

  unsigned index_from_block(const free_block_t &block) const {
    return &block - blocks_;
  }

  void set_free(free_block_t &block, bool value) {
    if (value) {
      block.prev |= free_bit;
    } else {
      block.prev &= free_mask;
    }
  }

  // returns 0 for the last block even though it's actually 1 block in length
  unsigned size_in_blocks(const free_block_t &block) const {
    if (is_last_block(block)) {
      return 0;
    } else {
      return block.next - (&block - blocks_);
    }
  }
  
  bool valid_internal_links(const free_block_t &block) const {
    if (block.next >= block_count_) return false;
    if ((block.prev & free_mask) >= block_count_) return false;

    if (is_free(block)) {
      if (block.next_free >= block_count_) return false;
    }

    return true;
  }

  free_block_t *const blocks_;
  unsigned const block_count_;

  class iterator : public std::iterator<std::forward_iterator_tag, used_block_t> {
    friend class Umm;
    free_block_t *const blocks_;
    free_block_t *p_;

    iterator(Umm &heap, free_block_t *p) : blocks_(heap.blocks_), p_(p) {}

  public:
    iterator(Umm &heap) : blocks_(heap.blocks_), p_(&blocks_[blocks_[0].next]) {
      advance_past_free_blocks();
    }

    iterator(const iterator &other)
        : blocks_(other.blocks_), p_(other.p_) {}

    void advance_past_free_blocks() {
      while (is_free(*p_)) {
        p_ = &blocks_[p_->next];
      }
    }

    iterator &operator++() {
      p_ = &blocks_[p_->next];
      advance_past_free_blocks();
      return *this;
    }

    iterator operator++(int dummy) {
      iterator tmp(*this);
      p_ = &blocks_[p_->next];
      tmp.advance_past_free_blocks();

      return tmp;
    }
    bool operator==(const iterator &rhs) const { return p_ == rhs.p_ && blocks_ == rhs.blocks_; }
    bool operator!=(const iterator &rhs) const { return p_ != rhs.p_ && blocks_ == rhs.blocks_; }
    used_block_t &operator*() { return *reinterpret_cast<used_block_t *>(p_); }
  };

  unsigned length_of(const free_block_t &block) {
    return (block.next - (&block - blocks_)) * sizeof(free_block_t);
  }

public:
  iterator begin() { return iterator(*this); }
  iterator end() { return iterator(*this, &blocks_[block_count_-1]); }

  Umm(void *storage, size_t bytes)
      : blocks_(reinterpret_cast<free_block_t *>(storage)),
        block_count_(bytes / sizeof(free_block_t)) {
    assert(block_count_ > 3);
  }

  void init() {
    const blockref_t last = block_count_ - 1;

    // The 0th block is the head of the free block list. After init(),
    // the 1st block will hold all the available space (as a free
    // block), so block 0 should simply point to it.
    blocks_[0].next = 1;
    blocks_[0].prev = 0;
    blocks_[0].next_free = 1;
    blocks_[0].prev_free = 1;

    // The 1st block gets all the free space that can be allocated
    blocks_[1].next = last;
    blocks_[1].prev = 0;
    blocks_[1].next_free = 0;
    blocks_[1].prev_free = 0;
    set_free(blocks_[1], true);

    blocks_[last].next = 0;
    blocks_[last].prev = 1;
  }

  constexpr unsigned blocks_to_hold_bytes(size_t bytes) const {
    return ((bytes + block_overhead) + (block_size - 1))/block_size;
  }

  free_block_t *find_first_free_block_of_size(unsigned blocks_requested) {
    for (free_block_t *b = &blocks_[blocks_[0].next_free];
         b != blocks_;
         b  = &blocks_[b->next_free])
    {
      if (size_in_blocks(*b) >= blocks_requested)
        return b;
    }

    return nullptr;
  }

  /**
   * split_head
   *
   * Divides a block into two smaller blocks. The size of the first
   * sub-block is passed in as an argument (specified in blocks).
   * The remaining space is then converted into the second sub-block.
   * The used/free flag of the first block is the same as the original
   * block before the split. The second block will be marked as used by
   * default.
   *
   * This method does not change the free list.
   *
   * The size of the first sub-block must (obviously) be less than the size
   * of the block to be split.
   *
   * Parameters:
   *   b0         - the block to be split
   *   split_size - size in blocks of the initial sub-block
   *
   * Returns:
   *   a reference to the second block
   */
  free_block_t &split_head(free_block_t &b0, unsigned split_size) {
    unsigned b0_index = index_from_block(b0);
    unsigned b1_index = b0_index + split_size;
    free_block_t &b1 = block_from_index(b1_index);
    free_block_t &b0_next = block_from_index(b0.next);

    // set up the new block (b1) first
    b1.prev = b0_index; // no free_bit means this is marked used
    b1.next = b0.next;

    // configure b0 as a used block
    b0.next = b1_index;

    // the block following the original b0 points back to b1, and
    // must keep its used/free flag
    b0_next.prev = b1_index | (b0_next.prev & free_bit);

    return b1;
  }

  /**
   * split_tail
   *
   * Divides a block into two smaller blocks. The size of the second block
   * is specified (in blocks, not bytes). This size must (obviously) be less
   * than the size of the block to be split.
   *
   * This method does not change the free list, and the used/free flag of
   * the first block is the same as the original block. The second block
   * will be marked as used by default.
   *
   * When using this method to split a free block, there's no need to update
   * the free list.
   *
   * Parameters:
   *   b0         - the block to be split
   *   split_size - size in blocks of the second sub-block
   *
   * Returns:
   *   reference to the second block
   */
  used_block_t &split_tail(free_block_t &b0, unsigned split_size) {
    unsigned b0_index = index_from_block(b0);
    unsigned b1_index = b0.next - split_size;
    free_block_t &b1 = block_from_index(b1_index);
    free_block_t &b0_next = block_from_index(b0.next);

    b1.prev = b0_index; // no free_bit means this is used
    b1.next = b0.next;
    b0.next = b1_index;

    // the block following the original b0 points back to b1, and
    // must keep its used/free flag
    b0_next.prev = b1_index | (b0_next.prev & free_bit);

    return *reinterpret_cast<used_block_t *>(&b1);
  }
  
  /**
   * unfree
   *
   * Modifies a block that's currently on the free list to be an allocated
   * block. The size of the block remains the same. The free flag is cleared.
   *
   * Parameters:
   *   [in] b - reference to the free block
   *
   * Returns:
   *   reference to the used block
   */
  used_block_t & unfree(free_block_t &b) {
    free_block_t &prev_free = blocks_[b.prev_free];
    free_block_t &next_free = blocks_[b.next_free];

    prev_free.next_free = b.next_free;
    next_free.prev_free = b.prev_free;
    set_free(b, false);

    return *reinterpret_cast<used_block_t *>(&b);
  }
  
  virtual void *malloc(size_t size) override {
    if (size == 0) return nullptr;

    unsigned blocks_required = blocks_to_hold_bytes(size);
    free_block_t *b = find_first_free_block_of_size(blocks_required);
    if (b == nullptr) return nullptr;

    unsigned b_size = size_in_blocks(*b);
    if (b_size > (blocks_required + 1)) {
      return split_tail(*b, blocks_required).data;
    } else {
      return unfree(*b).data;
    }
  }

  virtual void *realloc(void *ptr, size_t new_size_in_bytes) override {
    if (ptr == nullptr)
      return malloc(new_size_in_bytes);

    if (new_size_in_bytes == 0) {
      free(ptr);
      return nullptr;
    }

    unsigned new_size = blocks_to_hold_bytes(new_size_in_bytes);
    free_block_t &block = block_from_ptr(ptr);
    free_block_t &prev = block_from_index(block.prev /* & free_mask */);
    free_block_t &next = block_from_index(block.next);
    unsigned current_size = size_in_blocks(block);

    if (new_size < (current_size - 1)) {
      // shrink the block and try to avoid fragmentation

      if (is_free(next)) {
        // merge unused space at the end with the next block (which is free)
        unfree(next);
        free_block_t &tail = split_head(block, new_size);
        join(tail, next);
        free(tail);

        return ptr_from_block(block);
      } else if (is_free(prev)) {
        // new location will be at the end of the current block
        used_block_t &tail = *reinterpret_cast<used_block_t *>(blocks_ + block.next - new_size);

        // shift the kept portion to the end
        memmove(tail.data, ptr, new_size_in_bytes);

        // split the block
        /* &tail == */ split_tail(block, new_size);

        // merge the previous (free) block with the unused portion
        join(prev, block);
        return tail.data;
      } else {
        free_block_t &tail = split_head(block, new_size);
        free(tail);
        return ptr_from_block(block);
      }
    } else if (new_size > current_size) {
      // find a new larger block and copy the data there
      free_block_t *new_block = find_first_free_block_of_size(new_size);
      unsigned current_size_in_bytes =
          current_size * block_size - offsetof(used_block_t, data);

      if (new_block) {
        unfree(*new_block);
        memmove(ptr_from_block(*new_block), ptr, current_size_in_bytes);
        free(block);

        return ptr_from_block(*new_block);
      } else {
        return nullptr; // failure
      }
    } else {
      // keep the existing block
      return ptr;
    }
  }

  /**
   * join
   *
   * Merges two physically adjacent blocks into one larger block,
   * eliminating the second. The first block retains its free/used
   * status and simply becomes larger. The order of the arguments is
   * important and the block with the lower address must be passed
   * as the first argument.
   *
   * The free list is not updated by this method.
   *
   * Parameters:
   *   b0 - the lower (surviving) block
   *   b1 - the adjacent block to be merged
   */
  void join(free_block_t &b0, free_block_t &b1) {
  assert(b0.next == (&b1 - blocks_));
  assert((b1.prev & free_mask) == &b0 - blocks_);

  b0.next = b1.next;

  free_block_t &next = blocks_[b1.next];
  next.prev = b1.prev & free_mask;
  }

  void free(free_block_t &block) {
    free_block_t &prev = block_from_index(block.prev & free_mask);
    free_block_t &next = block_from_index(block.next);

    // when the block following this one is free, merge the two
    if (is_free(next)) {
      unfree(next);
      join(block, next);
    }

    // when the previous block is free, merge it with this one (which may have
    // already been merged with its successor)
    if (is_free(prev)) {
      join(prev, block);
    } else {
      // add this block to the head of the free list
      unsigned index = &block - blocks_;

      block.next_free = blocks_[0].next_free;
      block.prev_free = 0;
      set_free(block, true);

      blocks_[blocks_[0].next_free].prev_free = index;
      blocks_[0].next_free = index;
    }
  }

  virtual void free(void *ptr) override {
    if (ptr == nullptr) return;
    free(block_from_ptr(ptr));
  }

  void dump() {
    free_block_t *b = blocks_;

    do {
      unsigned block_id = b - blocks_;
      unsigned block_length = length_of(*b);

      if (block_id == 0) {
        printf(" 0000: [%04d, %04d] [%04d, %04d] free list\n",
               b->prev, b->next, b->prev_free, b->next_free);
      } else if (is_free(*b)) {
        printf("*%04d: [%04d, %04d] [%04d, %04d] %d bytes\n", block_id,
               (b->prev & free_mask), b->next, b->prev_free, b->next_free,
               block_length);
      } else {
        printf(" %04d: [%04d, %04d] %d bytes\n", block_id,
               (b->prev & free_mask), b->next, block_length);
      }

      b = &blocks_[b->next];
    } while (b > blocks_);
  }
};

template <unsigned N> class SizedUmm : public Umm {
  uint8_t storage_[N];

public:
  SizedUmm() : Umm(storage_, sizeof(storage_)) {}
};
