#ifndef _MIST_H
#define _MIST_H

// This code is NOT yet multithread safe; future revisions
// will place mutexes around bag and frame (de)allocations

// For booleans and the standard-width integers
#include <stdint.h>
#include <stdbool.h>

// Helpful constants
#define MIST_MAX_BAGS        (size_t)16
#define MIST_BASE_BAG        (size_t)0
#define MIST_START_FRAME     (size_t)0x100000000
#define MIST_MAX_BAG_ALLOC   (size_t)0x100000000

// This should be the hardware page size; future revisions
// will query the host os for this information
#define MIST_FRAME_SIZE       0x1000

// Macros to quickly align up or down on a given value for page allocation routines
#define UDIV_UP(A, B)        ((((size_t)A) + ((size_t)B) - 1ULL) / ((size_t)B))
#define ALIGN_UP(A, B)       (UDIV_UP((size_t)A, (size_t)B) * ((size_t)B))
#define ALIGN_DOWN(A, B)     (A - (A % B))

// Future potential for a rudimentary ASLR
#undef MIST_USE_RANDOM

// undef for POSIX
#define MIST_OS_WINDOWS

// Include the VirtualAlloc or mmap/munmap header as appropriate
#ifdef MIST_OS_WINDOWS
#include <windows.h>
#include <memoryapi.h>
#include <strsafe.h>

// For reporting Windows errors, courtesy Microsoft online example
void ErrorExit(LPTSTR lpszFunction) 
{ 
    // Retrieve the system error message for the last-error code

    LPVOID lpMsgBuf;
    LPVOID lpDisplayBuf;
    DWORD dw = GetLastError(); 

    FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | 
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        dw,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR) &lpMsgBuf,
        0, NULL );

    // Display the error message and exit the process

    lpDisplayBuf = (LPVOID)LocalAlloc(LMEM_ZEROINIT, 
        (lstrlen((LPCTSTR)lpMsgBuf) + lstrlen((LPCTSTR)lpszFunction) + 40) * sizeof(TCHAR)); 
    StringCchPrintf((LPTSTR)lpDisplayBuf, 
        LocalSize(lpDisplayBuf) / sizeof(TCHAR),
        TEXT("%s failed with error %d: %s"), 
        lpszFunction, dw, lpMsgBuf); 
    MessageBox(NULL, (LPCTSTR)lpDisplayBuf, TEXT("Error"), MB_OK); 

    LocalFree(lpMsgBuf);
    LocalFree(lpDisplayBuf);
    ExitProcess(dw); 
}

#else
#include <sys/mman.h>
#endif

// Pointer to Struct Init Function
bool (*struct_init_fn)(void* struct_init_struct);

// Bump Allocator
typedef struct {
  uint16_t type_alignment; // for < MIST_FRAME_SIZE types 
  uint16_t type_width; // for < MIST_FRAME_SIZE types
  size_t aligned_type_width; // Do it to it yo
  uint8_t* bump_ptr; // for bump allocation (current pointer)
} mist_allocator_t;

// Bag (frame collection)
typedef struct {
  uint8_t* base_page_ptr; // base pointer to frame 0
  size_t page_size; // pages can be MIST_FRAME_SIZE to a max of (pages per cacheline) pages as a multiple of MIST_FRAME_SIZE
  size_t max_frames; // maximum number of frames for this bag; if 0, then it defaults to MIST_MAX_BAG_ALLOC / MIST_FRAME_SIZE
  size_t allocated_frames; // how many pages have been allocated
  size_t bag_index; // the index within the bag_manager of this bag (this index' purpose is as a back referencing index)
  mist_allocator_t allocator; // All bags have a bump allocator, even if it's not used
} mist_bag_t;

// Bag Manager
typedef struct {
  mist_bag_t bags[MIST_MAX_BAGS]; // bags
  size_t current_bag; // what is the "current" bag (for allocating the next bag)
} mist_manager_t;

