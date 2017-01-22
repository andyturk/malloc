#include <iostream>

#include "malloc.h"

SizedUmm<8192> umm;

int main(int argc, char **argv) {
  umm.init();
  std::cout << "after init\n";
  umm.dump();

  void *b0 = umm.malloc(27);
  void *b1 = umm.malloc(200);
  void *b2 = umm.malloc(38);

  std::cout << "setup\n";
  umm.dump();

  std::cout << "deleting b0\n";
  umm.free(b0);

  std::cout << "deleting b1\n";
  umm.free(b1);

  std::cout << "deleting b2\n";
  umm.free(b2);

  umm.dump();
}
