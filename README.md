# Dynamic Memory Allocator (Custom Heap & Page Manager) from Scratch

A comprehensive, mathematically rigorous implementation of a custom user-space dynamic memory allocator (**malloc**, **free**, **calloc**, **realloc**) in C++. Developed from scratch, the library implements multiple allocation paradigms, ranging from naive linear heap expansion to a highly efficient **Buddy Memory System** paired with kernel-level **Memory Mapping (mmap)** for large allocations.

This project was built as part of the systems programming curriculum for the **Operating Systems (02340123 / 234123)** course at the **Technion - Israel Institute of Technology**.

---

## 🌟 Key Features

*   **Naive Linear Allocation (Part 1):** Implements a simple linear dynamic heap expansion (`smalloc`) via direct manipulation of the program break using the `sbrk()` system call.
*   **Block Reuse with Metadata Headers (Part 2):** Employs explicit doubly-linked metadata structures (`MallocMetadata`) attached to each block. Supports:
    *   **First-Fit Reuse:** Searches the doubly-linked list in ascending address order to locate and reuse previously freed blocks, reducing unnecessary heap expansion.
    *   **Block Tracking:** Maintains statistics on allocated/free blocks and bytes in real-time.
*   **Buddy Memory Allocator (Part 3):** Combines planning and systems design to eliminate external fragmentation:
    *   **Power of 2 Partitioning:** Manages memory in blocks of sizes that are powers of 2, ranging from **Order 0 (128 bytes)** to **Order 10 (128 KB)**.
    *   **Address-Sorted Array of Free Lists:** Implements an array of doubly-linked lists where cell $i$ holds all free blocks of order $i$, sorted by memory address for $O(1)$ order access.
    *   **Iterative Splitting:** Cut large blocks in half (buddies) iteratively down to the smallest required order that accommodates the user's payload.
    *   **Address-Based Buddy Coalescing:** Merges adjacent free buddy blocks back into single larger partitions. Computes buddy addresses using bitwise XOR operations on block addresses.
*   **Large Allocations Routing (mmap):** Routes requests greater than 128 KB directly to the OS kernel via anonymous, private memory-mapped regions using `mmap()` and `munmap()`, preventing large-scale fragmentation.

---

## 📁 Repository Structure

To support the automated test runners and compilation scripts, the repository implements a clean **Flat Directory Structure** in the root folder:

```text
custom-memory-allocator/
├── malloc_1.cpp                # Naive linear malloc using direct sbrk()
├── malloc_2.cpp                # Basic block-reuse malloc with explicit metadata
├── malloc_3.cpp                # Advanced Buddy Memory Allocator with mmap routing
├── os_malloc.h                 # Central API header defining malloc family and stats functions
├── test_malloc_1.cpp           # Automated sanity suite for Part 1
├── test_malloc_2.cpp           # Automated sanity suite for Part 2
├── test_malloc_3.cpp           # Automated sanity suite for Part 3
├── Makefile                    # Multi-part compilation, testing, and submission packager
├── README_tests.txt            # Course assignment sanity testing documentation
├── .gitignore                  # Excludes compiler outputs and test binaries
└── README.md                   # Main project documentation
```

*Note: Please rename the starter files by removing the `-org` suffix (e.g., `test_malloc_3.cpp-org` $\rightarrow$ `test_malloc_3.cpp`, `Makefile-org` $\rightarrow$ `Makefile`) to align with this clean compilation model.*

---

## 🛠️ Technical Deep Dive & Memory Architecture

### 1. Basic Allocation & Doubly-Linked Metadata Headers (`malloc_2.cpp`)

To avoid the inability to free memory in naive implementations, every allocation is prefixed with an explicit `MallocMetadata` header:

```cpp
struct MallocMetadata {
    size_t size;         // User payload size (excluding metadata)
    bool is_free;        // Block utilization flag
    MallocMetadata* next; // Pointer to next block in heap
    MallocMetadata* prev; // Pointer to previous block in heap
};
```

