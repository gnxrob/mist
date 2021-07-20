#include "../include/mist.h"
#include <stdio.h>

typedef struct twelvebyte { uint64_t b8; uint32_t b4; } twelvebyte_t;

uint64_t* pointers64[0x4000];
uint32_t* pointers32[0x4000];
twelvebyte_t* pointers12byte[0x4000];

int main(int argc, char* argv[]) {
    int mist_init = MistInit();

    if (mist_init != MIST_OK) {
        printf("Failure initilizing Mist\n");
        return -1;
    }
    int bag1_result = MistAllocateBag(0, 8, 8);
    if (bag1_result < 0) {
        printf("Failure allocating bag #1\n");
        return -2;
    }
    size_t bag1_idx = (size_t)bag1_result;

    int bag2_result = MistAllocateBag(0, 4, 4);
    if (bag2_result < 0) {
        printf("Failure allocating bag #2\n");
        return -2;
    }
    size_t bag2_idx = (size_t)bag2_result;

    int bag3_result = MistAllocateBag(0, 12, 16);
    if (bag3_result < 0) {
        printf("Failure allocating bag #3\n");
        return -2;
    }
    size_t bag3_idx = (size_t)bag3_result;

    size_t z = NULL;

    printf("Bag #1 index is 0x%x (MAX_FRAMES, 8 Byte Size, 8 Byte Align) @ 0x%lx\n", (uint32_t)bag1_idx, (uint64_t)mist_manager.bag[bag1_idx].base_ptr);
    printf("Bag #2 index is 0x%x (MAX_FRAMES, 4 Byte Size, 4 Byte Align) @ 0x%lx\n", (uint32_t)bag2_idx, (uint64_t)mist_manager.bag[bag2_idx].base_ptr);
    printf("Bag #3 index is 0x%x (MAX_FRAMES, 12 Byte Size, 16 Byte Align) @ 0x%lx\n", (uint32_t)bag3_idx, (uint64_t)mist_manager.bag[bag3_idx].base_ptr);

    for (z = 0; z < 0x4000; z++) {
        pointers64[z] = (uint64_t*)MistNew(bag1_idx, 0);
        if (pointers64[z] == NULL) {
            printf("Null pointer returned on pointers64! 0x%lx at index %lu\n", pointers64[z], z);
            break;
        }
        *pointers64[z] = (uint64_t)z;

        pointers32[z] = (uint32_t*)MistNew(bag2_idx, 0);
        if (pointers32[z] == NULL) {
            printf("Null pointer returned on pointers32! 0x%lx at index %lu\n", pointers32[z], z);
            break;
        }
        *pointers32[z] = (uint32_t)z;


        uint8_t* mem_loc = (uint8_t*)MistNew(bag3_idx, 0);
        pointers12byte[z] = (twelvebyte_t*)mem_loc;
        if (pointers12byte[z] == NULL) {
            printf("Null pointer returned! 0x%lx at index %lu\n", pointers12byte[z], z);
            break;
        }

        twelvebyte_t temp12;
        temp12.b8 = (uint64_t)(0xDEADBEEF + (uint64_t)z);
        temp12.b4 = (uint32_t)(0xDEEDDEED);

        *pointers12byte[z] = temp12;
    }

    printf("Clearing bag %lu, frame %d\n", bag2_idx, 0);

    MistZeroFrame(bag2_idx, 0);

    for (z = 0; z < 10; z++) {

        printf("pointers64 loc[%lu] == (0x%p) 0x%lx pointers32 loc[%lu] == (0x%p) 0x%x pointers12byte loc[%lu] == (0x%p) .b8 == 0x%lx, .b4 == 0x%x\n", 
            z, pointers64[z], (pointers64[z] == NULL) ? 0 : *pointers64[z], 
            z, pointers32[z], (pointers32[z] == NULL) ? 0 : *pointers32[z],
            z, pointers12byte[z], (pointers12byte[z] == NULL) ? 0, 0 : (((twelvebyte_t*)pointers12byte[z])->b8), (((twelvebyte_t*)pointers12byte[z])->b4));
    }

    for (z = 0x3FFF; z > 0x3FF5; z--) {

        printf("pointers64 loc[%lu] == (0x%p) 0x%lx pointers32 loc[%lu] == (0x%p) 0x%x pointers12byte loc[%lu] == (0x%p) .b8 == 0x%lx, .b4 == 0x%x\n", 
            z, pointers64[z], (pointers64[z] == NULL) ? 0 : *pointers64[z], 
            z, pointers32[z], (pointers32[z] == NULL) ? 0 : *pointers32[z],
            z, pointers12byte[z],  (((twelvebyte_t*)pointers12byte[z])->b8), (((twelvebyte_t*)pointers12byte[z])->b4));
    }

    return 0;
}