// Local Library Management Variables
mist_manager_t mist_manager; // manages the bags
int8_t* next_major_bag_alloc_loc = (int8_t*)MIST_START_FRAME; // where do we put the bags in the VAS

// Find the frame index given a bag index and a pointer
inline size_t MistCalculateIndex(size_t mist_bag_index, uint8_t* pointer);
inline size_t MistCalculateIndex(size_t mist_bag_index, uint8_t* pointer) {
  uint8_t* current_base_frame = (uint8_t*)ALIGN_DOWN((size_t)pointer, MIST_FRAME_SIZE);
  size_t xdiff = current_base_frame - mist_manager.bags[mist_bag_index].base_page_ptr;
  return xdiff / MIST_FRAME_SIZE;
}

// Zeros out the given frame -- should be noted that this can be very destructive
void MistZeroFrame(size_t mist_bag_index, size_t mist_frame_index);
void MistZeroFrame(size_t mist_bag_index, size_t mist_frame_index) {
  uint8_t* bp = mist_manager.bags[mist_bag_index].base_page_ptr + (mist_frame_index * MIST_FRAME_SIZE);
  
  for (size_t i = 0; i < MIST_FRAME_SIZE; i++) {
      *bp++ = (uint8_t)0;
  }
}

// Allocate the next frame on a given bag
void* MistAllocateFrame(size_t mist_bag_index);
void* MistAllocateFrame(size_t mist_bag_index) {
  // Make sure not exceeding a self-imposed page limit
  if (mist_manager.bags[mist_bag_index].allocated_frames == mist_manager.bags[mist_bag_index].max_frames)
    return NULL;

  // Find the base pointer
  uint8_t* bp = (uint8_t*)mist_manager.bags[mist_bag_index].base_page_ptr;

// Allocate a new frame at the end of the currently allocated frameset
#ifdef MIST_OS_WINDOWS
  VirtualAlloc((LPVOID)((bp + 
    ((mist_manager.bags[mist_bag_index].allocated_frames) * MIST_FRAME_SIZE))), 
    MIST_FRAME_SIZE, 0, 0);
#else
  mmap((void*)((bp + 
    ((mist_manager.bags[mist_bag_index].allocated_frames) * MIST_FRAME_SIZE))), 
    MIST_FRAME_SIZE, NULL, NULL, NULL, 0);
#endif
  MistZeroFrame(mist_bag_index, mist_manager.bags[mist_bag_index].allocated_frames);
  mist_manager.bags[mist_bag_index].allocated_frames += 1;
  

  // Return a pointer to the first size_t in the newly allocated frame
  return (bp + 
    ((mist_manager.bags[mist_bag_index].allocated_frames - 1) * MIST_FRAME_SIZE));
}

// Allocate a specifiec frame on a given bag - buyer beware
// does no tracking; does not increment allocated count -- 
// should really only use this to grow pages down from the end of the VAS
bool MistAllocateSpecificFrame(size_t mist_bag_index, size_t mist_frame_index);
bool MistAllocateSpecificFrame(size_t mist_bag_index, size_t mist_frame_index) {
  // Make sure not exceeding a self-imposed page limit
  if (mist_manager.bags[mist_bag_index].allocated_frames == mist_manager.bags[mist_bag_index].max_frames)
    return false;

  // Find the base Pointer
  uint8_t* bp = mist_manager.bags[mist_bag_index].base_page_ptr;

// Allocate the specified frame, but do not increment allocation counters
#ifdef MIST_OS_WINDOWS
  VirtualAlloc((LPVOID)((bp + 
    (mist_frame_index * MIST_FRAME_SIZE))), 
    MIST_FRAME_SIZE, 0, 0);
#else
  mmap((void*)((bp + 
    (mist_frame_index * MIST_FRAME_SIZE))), 
    MIST_FRAME_SIZE, NULL, NULL, NULL, 0);
#endif
  MistZeroFrame(mist_bag_index, mist_frame_index);

  // Let everyone know we succeeded
  return true;
}

