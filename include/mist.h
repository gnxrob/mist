#include <stdio.h>

// For booleans and the standard-width integers
#include <stdint.h>
#include <stdbool.h>

// undef for POSIX
#define MIST_OS_WINDOWS

// undef for no verbose debug
#undef MIST_VERBOSE_DEBUG

// Include the VirtualAlloc or mmap/munmap header as appropriate
#ifdef MIST_OS_WINDOWS
#include <windows.h>
#include <memoryapi.h>
#else
#include <sys/mman.h>
#endif

// Macros to quickly align up or down on a given value for page allocation routines
#define UDIV_UP(A, B)         ((((size_t)A) + ((size_t)B) - 1ULL) / ((size_t)B))
#define ALIGN_UP(A, B)        (UDIV_UP((size_t)A, (size_t)B) * ((size_t)B))
#define ALIGN_DOWN(A, B)      (A - (A % B))

// Helpful constants
#define MIST_MAX_BAGS         (size_t)32
#define MIST_ACCTG_BAG        (size_t)0
#define MIST_FRAME_SIZE       ((size_t)0x1000)
#define MIST_START_FRAME      ((size_t)0x200000000)
#define MIST_MAX_BAG_ALLOC    ((size_t)0x100000000)



// Common return values
#define MIST_OK               ((int)0)
#define MIST_NULLPTR_RETURNED ((int)-1)
#define MIST_ERROR            ((int)-2)

// Bump Allocator
typedef struct {
    uint16_t align; // for < MIST_FRAME_SIZE types 
    uint16_t width; // for < MIST_FRAME_SIZE types
    size_t aligned_width; // Do it to it yo
    uint8_t* current_ptr; // for bump allocation (current pointer)
} mist_allocator_t;

// Bag (frame collection)
typedef struct {
    uint8_t* bag_base_ptr; // base pointer to frame 0
    size_t max_frames; // maximum number of frames for this bag; if 0, then it defaults to MIST_MAX_BAG_ALLOC / MIST_FRAME_SIZE
    size_t frames; // how many pages have been allocated
    mist_allocator_t alloc; // All bags have a bump allocator, even if it's not used
    bool init;
} mist_bag_t;

// Bag Manager
typedef struct {
    mist_bag_t bag[MIST_MAX_BAGS]; // bags
    size_t current_bag; // what is the "current" bag (for allocating the next bag)
} mist_manager_t;

// Local Library Management Variables
mist_manager_t mist_manager; // manages the bags
uint8_t* next_major_alloc_ptr = (uint8_t*)MIST_START_FRAME; // where do we put the bags in the VAS

                                                            // Quick Macros
#define MCB    mist_manager.current_bag
#define MBI    mist_bag_index
#define MQBI   mist_manager.bag[MBI]
#define MQCBI  mist_manager.bag[MCB]

                                                            // Helpful macros
#define MIST_CURRENT_FRAME_BASE(PTR)     ((size_t)ALIGN_DOWN((size_t)PTR, MIST_FRAME_SIZE))
#define MIST_NEXT_FRAME_BASE(PTR)        ((size_t)(MIST_CURRENT_FRAME_BASE(PTR) + (size_t)MIST_FRAME_SIZE))
#define MIST_NTH_FRAME_BASE(PTR, N)      ((size_t)(MIST_CURRENT_FRAME_BASE(PTR) + ((size_t)MIST_FRAME_SIZE * ((size_t)N))))

                                                            // Function Definitions

                                                            // Find the frame index given a bag index and a pointer
inline size_t MistCalculateIndex(size_t mist_bag_index, uint8_t* pointer) {
    // Make sure we are initialized
    if (!MQBI.init) {
#ifdef MIST_VERBOSE_DEBUG
        printf("Attempt to calculate an index on an uninitialized bag\n");
#endif
        return 0;
    }

    uint8_t* frame_base = (uint8_t*)MIST_CURRENT_FRAME_BASE(pointer);
    size_t xdiff = ((size_t)frame_base - (size_t)MQBI.bag_base_ptr);
#ifdef MIST_VERBOSE_DEBUG
    printf("Returning %lld from MistCalculateIndex(%lld, %p), frame_base = 0x%llx, xdiff = %lld\n", (xdiff / MIST_FRAME_SIZE), mist_bag_index, pointer, frame_base, xdiff);
#endif
    return (xdiff / MIST_FRAME_SIZE);
}

#define MIST_FRAME_INDEX(INDEX, PTR)     (MistCalculateIndex(INDEX, PTR))

