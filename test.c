
#include <stdio.h>

#pragma pack(1)
struct Foo {
  float f;
  char x;
  char y;
};
#pragma pack()

struct Bar {
  float f;
  char x;
  char y;
};

int main() {
  struct Foo arr[1];
  printf("sizeof foo is %lu\n", sizeof(struct Foo));
  printf("sizeof bar is %lu\n", sizeof(struct Bar));
  printf("sizeof arr is %lu\n", sizeof(arr));
}
