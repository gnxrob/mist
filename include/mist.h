// For booleans and the standard-width integers
#include <stdint.h>
#include <stdbool.h>

#include <stdio.h>

// A no-no
#define MIST_BAD_FRAME_INDEX ((size_t)(UINTPTR_MAX))

// undef for POSIX
#define MIST_OS_WINDOWS

// undef for no verbose debug
#undef MIST_VERBOSE_DEBUG

// Include the VirtualAlloc or mmap/munmap header as appropriate
#ifdef MIST_OS_WINDOWS
#include <windows.h>
#include <memoryapi.h>
#elif !defined(MIST_OS_WINDOWS)
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#endif

// Macros to quickly align up or down on a given value for page allocation routines
#define UDIV_UP(A, B)         ((((size_t)A) + ((size_t)B) - 1ULL) / ((size_t)B))
#define ALIGN_UP(A, B)        (UDIV_UP((size_t)A, (size_t)B) * ((size_t)B))
#define ALIGN_DOWN(A, B)      (A - (A % B))

// Helpful constants
#define MIST_START_FRAME      ((size_t)0x200000000)     // First place to allocate a bag (random to be added later)
#define MIST_MAX_BAG_ALLOC    ((size_t)0x100000000)     // Max bag size is 4GB for now, but lack of understanding is my limitation atm

// Common return values
#define MIST_OK               ((int)0)
#define MIST_NULLPTR_RETURNED ((int)-1)
#define MIST_ERROR            ((int)-2)

// Frame Based Bump Allocator
typedef struct {
    uint16_t align;        // for < MIST_FRAME_SIZE types 
    uint16_t width;        // for < MIST_FRAME_SIZE types
    size_t aligned_width;  // Do it to it yo
    uint8_t* current_ptr;  // for bump allocation (current pointer)
} mist_allocator_t;

// Bag (frame collection)
typedef struct {
    uint8_t* base_ptr;      // base pointer to frame 0
    size_t max_frames;      // maximum number of frames for this bag; if 0, then it defaults to MIST_MAX_BAG_ALLOC / MIST_FRAME_SIZE
    size_t frames;          // how many frames have been allocated
    size_t frame_size;      // what is the capacity (in bytes) of each frame in this bag
    mist_allocator_t alloc; // All bags have a bump allocator, even if it's not used
    bool init;              // has this bag been initialized
} mist_bag_t;

// Local Library Management Variables
uint8_t* _next_major_alloc_ptr = (uint8_t*)MIST_START_FRAME; // where do we put the bags in the VAS

// Helpful macros
#define MIST_CURRENT_FRAME_BASE(BAG, PTR)     ((size_t)ALIGN_DOWN((size_t)PTR, BAG->frame_size))
#define MIST_NEXT_FRAME_BASE(BAG, PTR)        ((size_t)(MIST_CURRENT_FRAME_BASE(BAG, PTR) + BAG->frame_size))
#define MIST_NTH_FRAME_BASE(BAG, PTR, N)      ((size_t)(MIST_CURRENT_FRAME_BASE(BAG, PTR) + (BAG->frame_size * ((size_t)N))))

// Function Definitions
int MistInit();
inline extern size_t MistCalculateIndex(mist_bag_t *bag, uint8_t* pointer);
inline extern size_t MistGetOSPageSize();

int MistZeroMem(uint8_t* frame_base_ptr, size_t sz);
inline extern void MistZeroFrame(size_t mist_bag_index, size_t mist_frame_index);

mist_bag_t MistNewBag(size_t frame_size, size_t max_frames, uint16_t type_width, uint16_t type_align);

int _MistMemMapFrame(uint8_t* frame_base_ptr, size_t sz, bool reserve, bool commit, bool exec);
int _MistMemUnMapFrame(mist_bag_t *bag, uint8_t* frame_base_ptr);