// Init the MIST manager
bool MistInit();
bool MistInit() {
  // Ensure no double init
  static bool init = false;
  if (init) return false;

  // Configure the base bag (0) with params for bookeeping and allocate a single frame
  mist_manager.current_bag = MIST_BASE_BAG;
  mist_manager.bags[mist_manager.current_bag].page_size = MIST_FRAME_SIZE;
  mist_manager.bags[mist_manager.current_bag].max_frames = (size_t)(MIST_MAX_BAG_ALLOC / MIST_FRAME_SIZE);
  
  // VirtualAlloc on Windows, mmap() on Linux / POSIX
#ifdef MIST_OS_WINDOWS
  mist_manager.bags[mist_manager.current_bag].base_page_ptr = (uint8_t*)VirtualAlloc((LPVOID)next_major_bag_alloc_loc, MIST_FRAME_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

  // Make sure we actually received VAS
  if (mist_manager.bags[mist_manager.current_bag].base_page_ptr == NULL) {
    ErrorExit(TEXT("VirtualAlloc"));
  }
#else
  mist_manager.bags[mist_manager.current_bag].base_page_ptr = (uint8_t*)mmap((void*)next_major_bag_alloc_loc, MIST_FRAME_SIZE, NULL, NULL, NULL, 0);
#endif
  mist_manager.bags[mist_manager.current_bag].allocated_frames = 1;
  
  // Set the location of the next major bag allocation in the VMA
  next_major_bag_alloc_loc += MIST_MAX_BAG_ALLOC;
  
  // We are now initialized; Elvii has left the building...
  init = true;
  return true;
}

// Allocate the next bag
size_t MistAllocateBag(size_t max_pages, uint16_t type_align, uint16_t type_width);
size_t MistAllocateBag(size_t max_pages, uint16_t type_align, uint16_t type_width) {
  // Make sure not last
  if ((mist_manager.current_bag + 1) > MIST_MAX_BAGS) return 0;
  
  // Safe to increment the index
  mist_manager.current_bag++;

  // Defaults
  if (max_pages == 0) max_pages = (MIST_MAX_BAG_ALLOC / MIST_FRAME_SIZE);
  
  // Set up this bag and its first frame
  mist_manager.bags[mist_manager.current_bag].page_size = MIST_FRAME_SIZE;
  mist_manager.bags[mist_manager.current_bag].max_frames = (size_t)max_pages;
#ifdef MIST_OS_WINDOWS
  mist_manager.bags[mist_manager.current_bag].base_page_ptr = (uint8_t*)VirtualAlloc((LPVOID)next_major_bag_alloc_loc, MIST_FRAME_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
  mist_manager.bags[mist_manager.current_bag].base_page_ptr = (uint8_t*)mmap((void*)next_major_bag_alloc_loc, MIST_FRAME_SIZE, NULL, NULL, NULL, 0);
  
#endif
  mist_manager.bags[mist_manager.current_bag].allocated_frames = 1;
  mist_manager.bags[mist_manager.current_bag].bag_index = mist_manager.current_bag;

  // Allocator properties
  mist_manager.bags[mist_manager.current_bag].allocator.bump_ptr = (uint8_t*)next_major_bag_alloc_loc;
  mist_manager.bags[mist_manager.current_bag].allocator.type_alignment = type_align;
  mist_manager.bags[mist_manager.current_bag].allocator.type_width = type_width;
  mist_manager.bags[mist_manager.current_bag].allocator.aligned_type_width = ALIGN_UP(type_width, type_align);

  // Pre-set where the next major allocation location
  // will occur within the VMA
  next_major_bag_alloc_loc += MIST_MAX_BAG_ALLOC;

  // Which index is this bag?
  return mist_manager.current_bag;
}

// Bump Allocate a new size on a given bag
void* MistNew(size_t mist_bag_index, size_t allocation_size);
void* MistNew(size_t mist_bag_index, size_t allocation_size) {
  if (allocation_size != 0 && mist_manager.bags[mist_bag_index].allocator.aligned_type_width > 0) return NULL;
  if (allocation_size == 0) allocation_size = mist_manager.bags[mist_bag_index].allocator.aligned_type_width;
  
  uint8_t* next_bump_loc = mist_manager.bags[mist_bag_index].allocator.bump_ptr + allocation_size;
  size_t next_bump_index = MistCalculateIndex(mist_bag_index, next_bump_loc);
  size_t current_bump_index = MistCalculateIndex(mist_bag_index, mist_manager.bags[mist_bag_index].allocator.bump_ptr);
  void* retptr = NULL;

  // See where we fall with this next bump, w.r.t. frame boundaries
  // We are in the same frame after the allocation, so no new frames
  if (next_bump_index == current_bump_index) {
    retptr = mist_manager.bags[mist_bag_index].allocator.bump_ptr;
    mist_manager.bags[mist_bag_index].allocator.bump_ptr = next_bump_loc;    
    return retptr;
  }
  
  // We are more than 1 frame ahead of the current frame, so allocate as 
  // appropriate
  retptr = MistAllocateFrame(mist_bag_index);
  if ((next_bump_index - current_bump_index) > 1) {
    for (size_t z = 0; z < (next_bump_index - current_bump_index - 1); z++) {
        MistAllocateFrame(mist_bag_index);
    }  
  }

  return retptr;
}

bool MistApplyInitFn(size_t mist_bag_index, void* start_ptr, size_t count, bool (*struct_init_fn)(void* struct_init_struct));

// De-Allocates the Last Frame in the given bag
bool MistDeAllocateLastFrame(size_t mist_bag_index);
bool MistDeAllocateLastFrame(size_t mist_bag_index) {
  // Make sure this is not the last (first) frame;
  // If it is, we can safely DeAllocate
  if (mist_manager.bags[mist_bag_index].allocated_frames == 1) return false;

  // Base pointer
  uint8_t* bp = mist_manager.bags[mist_bag_index].base_page_ptr;

// Call VirtualAlloc / munmap with the necessary flags to unmap the last page mapped
#ifdef MIST_OS_WINDOWS
  VirtualAlloc((LPVOID)((bp + 
    ((mist_manager.bags[mist_bag_index].allocated_frames - 1) * MIST_FRAME_SIZE))), 
    MIST_FRAME_SIZE, MEM_RESET, 0);
    MistZeroFrame(mist_bag_index, mist_manager.bags[mist_bag_index].allocated_frames - 1);
#else
  munmap((void*)((bp + 
    ((mist_manager.bags[mist_bag_index].allocated_frames - 1) * MIST_FRAME_SIZE))),
    MIST_FRAME_SIZE);
#endif
  // Update our count and return true
  mist_manager.bags[mist_bag_index].allocated_frames -= 1;
  return true;
}

// Allocate a specifiec frame on a given bag - buyer beware
// does no tracking; does not increment allocated count -- 
// should really only use this to grow pages down from the end of the VAS
bool MistDeAllocateSpecificFrame(size_t mist_bag_index, size_t mist_frame_index);
bool MistDeAllocateSpecificFrame(size_t mist_bag_index, size_t mist_frame_index) {
  // Sanity
  if (mist_bag_index == 0 || mist_frame_index == 0) return false;

  // Make sure not exceeding a self-imposed page limit
  if (mist_manager.bags[mist_bag_index].allocated_frames == 1)
    return false;

  // Find the base Pointer
  uint8_t* bp = mist_manager.bags[mist_bag_index].base_page_ptr;

// Allocate the specified frame, but do not increment allocation counters
#ifdef MIST_OS_WINDOWS
  VirtualAlloc((LPVOID)((bp + 
    (mist_frame_index * MIST_FRAME_SIZE))), 
    MIST_FRAME_SIZE, MEM_RESET, 0);
#else
  munmap((void*)((bp + 
    (mist_frame_index * MIST_FRAME_SIZE))), 
    MIST_FRAME_SIZE);
#endif
  // Let everyone know we succeeded
  return true;
}

#endif