When `smalloc(size)` is invoked:
1.  The allocator traverses the heap starting from `block_head`.
2.  If a free block with `size >= requested_size` is found, `is_free` is flipped to `false`, and the user is returned a pointer to the memory *after* the metadata header (`metadata_ptr + 1`).
3.  If no matching block is found, `sbrk()` is invoked to extend the program break by `requested_size + sizeof(MallocMetadata)`.

---

### 2. Advanced Buddy Allocator & Coalescing System (`malloc_3.cpp`)

At startup, the allocator pre-allocates a contiguous block of **4 MB (32 blocks of 128 KB)** by invoking `sbrk()` exactly once (making sure it is aligned to $32 \times 128 \text{ KB}$ boundaries). It maintains an array of doubly-linked lists:

$$\text{free\_list}[\text{MAX\_ORDER} + 1] = \{ \text{free\_list}[0], \text{free\_list}[1], \dots, \text{free\_list}[10] \}$$

Where cell $i$ contains free blocks of size $128 \times 2^i$ bytes (payload + metadata).

```text
               [ Order Array ]
Order 10 (128KB) -> [ Block 1 ] <-> [ Block 2 ] <-> NULL
Order 9  (64KB)  -> NULL
...
Order 0  (128B)  -> [ Block 3 ] <-> [ Block 4 ] <-> NULL
```

#### Splitting Algorithm
When an allocation of order $T$ is requested, and only a free block of a larger order $F > T$ is available:
1.  The block of order $F$ is popped from `free_list[F]`.
2.  The block size is halved: $\text{size} \leftarrow \text{size} / 2$.
3.  The second half (the buddy block) is marked free and pushed into `free_list[F-1]`.
4.  Step 2 and 3 are repeated until the block size matches the target order $T$.

#### Coalescing & XOR Buddy Calculations
Upon invoking `sfree(p)`, the block is marked free. To eliminate internal fragmentation, buddy blocks are iteratively merged if they are also free. 
Because block sizes are powers of 2 and aligned, the starting address of a block's buddy is calculated instantly via bitwise XOR:

$$\text{Buddy Address} = \text{Current Block Address} \oplus \text{Block Size}$$

```cpp
uintptr_t buddy_addr = reinterpret_cast<uintptr_t>(metadata) ^ metadata->size;
MallocMetadata* buddy = reinterpret_cast<MallocMetadata*>(buddy_addr);

if (buddy->is_free && buddy->size == metadata->size && !buddy->is_mmaped) {
    // Unlink buddy from its free list
    remove_from_free_list(buddy);
    
    // Determine which block starts first in physical memory
    if (buddy < metadata) {
        metadata = buddy; // Buddy is the left block
    }
    
    // Double the size of the combined block
    metadata->size *= 2;
    // Repeat iteratively...
}
```

---

### 3. Memory Mapping (`mmap`) Routing
For allocations where $\text{size} + \text{sizeof}(MallocMetadata) > 128\text{ KB}$, sbrk-based buddy lists are bypassed. The allocation is requested directly from the OS kernel using anonymous, private page mapping:

```cpp
void* ptr = mmap(NULL, size + sizeof(MallocMetadata), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
```

When `sfree()` is called on an mmap-allocated block, the memory mapped region is unmapped immediately using `munmap()`, releasing virtual memory pages back to the operating system in real-time.

---

## 🚀 Getting Started

### Compilation & Build
To compile the custom memory allocator along with the automated test binaries, execute the default rule in the root folder:
```bash
make all
```
This compiles the standalone test executables `test1`, `test2`, and `test3` using `g++`.

### Running Tests
To manually execute individual sanity test suites for each part:
```bash
# Test Naive Malloc
./test1

# Test Basic block-reuse and statistics
./test2

# Test Advanced Buddy Allocator, address splitting, coalescing, and mmap
./test3
```

You can also run testing shortcuts directly via the Makefile:
```bash
make test1
make test2
make test3
```

### Packaging Submission Archives
To package the project files into a flat `.zip` file for academic grading, run:
```bash
make submit
```
This script checks for OS compatibility (Ubuntu 18.04), verifies that all required source files are present, and generates the flat submission package.
