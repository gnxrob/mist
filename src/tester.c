#include "../include/mist.h"
#include <stdio.h>

typedef struct twelvebyte { uint64_t b8[1]; uint32_t b4[1]; } twelvebyte;

int main(int argc, char* argv[]) {
  MistInit();
  size_t bag1_idx = MistAllocateBag(0, 8, 8);
  size_t bag2_idx = MistAllocateBag(0, 4, 4);
  size_t bag3_idx = MistAllocateBag(0, 16, 12);

  printf("Bag #1 index is 0x%x (MAX_FRAMES, 8 Byte Size, 8 Byte Align)\n", (uint32_t)bag1_idx);
  printf("Bag #2 index is 0x%x (MAX_FRAMES, 4 Byte Size, 4 Byte Align)\n", (uint32_t)bag2_idx);
  printf("Bag #3 index is 0x%x (MAX_FRAMES, 12 Byte Size, 16 Byte Align)\n", (uint32_t)bag3_idx);

  uint64_t* pointers64[10];
  uint32_t* pointers32[10];
  twelvebyte* pointers12byte[10];

  for (size_t z = 0; z < 10; z++) {
    pointers64[z] = (uint64_t*)MistNew(bag1_idx, 0);
    *pointers64[z] = (uint64_t)z;

    pointers32[z] = (uint32_t*)MistNew(bag2_idx, 0);
    *pointers32[z] = (uint32_t)z;

    pointers12byte[z] = (uint64_t*)MistNew(bag3_idx, 0);
    *pointers12byte[z] = (twelvebyte){ .b8 = 0xDEADBEEF + z, .b4 = 0xDEEDDEED };

    printf("pointers64 loc[%d] == (0x%p) 0x%x pointers32 loc[%d] == (0x%p) 0x%x pointers12byte loc[%d] == (0x%p) .b8 == 0x%x, .b4 == 0x%x\n", 
      z, pointers64[z], *pointers64[z], 
      z, pointers32[z], *pointers32[z],
      z, pointers12byte[z], *pointers12byte[z]->b8, *pointers12byte[z]->b4);
  }

  printf("Clearing bag %d, frame %d\n", bag2_idx, 0);

  MistZeroFrame(bag2_idx, 0);

  for (size_t z = 0; z < 10; z++) {
   printf("pointers32 loc[%d] == (0x%p) 0x%x\n", 
          z, pointers32[z], *pointers32[z]);
  }

  return 0;
}