// Zero the specified area of memory (CAREFUL, CAREFUL)
int MistZeroMem(uint8_t* frame_base_ptr, size_t sz) {
#ifdef MIST_VERBOSE_DEBUG
    printf("Zeroing Memory from frame_base_ptr = 0x%llx, sz = %lld\n", frame_base_ptr, sz);
#endif
    for (size_t z = 0; z < sz; z++) {
        *frame_base_ptr++ = '\0';
    }

    return MIST_OK;
}
inline void MistZeroFrame(size_t mist_bag_index, size_t mist_frame_index) {
    // Make sure we are initialized
    if (!MQBI.init) {
#ifdef MIST_VERBOSE_DEBUG
        printf("Returning from MistZeroFrame(%lld, %lld) because the bag at this index is not initialized\n", mist_bag_index, mist_frame_index);
#endif
        return;
    }

    uint8_t* framebase = MQBI.bag_base_ptr + (mist_frame_index * MIST_FRAME_SIZE);

#ifdef MIST_VERBOSE_DEBUG
    printf("Attempting to zero from framebase 0x%llx, frame_index = %lld\n", framebase, mist_frame_index);
#endif
    for (size_t i = 0; i < MIST_FRAME_SIZE; i++) {
        *framebase++ = (uint8_t)0;
    }
}

// Commit field ignored on POSIX
int MistMemMapFrame(uint8_t* frame_base_ptr, size_t sz, bool reserve, bool commit, bool exec) {
#ifdef MIST_OS_WINDOWS
    DWORD dFlags = 0;
    if (reserve) dFlags = MEM_RESERVE;
    if (commit) dFlags |= MEM_COMMIT;

    DWORD dProt = 0;
    if (exec) dProt = PAGE_EXECUTE_READWRITE; else dProt = PAGE_READWRITE;

    void* result = VirtualAlloc((LPVOID)(frame_base_ptr), sz, dFlags, dProt);
#ifdef MIST_VERBOSE_DEBUG
    printf("Calling VirtualAlloc(0x%llx, %lld, %d, %d), reserve = 0x%x, commit = 0x%x, exec = 0x%x\n", frame_base_ptr, sz, dFlags, dProt, reserve, commit, exec);
    printf("VirtualAlloc result = 0x%llx\n", result);
#endif
    if (result == NULL) {
        printf("NULL returned when allocating 0x%llx\n", (uint64_t)frame_base_ptr);
        return MIST_NULLPTR_RETURNED;
    }

#else
    int iFlags = 0;
    if (!reserve) iFlags |= MAP_NORESERVE;
    int iProt = PROT_READ | PROT_WRITE;
    if (exec) iProt |= PROT_EXEC;

    void* result = mmap((void*)(frame_base_ptr), sz, iProt, iFlags, NULL, 0);
#endif
    if (result == NULL) {
        return MIST_NULLPTR_RETURNED;
    }
    else {
#ifdef MIST_VERBOSE_DEBUG
        printf("Returning MIST_OK from MistMemMapFrame()\n");
#endif
        return MIST_OK;
    }
}

int MistMemUnMapFrame(uint8_t* frame_base_ptr) {
    // Allocate a new frame at the end of the currently allocated frameset
#ifdef MIST_OS_WINDOWS
    void* var = VirtualAlloc((LPVOID)(frame_base_ptr), MIST_FRAME_SIZE, MEM_RESET | MEM_COMMIT, 0);

    if (var == NULL) {
        printf("NULL returned when deallocating 0x%llx\n", (uint64_t)frame_base_ptr);
        return MIST_NULLPTR_RETURNED;
    }

#else
    munmap((void*)(new_addr), MIST_FRAME_SIZE);
#endif
    return MIST_OK;
}

// Init the MIST manager
int MistInit() {
    // Ensure no double init
    static bool init = false;
    if (init) return MIST_ERROR;

    // Configure the base bag (0) with params for bookeeping and allocate a single frame
    mist_manager.current_bag = 0;
    MQCBI.max_frames = (size_t)(MIST_MAX_BAG_ALLOC / MIST_FRAME_SIZE);

    // Forecefully map the genesis frame for our accounting block

    // Allocate our first data area, which is to reserve the full space,
    // but generally not commit at this juncture
    int result = MistMemMapFrame(next_major_alloc_ptr, MIST_MAX_BAG_ALLOC, true, false, false);
    // Make sure we actually received VAS
    if (result != MIST_OK) {
        printf("Call to allocate genesis frame on default bag returned NULL\n");
        return result;
    }

    int result2 = MistMemMapFrame(next_major_alloc_ptr, MIST_FRAME_SIZE, false, true, false);
    // Make sure the VAS commit was successful
    if (result2 != MIST_OK) {
        printf("Call to commit genesis frame on default bag returned NULL\n");
        return result2;
    }

    // Bookeeping
    MQCBI.bag_base_ptr = next_major_alloc_ptr;
    MQCBI.frames = 1;

    // Set the location of the next major bag allocation in the VMA
    next_major_alloc_ptr += MIST_MAX_BAG_ALLOC;

    // We are now initialized; Elvii has left the building...
    init = true;
    return MIST_OK;
}

