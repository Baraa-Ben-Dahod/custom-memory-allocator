# Custom Dynamic Memory Allocator

A high-performance C++ dynamic memory management library implementing custom dynamic memory allocation routines (`smalloc`, `scalloc`, `sfree`, `srealloc`) and diagnostic heap telemetry. Developed for the Operating Systems course (234123) at the Technion - Israel Institute of Technology.

The library replaces standard C/C++ runtime heap allocators (`malloc`, `free`, `calloc`, `realloc`, `new`, `delete`) across three architectural iterations: a linear program-break expansion model (`malloc_1.cpp`), an in-band metadata header model with first-fit block reuse (`malloc_2.cpp`), and a dual-engine Buddy System allocator combined with direct kernel page mapping via POSIX `mmap()`/`munmap()` (`malloc_3.cpp`).

---

## Architecture Overview

The system provides a complete user-space memory allocator that balances internal and external fragmentation against allocation latency through progressive architectural stages:

```
+-----------------------------------------------------------------------------------+
|                                  USER APPLICATION                                 |
+-----------------------------------------------------------------------------------+
|         smalloc()  |  scalloc()  |  sfree()  |  srealloc()  |  Telemetry API      |
+-----------------------------------------------------------------------------------+
                                         |
                       Allocation Size Threshold Check
                                         |
                  +----------------------+----------------------+
                  |                                             |
           Size <= 128 KB                                Size > 128 KB
                  |                                             |
                  v                                             v
     +-------------------------+                   +-------------------------+
     |   BUDDY SYSTEM ENGINE   |                   |  KERNEL PAGE MAP ENGINE |
     +-------------------------+                   +-------------------------+
     | Free Lists [Order 0..10]|                   | POSIX mmap() / munmap() |
     | 128B to 128KB Blocks    |                   | Direct Anonymous Pages  |
     | Bitwise XOR Coalescing  |                   +-------------------------+
     +-------------------------+
                  |
     4MB Pre-allocated Heap
     Aligned Boundary (sbrk)
```

### Allocation Engines

1. **Naïve Linear Heap Allocator (`malloc_1.cpp`)**:
   - Expands the process program break via `sbrk()` for every allocation request.
   - Enforces basic boundary checks (0-byte requests and allocations exceeding $10^8$ bytes are rejected).

2. **First-Fit Block Recycling Allocator (`malloc_2.cpp`)**:
   - Embeds an in-band `MallocMetadata` structure directly preceding each memory payload.
   - Maintains a global doubly-linked list (`block_head`) to search for and reuse freed blocks satisfying payload constraints before extending the heap via `sbrk()`.

3. **Dual-Engine Buddy Allocator & Page Mapper (`malloc_3.cpp`)**:
   - **Buddy Allocator ($\le 128	ext{KB}$)**: Manages a pre-allocated 4MB virtual memory pool partitioned into $2^k$ blocks across orders 0 ($128	ext{B}$) through 10 ($128	ext{KB}$). Uses address-sorted free lists to enforce tightest-fit allocation and $O(1)$ bitwise XOR buddy address calculation for block merging.
   - **Direct Kernel Page Mapping ($> 128	ext{KB}$)**: Bypasses user-space heap structures for large allocations, mapping isolated anonymous memory pages directly via `mmap()` (`MAP_PRIVATE | MAP_ANONYMOUS`) and unmapping them immediately upon release via `munmap()`.

---

## Core Features (Technical)

* **Dual Engine Size-Based Routing**:
  - Automatically routes allocation requests $\le 128	ext{KB}$ (including metadata) to the Buddy System free lists.
  - Routes large allocation requests $> 128	ext{KB}$ directly to the kernel page mapper via `mmap(NULL, eff_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)`.

* **Address-Sorted Free Lists**:
  - Maintains `free_list[11]`, an array of doubly-linked lists corresponding to block orders 0 through 10.
  - Enforces strict ascending virtual memory address ordering within each list, guaranteeing deterministic tightest-fit block selection and minimal external fragmentation.

* **Bitwise XOR Buddy Coalescing**:
  - Eliminates list searching during deallocation by computing a block's buddy address in $O(1)$ time:
    $$	ext{Buddy Address} = 	ext{Block Address} \oplus 	ext{Block Size}$$
  - Iteratively unlinks and merges adjacent free buddies into higher-order blocks up to $128	ext{KB}$ (`MAX_ORDER = 10`).

* **In-Place Reallocation Optimization**:
  - `srealloc` evaluates current block capacity before allocating new memory.
  - Simulates bitwise buddy coalescing for adjacent free blocks; if sufficient contiguous capacity can be formed, expands the block in-place and executes necessary memmove operations without incurring external allocation overhead.

* **Integer Overflow & Boundary Safety**:
  - `scalloc` protects against arithmetic wrap-around by validating `num > 100000000 / size` prior to computing total byte requirements.
  - Rejects allocations requesting 0 bytes or exceeding $10^8$ bytes ($100	ext{MB}$) across all entry points.

