#include "../include/mist.h"
#include <stdio.h>

#undef TESTER_VERBOSE_DEBUG

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
    
    mist_bag_t bag1 = MistNewBag(0, 0, 8, 8);
    if (bag1.base_ptr == NULL) {
        printf("Failure allocating bag #1\n");
        return -2;
    }

    mist_bag_t bag2 = MistNewBag(0, 0, 4, 4);
    if (bag2.base_ptr == NULL) {
        printf("Failure allocating bag #2\n");
        return -3;
    }
    
    mist_bag_t bag3 = MistNewBag(0, 0, 12, 16);
    if (bag3.base_ptr == NULL) {
        printf("Failure allocating bag #3\n");
        return -4;
    }

    size_t z = 0;

    printf("Bag #1 is at b.p. %p (Frame Size: %llu, Max Frames: %llu, Type Width: %u, Type Align: %u, Aligned Width: %u)\n", bag1.base_ptr, bag1.frame_size, bag1.max_frames, bag1.alloc.width, bag1.alloc.align, bag1.alloc.aligned_width);
    printf("Bag #2 is at b.p. %p (Frame Size: %llu, Max Frames: %llu, Type Width: %u, Type Align: %u, Aligned Width: %u)\n", bag2.base_ptr, bag2.frame_size, bag2.max_frames, bag2.alloc.width, bag2.alloc.align, bag2.alloc.aligned_width);
    printf("Bag #3 is at b.p. %p (Frame Size: %llu, Max Frames: %llu, Type Width: %u, Type Align: %u, Aligned Width: %u)\n", bag3.base_ptr, bag3.frame_size, bag3.max_frames, bag3.alloc.width, bag3.alloc.align, bag3.alloc.aligned_width);

    for (z = 0; z < 0x4000; z++) {
        pointers64[z] = (uint64_t*)MistNew(&bag1, 0);
        if (pointers64[z] == NULL) {
            printf("Null pointer returned on pointers64! 0x%lx at index %lu\n", pointers64[z], z);
            break;
        }
#ifdef TESTER_VERBOSE_DEBUG
        else {
            printf("Assigning new uint64_t pointer at 0x%llx at index %lu\n", pointers64[z], z);
        }
#endif
        *(pointers64[z]) = (uint64_t)z;

        pointers32[z] = (uint32_t*)MistNew(&bag2, 0);
        if (pointers32[z] == NULL) {
            printf("Null pointer returned on pointers32! 0x%lx at index %lu\n", pointers32[z], z);
            break;
        }
#ifdef TESTER_VERBOSE_DEBUG
        else {
            printf("Assigning new uint32_t pointer at 0x%llx at index %lu\n", pointers32[z], z);
        }
#endif
        *(pointers32[z]) = (uint32_t)z;


        pointers12byte[z] = (twelvebyte_t*)MistNew(&bag3, 0);
        if (pointers12byte[z] == NULL) {
            printf("Null pointer returned! 0x%lx at index %lu\n", pointers12byte[z], z);
            break;
        }
#ifdef TESTER_VERBOSE_DEBUG
        else {
            printf("Assigning new twelvebyte_t pointer at index %lu\n", z);
        }
#endif
        twelvebyte_t temp12;
        temp12.b8 = (uint64_t)(0xDEADBEEF + (uint64_t)z);
        temp12.b4 = (uint32_t)(0xDEEDDEED);

        *(pointers12byte[z]) = temp12;
    }

    for (z = 0; z < 10; z++) {

        printf("pointers64 loc[%lu] == (0x%p) 0x%lx pointers32 loc[%lu] == (0x%p) 0x%lx pointers12byte loc[%lu] == (0x%p) .b8 == 0x%lx, .b4 == 0x%x\n", 
            z, pointers64[z], (pointers64[z] == NULL) ? 0 : *(pointers64[z]), 
            z, pointers32[z], (pointers32[z] == NULL) ? 0 : *(pointers32[z]),
            z, pointers12byte[z], (pointers12byte[z] == NULL) ? 0, 0 : (((twelvebyte_t*)pointers12byte[z])->b8), (((twelvebyte_t*)pointers12byte[z])->b4));
    }

    for (z = 0; z < 10; z++) {

        printf("pointers64 loc[%lu] == (0x%p) 0x%lx pointers32 loc[%lu] == (0x%p) 0x%lx pointers12byte loc[%lu] == (0x%p) .b8 == 0x%lx, .b4 == 0x%x\n", 
            z, pointers64[z], (pointers64[z] == NULL) ? 0 : *(pointers64[z]), 
            z, pointers32[z], (pointers32[z] == NULL) ? 0 : *(pointers32[z]),
            z, pointers12byte[z], (pointers12byte[z] == NULL) ? 0, 0 : (((twelvebyte_t*)pointers12byte[z])->b8), (((twelvebyte_t*)pointers12byte[z])->b4));
    }

    for (z = 0x3FF5; z < 0x4000; z++) {

        printf("pointers64 loc[%lu] == (0x%p) 0x%lx pointers32 loc[%lu] == (0x%p) 0x%lx pointers12byte loc[%lu] == (0x%p) .b8 == 0x%lx, .b4 == 0x%x\n", 
            z, pointers64[z], (pointers64[z] == NULL) ? 0 : *(pointers64[z]), 
            z, pointers32[z], (pointers32[z] == NULL) ? 0 : *(pointers32[z]),
            z, pointers12byte[z],  (((twelvebyte_t*)pointers12byte[z])->b8), (((twelvebyte_t*)pointers12byte[z])->b4));
    }

    return 0;
}