// Allocate the next bag
int MistAllocateBag(size_t max_frames, uint16_t type_width, uint16_t type_align) {
    // Make sure not last
    if ((mist_manager.current_bag + 1) > MIST_MAX_BAGS) return 0;

    // Safe to increment the index
    mist_manager.current_bag++;

    // Defaults
    if (max_frames == 0) max_frames = (MIST_MAX_BAG_ALLOC / MIST_FRAME_SIZE);

    // Set up this bag and its first frame
    MQCBI.init = false;
    MQCBI.max_frames = max_frames;
    int result = MistMemMapFrame(next_major_alloc_ptr, MIST_MAX_BAG_ALLOC, true, false, false);

    if (result != MIST_OK) {
        printf("NULL returned when attempting to allocate but not commit frame #0 of a new bag\n");
        return MIST_NULLPTR_RETURNED;
    }

    int result2 = MistMemMapFrame(next_major_alloc_ptr, MIST_FRAME_SIZE, false, true, false);

    if (result != MIST_OK) {
        printf("NULL returned when attempting to commit frame #0 of a new bag\n");
        return MIST_NULLPTR_RETURNED;
    }

    MQCBI.bag_base_ptr = next_major_alloc_ptr;

    MQCBI.frames = 1;

    // Allocator properties
    MQCBI.alloc.current_ptr = MQCBI.bag_base_ptr;
    MQCBI.alloc.align = type_align;
    MQCBI.alloc.width = type_width;
    MQCBI.alloc.aligned_width = ALIGN_UP(type_width, type_align);

    // Pre-set where the next major allocation location
    // will occur within the VMA
    next_major_alloc_ptr += MIST_MAX_BAG_ALLOC;

    // We are done
    MQCBI.init = true;

    // Which index is this bag?
    return (int)mist_manager.current_bag;
}

// Allocate a new frame
void* MistAllocateFrame(size_t mist_bag_index) {
    // Make sure not exceeding a self-imposed page limit
    if (MQBI.frames + 1 > MQBI.max_frames) return NULL;

    // Find the base pointer & the next frame's base pointer
    uint8_t* bp = (uint8_t*)MIST_CURRENT_FRAME_BASE(MQBI.alloc.current_ptr);
    uint8_t* next_bp = (uint8_t*)(MIST_NEXT_FRAME_BASE(bp));

    // Allocate a new frame at the end of the currently allocated frameset
    int var = MistMemMapFrame(next_bp, MIST_FRAME_SIZE, false, true, false);

    if (var != MIST_OK) {
        printf("Unable to allocate frame at next_bp %p\n", next_bp);
        return NULL;
    }

    MistZeroFrame(mist_bag_index, MQBI.frames);
    MQBI.frames += 1;

    // Return a pointer to the first size_t in the newly allocated frame
    return (void*)next_bp;
}

// Bump Allocate a new size on a given bag
void* MistNew(size_t mist_bag_index, size_t allocation_size) {
    if (!MQBI.init) return NULL;

    if (allocation_size != 0 && MQBI.alloc.aligned_width > 0) return NULL;
    if (allocation_size != 0) allocation_size = (size_t)ALIGN_UP(allocation_size, (size_t)MQBI.alloc.aligned_width);
    if (allocation_size == 0) allocation_size = MQBI.alloc.aligned_width;

    uint8_t* next_bump_loc = MQBI.alloc.current_ptr + allocation_size;
    size_t current_bump_index = MIST_FRAME_INDEX(mist_bag_index, MQBI.alloc.current_ptr);
    size_t next_bump_index = MIST_FRAME_INDEX(mist_bag_index, next_bump_loc);
    if ((size_t)next_bump_loc >= (ALIGN_DOWN((size_t)next_bump_loc, MIST_FRAME_SIZE) + MIST_FRAME_SIZE) && next_bump_index == 0) return NULL;

#ifdef MIST_VERBOSE_DEBUG
    printf("In MistNew(%lld) allocation_size = %lld, next_bump_loc = 0x%llx, next_bump_index = %lld, current_bump_index = %lld\n", mist_bag_index, allocation_size, next_bump_loc, next_bump_index, current_bump_index);
#endif
    uint8_t* retptr = NULL;

    // See where we fall with this next bump, w.r.t. frame boundaries
    // We are in the same frame after the allocation, so no new frames
    if (next_bump_index == current_bump_index) {
        retptr = MQBI.alloc.current_ptr;
        MQBI.alloc.current_ptr = next_bump_loc;  
#ifdef MIST_VERBOSE_DEBUG
        printf("Returning from MistNew() after allocating no frames\n");
#endif
        return retptr;
    }

    // We are more than 1 frame ahead of the current frame, so allocate as 
    // appropriate
    retptr = (uint8_t*)MistAllocateFrame(mist_bag_index);
    size_t alloc_count = next_bump_index - current_bump_index;
    if ((alloc_count) > 1) {
        for (size_t z = 0; z < (alloc_count - 1); z++) {
            uint8_t* ptrval = (uint8_t*)MistAllocateFrame(mist_bag_index);
            if (ptrval == NULL) {
                printf("Failure allocating subsequent frame at 0x%llx\n", (uint64_t)ptrval);
                return NULL;
            }
        }  
    }

    MQBI.alloc.current_ptr = next_bump_loc;

#ifdef MIST_VERBOSE_DEBUG
    printf("Returning from MistNew(), alloc_count = %lld, retptr = 0x%llx\n", alloc_count, retptr);
#endif

    return (void*)retptr;
}