* **Full Heap Telemetry & Diagnostics**:
  - Provides runtime visibility into heap structures without accounting for system metadata overheads:
    - `_num_free_blocks()`: Count of unallocated Buddy System blocks.
    - `_num_free_bytes()`: Total payload bytes available across free blocks.
    - `_num_allocated_blocks()`: Combined count of active Buddy blocks and `mmap` allocations.
    - `_num_allocated_bytes()`: Sum of allocated payload bytes across all active blocks.
    - `_num_meta_data_bytes()`: Cumulative byte footprint of all active metadata headers.
    - `_size_meta_data()`: Size of a single metadata header structure (`sizeof(MallocMetadata)`).

---

## Technical Deep-Dive: Buddy System & Memory Alignment

### 1. Initial Heap Pre-Allocation & Alignment Guarantee

Upon the first allocation call within `malloc_3.cpp`, the allocator pre-reserves 4MB of heap memory consisting of 32 contiguous $128	ext{KB}$ blocks (`MAX_ORDER = 10`).

To ensure that bitwise XOR buddy calculations are mathematically valid, the starting address of the initial 4MB heap block must be aligned to a $32 	imes 128	ext{KB}$ ($4,194,304	ext{ bytes}$) boundary. The allocator achieves this via `sbrk(0)` address offset calculation prior to the primary allocation:

```cpp
size_t block_size = 128 * 1024;
size_t total_size = 32 * block_size; // 4MB

uintptr_t current_brk = reinterpret_cast<uintptr_t>(sbrk(0));
size_t alignment_needed = (total_size - (current_brk % total_size)) % total_size;

void* ptr = sbrk(total_size + alignment_needed);
MallocMetadata* first_block = reinterpret_cast<MallocMetadata*>(
    static_cast<char*>(ptr) + alignment_needed
);
```

### 2. Iterative Block Splitting (`split_and_get_block`)

When an allocation request requires an order $k$, but `free_list[k]` is empty, the allocator searches higher orders $i > k$ for the smallest available block. Upon finding a block at order $i$, it iteratively bisects the block until reaching order $k$:

```cpp
while (found_order > target_order) {
    found_order--;
    block->size /= 2;
    
    // Calculate address of generated buddy block
    MallocMetadata* buddy = reinterpret_cast<MallocMetadata*>(
        reinterpret_cast<char*>(block) + block->size
    );
    buddy->is_mmaped = false;
    
    // Insert buddy into corresponding order free list
    add_to_free_list(found_order, buddy);
}
```

### 3. Deallocation & Bitwise Coalescing (`sfree`)

During `sfree(void* p)`, if the metadata header indicates `is_mmaped == true`, the block is released immediately via `munmap()`. Otherwise, the allocator executes bitwise coalescing:

```cpp
uintptr_t buddy_addr = reinterpret_cast<uintptr_t>(metadata) ^ metadata->size;
MallocMetadata* buddy = reinterpret_cast<MallocMetadata*>(buddy_addr);

if (buddy->is_free && buddy->size == metadata->size && !buddy->is_mmaped) {
    remove_from_free_list(buddy);
    if (reinterpret_cast<uintptr_t>(buddy) < reinterpret_cast<uintptr_t>(metadata)) {
        metadata = buddy;
    }
    metadata->size *= 2; // Merge into higher order block
}
```

---

## Repository File Structure

```
.
├── malloc_1.cpp        # Part 1: Naïve linear heap expansion via POSIX sbrk()
├── malloc_2.cpp        # Part 2: First-fit linked-list block reuse & stats API
├── malloc_3.cpp        # Part 3: Dual-engine Buddy Allocator & POSIX mmap/munmap
├── os_malloc.h         # Public interface declarations for smalloc family
├── test_malloc_1.cpp   # Validation suite for Part 1 implementation
├── test_malloc_2.cpp   # Validation suite for Part 2 block reuse and statistics
├── test_malloc_3.cpp   # Validation suite for Part 3 Buddy System and mmap routing
├── Makefile            # Build system, testing automation, and packaging tool
└── README.md           # Technical project documentation
```

---

## Build & Testing Setup

### Prerequisites

* **Operating System**: Ubuntu 18.04 LTS (Required for OS compatibility checks)
* **Compiler**: `g++` (supporting C++11 standard)
* **Build Tool**: GNU `make`

### Compilation & Test Execution

The provided `Makefile` automates OS checks, compilation, execution of diagnostic test harnesses, and submission packaging.

```bash
# Verify OS compatibility and run Part 1 tests
make test1

# Run Part 2 test suite (First-Fit block reuse and telemetry)
make test2

# Run Part 3 test suite (Buddy System partitioning and mmap page routing)
make test3

# Compile and execute test suites 1 through 3
make all

# Clean compiled binaries and test build artifacts
make clean
```

### Submission Packaging

To generate a standardized submission archive:

```bash
# 1. Create submitters.txt formatted as: <Name> <Email> <ID>
echo "Linus Torvalds linus@gmail.com 234567890" > submitters.txt
echo "Ken Thompson ken@belllabs.com 345678901" >> submitters.txt

# 2. Package source files and verify submission contents
make submit
```