int MistAllocateBag(size_t max_frames, uint16_t type_width, uint16_t type_align);
void* MistAllocateFrame(mist_bag_t *bag);
void* MistNew(size_t mist_bag_index, size_t allocation_size);

// Find the frame index given a bag pointer and a pointer
inline size_t MistCalculateIndex(mist_bag_t *bag, uint8_t* pointer) {
    if (bag == NULL) return MIST_BAD_FRAME_INDEX;

    // Make sure we are initialized
    if (!bag->init) {
#ifdef MIST_VERBOSE_DEBUG
        printf("Attempt to calculate an index on an uninitialized bag\n");
#endif
        return 0;
    }

    uint8_t* frame_base = (uint8_t*)MIST_CURRENT_FRAME_BASE(bag, pointer);
    size_t xdiff = ((size_t)frame_base - (size_t)bag->base_ptr);
#ifdef MIST_VERBOSE_DEBUG
    printf("Returning %llu from MistCalculateIndex(%p, %p), frame_base = 0x%lx, xdiff = %llu\n", (xdiff / bag->frame_size), bag, pointer, frame_base, xdiff);
#endif
    return (xdiff / bag->frame_size);
}

// Find the page granularity of the host operating system
inline size_t MistGetOSPageSize() {
    static size_t page_size = 0;
    if (page_size != 0) return page_size;

#if defined(MIST_OS_WINDOWS)
    SYSTEM_INFO si;
    GetSystemInfo(&si);

    page_size = si.dwAllocationGranularity;
#elif !defined(MIST_OS_WINDOWS)
    page_size = getpagesize();
#endif
}

#define MIST_FRAME_INDEX(BAG, PTR)     (MistCalculateIndex(BAG, PTR))

// Commit field ignored on POSIX
int _MistMemMapFrame(uint8_t* frame_base_ptr, size_t sz, bool reserve, bool commit, bool exec) {

#if defined(MIST_OS_WINDOWS)
#define MIST_BAD_ALLOC (NULL)
#elif !defined(MIST_OS_WINDOWS)
#define MIST_BAD_ALLOC ((void*)-1)
#endif

    void* result = NULL;

// Platform specific bits for VirtualAlloc vs mmap
// The semantics vary ever so slightly, and the flags are different
#if defined(MIST_OS_WINDOWS)
    DWORD dFlags = 0;
    if (reserve) dFlags = MEM_RESERVE;
    if (commit) dFlags |= MEM_COMMIT;

    DWORD dProt = 0;
    if (exec) dProt = PAGE_EXECUTE_READWRITE; else dProt = PAGE_READWRITE;

    result = VirtualAlloc((LPVOID)(frame_base_ptr), sz, dFlags, dProt);
#ifdef MIST_VERBOSE_DEBUG
    printf("Calling VirtualAlloc(0x%lx, %llu, %d, %d), reserve = 0x%x, commit = 0x%x, exec = 0x%x\n", \
        frame_base_ptr, sz, dFlags, dProt, reserve, commit, exec);
    printf("VirtualAlloc result = 0x%llx\n", result);
#endif
    if (result == MIST_BAD_ALLOC) {
        printf("MIST_BAD_ALLOC returned when allocating 0x%lx in _MistMemMapFrame() - Windows\n", (uint64_t)frame_base_ptr);
        return MIST_NULLPTR_RETURNED;
    }

#elif !defined(MIST_OS_WINDOWS)
    int iFlags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED;
    if (!reserve) iFlags |= MAP_NORESERVE;
    int iProt = PROT_READ | PROT_WRITE;
    if (exec) iProt |= PROT_EXEC;

    result = mmap((void*)(frame_base_ptr), sz, iProt, iFlags, NULL, 0);
#endif
    if (result == MIST_BAD_ALLOC) {
        printf("MIST_BAD_ALLOC returned when allocating 0x%lx in _MistMemMapFrame() - POSIX\n", (uint64_t)frame_base_ptr);
        return MIST_NULLPTR_RETURNED;
    }
    else {
#ifdef MIST_VERBOSE_DEBUG
        printf("Returning MIST_OK from _MistMemMapFrame()\n");
#endif
        return MIST_OK;
    }
}

