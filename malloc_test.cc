#include "gtest/gtest.h"
#include "malloc.h"

class UmmTest : public testing::Test {
protected:
  virtual void SetUp() override {
    umm_.init();
  }

  virtual void TearDown() override {
  }

  SizedUmm<8192> umm_;
};

TEST_F(UmmTest, T1) {
  void *b0 = umm_.malloc(27);
  void *b1 = umm_.malloc(200);
  void *b2 = umm_.malloc(38);

  ASSERT_NE(b0, nullptr);
  ASSERT_NE(b1, nullptr);
  ASSERT_NE(b2, nullptr);
}

TEST_F(UmmTest, CantAllocBiggerThanArena) {
  void *b0 = umm_.malloc(10000);

  ASSERT_EQ(b0, nullptr);
}
