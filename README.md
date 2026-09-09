# Custom Dynamic Memory Allocator (sbrk-based Heap Expansion)

A lightweight C++ dynamic memory allocation library implementing naive linear heap expansion via the POSIX `sbrk()` system call. Built for the Operating Systems course (234123) at the Technion - Israel Institute of Technology.

---

## Architecture Overview

The allocator provides a user-space memory management routine (`smalloc`) that interacts directly with the kernel to adjust a process's data segment boundary (the program break). When a process requests memory, the system dynamically shifts the heap boundary upward to allocate contiguous physical memory pages.

```
+-------------------------------------------------------+
|                   Virtual Memory                      |
+-------------------------------------------------------+
|  Text Segment  |  Data / BSS  |  Heap (Grows Up)      |
+-------------------------------+-----------------------+
                                ^ Program Break
                                |
                   sbrk(size)   +---> Shifted Boundary
```

---

## Core Features (Technical)

* **Linear Dynamic Heap Expansion (`smalloc`)**: Adjusts process data segment boundaries using the POSIX `sbrk()` system call.
* **Strict Input Guard Checks**:
  * **Zero-Size Protection**: Rejects allocation requests with `size == 0`, returning `NULL`.
  * **Maximum Bound Enforcement**: Rejects requests exceeding $10^8$ bytes ($100,000,000$ bytes) to prevent heap exhaustion and overflow, returning `NULL`.
* **Kernel Error Handling**: Handles system call failures by checking for `(void*)(-1)` return codes from `sbrk()`, returning `NULL` safely to the application.

---

## Technical Deep-Dive: Program Break Management

### Allocation Control Flow (`smalloc`)

When `smalloc(size_t size)` is called, execution proceeds as follows:

1. **Parameter Validation**:
   ```cpp
   if (size == 0 || size > 100000000) {
       return NULL;
   }
   ```
   * `size_t` represents an unsigned integer.
   * Zero-byte requests return `NULL` to avoid unnecessary system calls.
   * Allocations over 100MB are blocked as an upper-bound safety constraint.

2. **System Call Execution**:
   ```cpp
   void* ptr = sbrk(size);
   ```
   * Invokes `sbrk(size)` to increment the current program break by `size` bytes.

3. **Failure Checking & Return**:
   ```cpp
   if (ptr == reinterpret_cast<void*>(-1)) {
       return NULL;
   }
   return ptr;
   ```
   * Returns a direct pointer to the start of the newly allocated memory region on success, or `NULL` if `sbrk()` fails.

---

## Course Context & Baseline Framework

This library was developed as Part 1 of the Technion CS 234123 Operating Systems curriculum. 

The starter framework provided by the course staff includes starter skeletons for multi-phase allocator progression:
* **Part 1 (User Implementation)**: Basic `sbrk()` linear allocation and boundary guards in `malloc_1.cpp`.
* **Part 2 (Staff Skeleton Baseline)**: Basic block reuse using doubly-linked list `MallocMetadata` header structures.
* **Part 3 (Staff Skeleton Baseline)**: Buddy System allocator utilizing $2^k$ power-of-two partitioning, XOR address coalescing, and `mmap`/`munmap` page routing for allocations $>128\text{KB}$.

---

## Repository Structure

```
.
├── malloc_1.cpp        # Naive linear malloc implementation using sbrk()
├── os_malloc.h         # Central API header for smalloc family
├── test_malloc_1.cpp   # Automated unit test suite for Part 1
├── Makefile            # Build system and submission package utility
└── README.md           # Project documentation
```

---

## Build & Testing

### Prerequisites

* Target OS: Ubuntu 18.04
* Compiler: `g++`

### Build Commands

To build and run the test suite using the provided Makefile:

```bash
# Compile and run Part 1 tests
make test1

# Compile and run all available test suites
make all

# Clean build binaries
make clean
```