// Unmaps the specified frame
int _MistMemUnMapFrame(mist_bag_t *bag, uint8_t* frame_base_ptr) {
    // Allocate a new frame at the end of the currently allocated frameset
#ifdef MIST_OS_WINDOWS
    void* var = VirtualAlloc((LPVOID)(frame_base_ptr), bag->frame_size, MEM_RESET | MEM_COMMIT, 0);

    if (var == NULL) {
        printf("NULL returned when deallocating 0x%lx\n", (uint64_t)frame_base_ptr);
        return MIST_NULLPTR_RETURNED;
    }

#else
    munmap((void*)(frame_base_ptr), bag->frame_size);
#endif
    return MIST_OK;
}

// Construct a new bag to store memory -- this does not allocate or build a pointer;
// you must provide storage space for this bag struct.  If you set a custom frame_size, 
// it must be a multiple of the host OS page size
mist_bag_t MistNewBag(size_t frame_size, size_t max_frames, uint16_t type_width, uint16_t type_align) {
    // Set up this bag and its first frame
    mist_bag_t newbag;
    newbag.init = false;

    // Defaults
    if (MistGetOSPageSize() == 0) {
        printf("OS Page Size is Reporting Zero!!\n\n");
        goto en_fuego;
    }
    if (frame_size == 0) newbag.frame_size = MistGetOSPageSize();
    if (max_frames == 0) newbag.max_frames = (MIST_MAX_BAG_ALLOC / newbag.frame_size);
    
    // Ensure frame size is multiple of page size
    if (newbag.frame_size % MistGetOSPageSize() != 0) {
        printf("Attempting to allocate a bag with frame size that is not a multiple of host OS page size\n");
        goto en_fuego;
    }
    
    int result = _MistMemMapFrame(_next_major_alloc_ptr, MIST_MAX_BAG_ALLOC, true, false, false);
    
    if (result != MIST_OK) {
        printf("NULL returned when attempting to allocate but not commit frame #0 of a new bag\n");
        goto en_fuego;
    }

    int result2 = _MistMemMapFrame(_next_major_alloc_ptr, newbag.frame_size, false, true, false);

    if (result != MIST_OK) {
        printf("NULL returned when attempting to commit frame #0 of a new bag\n");
        goto en_fuego;
    }

    newbag.base_ptr = _next_major_alloc_ptr;

    newbag.frames = 1;

    // Allocator properties
    newbag.alloc.current_ptr = newbag.base_ptr;
    newbag.alloc.align = type_align;
    newbag.alloc.width = type_width;
    newbag.alloc.aligned_width = ALIGN_UP(type_width, type_align);

    // Pre-set where the next major allocation location
    // will occur within the VMA
    _next_major_alloc_ptr += MIST_MAX_BAG_ALLOC;

    // We are done
    newbag.init = true;

    // Which index is this bag?
    return newbag;

en_fuego:
    newbag.base_ptr = NULL;
    return newbag;
}

// Init the MIST manager
int MistInit() {
#ifdef MIST_VERBOSE_DEBUG
    printf("In MistInit()\n");
#endif

    // Ensure no double init
    static bool init = false;
    if (init) {
#ifdef MIST_VERBOSE_DEBUG
        printf("MistInit() called multiple times\n");
#endif
        return MIST_ERROR;
    }

    // Get the host os page size
    MistGetOSPageSize();

    // We are now initialized; Elvii has left the building...
    init = true;

#ifdef MIST_VERBOSE_DEBUG
    printf("Returning successfully from MistInit()\n");
#endif
    return MIST_OK;
}

