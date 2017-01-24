#include <array>
#include <vector>

#include "gtest/gtest.h"
#include "malloc.h"

using namespace std;

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

    for (unsigned i=0; i < length; ++i) {
      x = (x * x) % m;

      if (static_cast<uint8_t>(x) != src[i]) {
        printf("check fails at byte %d\n", i);
        return false;
      }
    }

    return true;
  }
};

struct MallocTest : public testing::Test {
  static constexpr unsigned size = 8192;
  static constexpr unsigned overhead = 2*Umm::block_size + offsetof(Umm::used_block_t, data);
  using free_block_t = Umm::free_block_t;

  virtual void SetUp() override {
    umm_.init();
  }

  virtual void *malloc(size_t size, unsigned seed) {
    uint8_t *storage = reinterpret_cast<uint8_t *>(umm_.malloc(size));
    if (storage == nullptr) return nullptr;

    BlumBlumShub::fill(storage, size, seed);
    return storage;
  }

  virtual bool check(const void *src, size_t length, unsigned seed) const {
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

    auto &first = umm_.blocks_[0];
    auto &last = umm_.blocks_[umm_.block_count_ - 1];

    if (first.next != 1) return false;
    if (first.prev != 0) return false;
    if (last.next != 0) return false;

    auto block = &umm_.blocks_[1];

    while (block != &last) {
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

    if ((last.prev & Umm::free_mask) != prev) return false;

    //    printf("used = %d, free = %d, actual = %d\n", used_size, free_size,
    //    block_count_);
    if ((free_size + used_size + 2) != umm_.block_count_) {
      printf("blocks missing from list\n");
      return false;
    }

    block = &umm_.block_from_index(umm_.blocks_[0].next_free);
    unsigned free_list_total = 0;

    while (block != &first) {
      free_list_total += umm_.size_in_blocks(*block);
      block = &umm_.block_from_index(block->next_free);
    }

    if (free_list_total != free_size) {
      printf("blocks missing from free list\n");
      return false;
    }

    return true;
  }

  virtual void consistency_check() {
    EXPECT_TRUE(block_lists_are_consistent());
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

  void calculate_usage(unsigned &free_bytes, unsigned &used_bytes) const {
    free_bytes = 0;
    used_bytes = 0;

    free_block_t *block = &umm_.blocks_[1];
    free_block_t *last = &umm_.blocks_[umm_.block_count_ - 1];

    while (block < last) {
      unsigned size = umm_.size_in_blocks(*block)*Umm::block_size;

      if (umm_.is_free(*block)) {
        free_bytes += size;
      } else {
        used_bytes += size;
      }

      block = &next(*block);
    }
  }

  unsigned free_bytes() const {
    unsigned free, used;
    calculate_usage(free, used);
    return free;
  }

  unsigned used_bytes() const {
    unsigned free, used;
    calculate_usage(free, used);
    return used;
  }

  SizedUmm<size> umm_;
};

TEST_F(MallocTest, NullptrIsNotAPointer) {
  ASSERT_FALSE(is_ptr(nullptr));
}

TEST_F(MallocTest, FreeNullptrDoesNothing) {
  size_t some_length = 100;
  unsigned some_seed = 99;

  void *block = malloc(some_length, some_seed);
  consistency_check();
  unsigned free_before = free_bytes();

  umm_.free(nullptr);
  unsigned free_after = free_bytes();

  EXPECT_TRUE(free_before == free_after);
  EXPECT_TRUE(check(block, some_length, some_seed));
}

TEST_F(MallocTest, MallocOneByte) {
  void *block = umm_.malloc(1);
  ASSERT_NE(block, nullptr);
  EXPECT_TRUE(is_ptr(block));
}

TEST_F(MallocTest, MallocSizeZeroIsNullPtr) {
  void *block = umm_.malloc(0);
  ASSERT_EQ(block, nullptr);
  consistency_check();
}

TEST_F(MallocTest, MallocOneHugeBlock) {
  void *block = umm_.malloc(MallocTest::size - overhead);
  ASSERT_NE(block, nullptr);
  consistency_check();
}

TEST_F(MallocTest, TestHugeBlockLimit) {
  unsigned free_before = free_bytes();
  void *block = umm_.malloc(MallocTest::size - (overhead - 1));
  unsigned free_after = free_bytes();

  EXPECT_EQ(free_before, free_after);
  EXPECT_EQ(block, nullptr);
  consistency_check();
}

TEST_F(MallocTest, MallocBiggerThanArena) {
  void *block = umm_.malloc(MallocTest::size + 1);
  ASSERT_EQ(block, nullptr);
  consistency_check();
}

TEST_F(MallocTest, ThreeBlocksFreedInAllPossibleOrders) {
  // specify three blocks of different sizes and contents
  struct { void *ptr;  size_t len;  unsigned seed; } block[3] = {
         {   nullptr,          27,              0  },  // block 0
         {   nullptr,         200,              1  },  // block 1
         {   nullptr,          38,              2  }}; // block 2

  // 6 ways to free all three
  vector<int> case_012 = {0, 1, 2};
  vector<int> case_021 = {0, 2, 1};
  vector<int> case_102 = {1, 0, 2};
  vector<int> case_120 = {1, 2, 0};
  vector<int> case_201 = {2, 0, 1};
  vector<int> case_210 = {2, 1, 0};

  // 6 ways to free two
  vector<int> case_01 = {0, 1};
  vector<int> case_02 = {0, 2};
  vector<int> case_10 = {1, 0};
  vector<int> case_12 = {1, 2};
  vector<int> case_20 = {2, 0};
  vector<int> case_21 = {2, 1};

  // three ways to free just one
  vector<int> case_0 = {0};
  vector<int> case_1 = {1};
  vector<int> case_2 = {2};

  // one way to free none
  vector<int> case_none = {};

  // all the possible orders
  auto all_sequences = {case_012, case_021, case_102, case_120,
                        case_201, case_210, case_01,  case_02,
                        case_10,  case_12,  case_20,  case_21,
                        case_0,   case_1,   case_2,   case_none};

  for (auto &sequence : all_sequences) {
    // re-initialize the allocator
    umm_.init();

    // set up test data
    for (auto &b : block) {
      b.ptr = malloc(b.len, b.seed); // fill block with pseudo-random data
    }

    // free some blocks
    for (auto index : sequence) {
      // make sure the test harness is working properly
      ASSERT_TRUE(block[index].ptr != nullptr);

      // free the desired block
      umm_.free(block[index].ptr);

      // and remember that it was freed
      block[index].ptr = nullptr;
    }

    for (auto &b : block) {
      if (b.ptr != nullptr) {
        // validate this block's structure
        EXPECT_TRUE(is_ptr(b.ptr));

        // validate this block's contents
        EXPECT_TRUE(check(b.ptr, b.len, b.seed));
      }
    }

    // verify overall consistency
    consistency_check();
  }
}

TEST_F(MallocTest, ReallocNullptrWithPositiveSizeSameAsMalloc) {
  unsigned size = 12;
  auto ptr = reinterpret_cast<uint8_t *>(umm_.realloc(nullptr, size));

  ASSERT_NE(ptr, nullptr);
  consistency_check();

  BlumBlumShub::fill(ptr, size, 1234);

  EXPECT_TRUE(is_ptr(ptr));
  consistency_check();
}

TEST_F(MallocTest, ReallocSmallerWhenPrevFree) {
  unsigned size = 100;
  unsigned seed0 = 123;
  unsigned seed1 = 456;

  void *ptr0 = malloc(size, seed0);
  void *ptr1 = malloc(size, seed1);

  EXPECT_TRUE(check(ptr0, size, seed0));
  EXPECT_TRUE(check(ptr1, size, seed1));
  EXPECT_LT(ptr1, ptr0);

  free_block_t &block0 = block(ptr0);
  free_block_t &block1 = block(ptr1);

  EXPECT_TRUE(&block1 == &prev(block0));

  umm_.free(ptr1);

  unsigned free_before = free_bytes();
  ptr0 = umm_.realloc(ptr0, size/2);
  unsigned free_after = free_bytes();

  EXPECT_LT(free_before, free_after);
  EXPECT_TRUE(check(ptr0, size/2, seed0));
  consistency_check();
}

TEST_F(MallocTest, ReallocSmallerWhenNextFree) {
  unsigned size = 100;
  unsigned seed0 = 123;
  unsigned seed1 = 456;

  void *ptr0 = malloc(size, seed0);
  void *ptr1 = malloc(size, seed1);

  EXPECT_TRUE(check(ptr0, size, seed0));
  EXPECT_TRUE(check(ptr1, size, seed1));
  EXPECT_LT(ptr1, ptr0);

  free_block_t &block0 = block(ptr0);
  free_block_t &block1 = block(ptr1);

  EXPECT_TRUE(&block1 == &prev(block0));

  umm_.free(ptr0);

  unsigned free_before = free_bytes();
  ptr1 = umm_.realloc(ptr1, size/2);
  unsigned free_after = free_bytes();

  EXPECT_LT(free_before, free_after);
  EXPECT_TRUE(check(ptr1, size/2, seed1));
  consistency_check();
}

TEST_F(MallocTest, ReallocSmallerWhenNextAndPrevFree) {
  unsigned size = 100;
  unsigned seed0 = 123;
  unsigned seed1 = 456;
  unsigned seed2 = 789;

  void *ptr0 = malloc(size, seed0);
  void *ptr1 = malloc(size, seed1);
  void *ptr2 = malloc(size, seed2);

  EXPECT_TRUE(check(ptr0, size, seed0));
  EXPECT_TRUE(check(ptr1, size, seed1));
  EXPECT_TRUE(check(ptr2, size, seed2));
  EXPECT_TRUE(ptr1 < ptr0);
  EXPECT_TRUE(ptr2 < ptr1);

  free_block_t &block0 = block(ptr0);
  free_block_t &block1 = block(ptr1);
  free_block_t &block2 = block(ptr2);

  EXPECT_TRUE(&block1 == &prev(block0));
  EXPECT_TRUE(&block2 == &prev(block1));

  umm_.free(ptr0);
  umm_.free(ptr2);

  unsigned free_before = free_bytes();
  ptr1 = umm_.realloc(ptr1, size/2);
  unsigned free_after = free_bytes();

  EXPECT_LT(free_before, free_after);
  EXPECT_TRUE(check(ptr1, size/2, seed1));
  consistency_check();
}

TEST_F(MallocTest, ReallocLarger) {
  unsigned size = 100;
  unsigned seed0 = 123;

  void *ptr0 = malloc(size, seed0);

  EXPECT_TRUE(check(ptr0, size, seed0));

  unsigned free_before = free_bytes();
  ptr0 = umm_.realloc(ptr0, 2*size);
  unsigned free_after = free_bytes();

  EXPECT_GT(free_before, free_after);
  EXPECT_TRUE(check(ptr0, size, seed0));
  consistency_check();
}

TEST_F(MallocTest, ReallocToZeroSizeSameAsFree) {
  size_t some_length = 100;
  unsigned some_seed = 99;

  void *ptr = malloc(some_length, some_seed);
  consistency_check();

  unsigned free_before = free_bytes();
  umm_.realloc(ptr, 0);
  unsigned free_after = free_bytes();

  EXPECT_LT(free_before, free_after);
}
