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

  SizedUmm<size> umm_;
};

TEST_F(MallocTest, T1) {
  void *b0 = malloc(27, 0);
  ASSERT_NE(b0, nullptr);

  void *b1 = malloc(200, 1);
  ASSERT_NE(b1, nullptr);

  void *b2 = malloc(38, 2);
  ASSERT_NE(b2, nullptr);

  EXPECT_TRUE(check(b0, 27, 0));
  EXPECT_TRUE(check(b1, 200, 1));
  EXPECT_TRUE(check(b2, 38, 2));
  EXPECT_TRUE(umm_.block_list_is_consistent());
}

TEST_F(MallocTest, CantMallocBiggerThanArena) {
  void *block = umm_.malloc(MallocTest::size + 1);
  ASSERT_EQ(block, nullptr);
  EXPECT_TRUE(umm_.block_list_is_consistent());
}

TEST_F(MallocTest, CantMallocBiggerThanArenaLessOverhead) {
  void *block = umm_.malloc(MallocTest::size - 19);
  ASSERT_EQ(block, nullptr);
  EXPECT_TRUE(umm_.block_list_is_consistent());
}

TEST_F(MallocTest, CanMallocateOneHugeBlock) {
  void *block = umm_.malloc(MallocTest::size - 20);
  ASSERT_NE(block, nullptr);
  EXPECT_TRUE(umm_.block_list_is_consistent());
}

TEST_F(MallocTest, MallocOfSizeZeroIsNullPtr) {
  void *block = umm_.malloc(0);
  ASSERT_EQ(block, nullptr);
  EXPECT_TRUE(umm_.block_list_is_consistent());
}