// Allocate a new frame
void* MistAllocateFrame(mist_bag_t *bag) {
    if (bag == NULL) return NULL;

    // Make sure not exceeding a self-imposed page limit
    if (bag->frames + 1 > bag->max_frames) return NULL;

    // Find the base pointer & the next frame's base pointer
    uint8_t* bp = (uint8_t*)MIST_CURRENT_FRAME_BASE(bag, bag->alloc.current_ptr);
    uint8_t* next_bp = (uint8_t*)(MIST_NEXT_FRAME_BASE(bag, bp)); 

    // Allocate a new frame at the end of the currently allocated frameset
    int var = _MistMemMapFrame(next_bp, bag->frame_size, false, true, false);

    if (var != MIST_OK) {
        printf("Unable to allocate frame at next_bp %p\n", next_bp);
        return NULL;
    }

    // Not necessary -- POSIX & Windows return zeroed pages
    // MistZeroFrame(bag, bag->frames);
    bag->frames += 1;

    // Return a pointer to the first size_t in the newly allocated frame
    return (void*)next_bp;
}

// Bump Allocate a new size on a given bag
void* MistNew(mist_bag_t *bag, size_t allocation_size) {
    if (!bag->init) return NULL;

    if (allocation_size != 0 && bag->alloc.aligned_width > 0) return NULL;
    if (allocation_size != 0) allocation_size = (size_t)ALIGN_UP(allocation_size, (size_t)bag->alloc.align);
    if (allocation_size == 0) allocation_size = bag->alloc.aligned_width;

    // Is the next bump going to be on a subsequent frame?
    uint8_t* next_bump_loc = bag->alloc.current_ptr + allocation_size;

    // Calculate the frame index (from the bag base ptr) for the current bump
    // pointer and the bump pointer after the allocation
    size_t current_bump_index = MIST_FRAME_INDEX(bag, bag->alloc.current_ptr);
    size_t next_bump_index = MIST_FRAME_INDEX(bag, next_bump_loc);
    
    // A next_bump_index of zero indicates a calculation issue, so if this is not the very
    // first frame, return a NULL reference because we did not safely secure a memory location
    if ((size_t)next_bump_loc > (ALIGN_DOWN((size_t)next_bump_loc, bag->frame_size) + bag->frame_size) && next_bump_index == 0) return NULL;

#ifdef MIST_VERBOSE_DEBUG
    printf("In MistNew(%p) allocation_size = %lu, next_bump_loc = 0x%lx, next_bump_index = %lu, current_bump_index = %lu\n", \
        bag, allocation_size, next_bump_loc, next_bump_index, current_bump_index);
#endif
    size_t retptr = (size_t)bag->alloc.current_ptr;

    // See where we fall with this next bump, w.r.t. frame boundaries
    // If we are in the same frame after the allocation, no new frames to alloc
    if (next_bump_index == current_bump_index) {
        bag->alloc.current_ptr = next_bump_loc;  
#ifdef MIST_VERBOSE_DEBUG
        printf("Returning from MistNew() after allocating no frames\n");
#endif
        return (void*)retptr;
    }

    // We are 1 frame or more ahead of the current frame, so allocate as 
    // appropriate
    next_bump_loc = (uint8_t*)MistAllocateFrame(bag);
    size_t alloc_count = next_bump_index - current_bump_index;
    if ((alloc_count) > 1) {
        for (size_t z = 0; z < (alloc_count - 1); z++) {
            uint8_t* ptrval = (uint8_t*)MistAllocateFrame(bag);
            if (ptrval == NULL) {
                printf("Failure allocating subsequent frame at 0x%lx\n", (uint64_t)ptrval);
                return NULL;
            }
        }  
    }

    // Go ahead and increment the current_ptr
    bag->alloc.current_ptr = next_bump_loc;

#ifdef MIST_VERBOSE_DEBUG
    printf("Returning from MistNew(), alloc_count = %lu, retptr = 0x%llx\n", alloc_count, retptr);
#endif

    return (void*)retptr;
}
