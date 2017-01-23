#include "gtest/gtest.h"
#include "malloc.h"

/**
 * class BlumBlumShub
 *
 * This is a quick-n-dirty psuedo-random sequence generator used to
 * initialize memory blocks and then verify that they haven't been
 * modified after allocation operations.
 *
 * See https://en.wikipedia.org/wiki/Blum_Blum_Shub
 */
struct BlumBlumShub {
  constexpr static unsigned prime1 = 5651;
  constexpr static unsigned prime2 = 5623;
  constexpr static unsigned m = prime1 * prime2;

  static unsigned fix_seed(unsigned seed) { return seed + 9901; }

  static void fill(uint8_t *dst, size_t length, unsigned seed) {
    unsigned x = fix_seed(seed);

    while (length--) {
      x = (x * x) % m;
      *dst++ = static_cast<uint8_t>(x);
    }
  }

  static bool check(const uint8_t *src, size_t length, unsigned seed) {
    unsigned x = fix_seed(seed);

    while (length--) {
      x = (x * x) % m;

      if (static_cast<uint8_t>(x) != *src++)
        return false;
    }

    return true;
  }
};

struct MallocTest : public testing::Test {
  static constexpr unsigned size = 8192;

  virtual void SetUp() override {
    umm_.init();
  }

  virtual void TearDown() override {
  }

  void *malloc(size_t size, unsigned seed) {
    uint8_t *storage = reinterpret_cast<uint8_t *>(umm_.malloc(size));
    if (storage == nullptr) return nullptr;

    BlumBlumShub::fill(storage, size, seed);
    return storage;
  }

  bool check(const void *src, size_t length, unsigned seed) const {
    return BlumBlumShub::check(reinterpret_cast<const uint8_t *>(src), length, seed);
  }

  /**
   * is_block
   *
   * Tests whether a pointer actually refers to a valid used or free
   * block. Valid pointers point within the block array and have the
   * same alignment as the block array base.
   *
   * Parameters:
   *   ptr - a reference to something that might be a block
   *
   * Returns:
   *   true if the pointer is within the block array
   */
  bool is_block(const Umm::free_block_t &block) const {
    if (&block < umm_.blocks_) return false;
    if (&block >= (umm_.blocks_ + umm_.block_count_)) return false;

    auto ublock = reinterpret_cast<unsigned long long>(&block);
    auto ublocks = reinterpret_cast<unsigned long long>(umm_.blocks_);

    if ((ublock % Umm::block_size) != (ublocks % Umm::block_size)) return false;

    return true;
  }

  bool is_ptr(const void *ptr) const {
    auto blockp = reinterpret_cast<const Umm::free_block_t *>(
        reinterpret_cast<const char *>(ptr) -
        offsetof(Umm::used_block_t, data));

    return is_block(*blockp) && !umm_.is_free(*blockp);
  }

  /**
   * block_lists_are_consistent
   *
   * Checks to see that the linked lists within the block array are consistent.
   *
   * Each block must have valid next/prev pointers, and if it's a free block,
   * its next_free pointer must also be valid. The sum of the sizes of the
   * blocks on the next/prev lists must match the size of the block array.
   *
   * The sum of all the block sizes found by walking the free list must be
   * the same as what's calculated by walking the main list and counting only
   * free blocks.
   */
  bool block_lists_are_consistent() const {
    unsigned used_size = 0;
    unsigned free_size = 0;
    Umm::blockref_t prev = 0;

    auto block = &umm_.blocks_[1];
    auto end = &umm_.blocks_[umm_.block_count_ - 1];

    while (block != end) {
      if (!umm_.valid_internal_links(*block)) {
        printf("invalid block links\n");
        return false;
      }

      auto s = umm_.size_in_blocks(*block);

      if (umm_.is_free(*block)) {
        free_size += s;
      } else {
        used_size += s;
      }

      if (block->next >= umm_.block_count_) {
        printf("bad next\n");
        return false;
      }

      if ((block->prev & umm_.free_mask) != prev) {
        printf("prev was %d, expected %d\n", block->prev, prev);
        return false;
      }

      prev = umm_.index_from_block(*block);
      block = &umm_.block_from_index(block->next);
    }

    //    printf("used = %d, free = %d, actual = %d\n", used_size, free_size,
    //    block_count_);
    if ((free_size + used_size + 2) != umm_.block_count_) {
      printf("blocks missing from list\n");
      return false;
    }

    block = &umm_.block_from_index(umm_.blocks_[0].next_free);
    end = umm_.blocks_;
    unsigned free_list_total = 0;

    while (block != end) {
      free_list_total += umm_.size_in_blocks(*block);
      block = &umm_.block_from_index(block->next_free);
    }

    if (free_list_total != free_size) {
      printf("blocks missing from free list\n");
      return false;
    }

    return true;
  }

  Umm::free_block_t &block(void *ptr) const {
    return umm_.block_from_ptr(ptr);
  }

  Umm::free_block_t &next(Umm::free_block_t &block) const {
    return umm_.blocks_[block.next];
  }

  Umm::free_block_t &prev(Umm::free_block_t &block) const {
    return umm_.blocks_[block.prev & Umm::free_mask];
  }

  SizedUmm<size> umm_;
};

TEST_F(MallocTest, T0) {
  ASSERT_FALSE(is_ptr(nullptr));
}

TEST_F(MallocTest, T1) {
  void *b0 = malloc(27, 0);
  ASSERT_NE(b0, nullptr);
  ASSERT_TRUE(is_ptr(b0));

  void *b1 = malloc(200, 1);
  ASSERT_NE(b1, nullptr);
  ASSERT_TRUE(is_ptr(b1));

  void *b2 = malloc(38, 2);
  ASSERT_NE(b2, nullptr);
  ASSERT_TRUE(is_ptr(b2));

  EXPECT_TRUE(check(b0, 27, 0));
  EXPECT_TRUE(check(b1, 200, 1));
  EXPECT_TRUE(check(b2, 38, 2));
  EXPECT_TRUE(block_lists_are_consistent());
}

TEST_F(MallocTest, CantMallocBiggerThanArena) {
  void *block = umm_.malloc(MallocTest::size + 1);
  ASSERT_EQ(block, nullptr);
  EXPECT_TRUE(block_lists_are_consistent());
}

TEST_F(MallocTest, CantMallocBiggerThanArenaLessOverhead) {
  void *block = umm_.malloc(MallocTest::size - 19);
  ASSERT_EQ(block, nullptr);
  EXPECT_TRUE(block_lists_are_consistent());
}

TEST_F(MallocTest, CanMallocateOneHugeBlock) {
  void *block = umm_.malloc(MallocTest::size - 20);
  ASSERT_NE(block, nullptr);
  EXPECT_TRUE(block_lists_are_consistent());
}

TEST_F(MallocTest, MallocOfSizeZeroIsNullPtr) {
  void *block = umm_.malloc(0);
  ASSERT_EQ(block, nullptr);
  EXPECT_TRUE(block_lists_are_consistent());
}
