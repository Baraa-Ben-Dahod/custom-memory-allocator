#include <unistd.h>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <sys/mman.h>
#include <algorithm>

struct MallocMetadata;
static constexpr size_t MAX_ORDER = 10;
MallocMetadata* free_list[MAX_ORDER + 1] = {NULL};
static bool is_initialized = false;
static size_t num_allocated_blocks = 0;  // Number of all allocated blocks including mmaped blocks and used/free blocks
static size_t num_allocated_bytes = 0;   // Including payload only


struct MallocMetadata {
    size_t size;  // Total size of the allocated block (including meta-data).
    bool is_free; // Indicates if the block is used or no
    bool is_mmaped;
    MallocMetadata* next;
    MallocMetadata* prev;
};

//=========================================Auxiliary functions==========================================================
static int initialize_buddy_system();
static int get_order(size_t size);
static void* split_and_get_block(int found_order, int target_order);
static void add_to_free_list(int order, MallocMetadata* block);
static void remove_from_free_list(MallocMetadata* block);


//=========================================Main Allocation Functions====================================================


/**
 * @brief Tries to allocate 'size' bytes.
 * * Uses the Buddy System algorithm for allocations up to 128KB.
 * For allocations larger than 128KB, it uses mmap() to allocate memory directly from the OS.
 * * @param size The size of the memory block to allocate (in bytes).
 * @return void* Pointer to the first allocated byte (excluding meta-data), or NULL on failure.
 * * Failure cases:
 * - size is 0.
 * - size is > 10^8.
 * - sbrk() fails (for Buddy System).
 * - mmap() fails (for large allocations).
 */


void* smalloc(size_t size) {
    if (size == 0 || size > 100000000) {
        return NULL;
    }


    // size with more than 128 KB
    size_t eff_size = size + sizeof(MallocMetadata);
    if (eff_size > 128 * 1024) {
        void* ptr = mmap(NULL, eff_size, PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

        if (ptr == MAP_FAILED) {
            return NULL;
        }

        MallocMetadata* meta = static_cast<MallocMetadata*>(ptr);
        meta->size = eff_size;
        meta->is_free = false;
        meta->is_mmaped = true;
        num_allocated_blocks++;
        num_allocated_bytes += size;
        return (meta + 1);
    }

    // Initializing
    if (!is_initialized) {
        if (initialize_buddy_system() == -1) {
            return NULL;
        }
        is_initialized = true; // Must not change later
    }

    // Determine smallest available block
    const int ord = get_order(size + sizeof(MallocMetadata));

    // Available free block of required size
    if (free_list[ord] != NULL) {
        MallocMetadata* head = free_list[ord];
        head->is_free = false;
        head->prev = NULL;

        auto new_head = head->next;
        if (new_head != NULL) {
            new_head->prev = NULL;
        }

        head->next = NULL;
        free_list[ord] = new_head;

        return static_cast<void*>(head + 1);
    }

    // Else
    int i;
    for (i = ord + 1; (i <= MAX_ORDER) && (free_list[i] == NULL); i++) {}

    // There is no block available
    if (i > MAX_ORDER) {
        return NULL;
    }


    return split_and_get_block(i, ord);
}

/**
 * @brief Allocates memory for an array of 'num' elements of 'size' bytes each and initializes them to zero.
 * * @param num Number of elements.
 * @param size Size of each element.
 * @return void* Pointer to the allocated and zeroed memory, or NULL on failure.
 * * Failure cases:
 * - size is 0 or num is 0.
 * - size * num > 10^8.
 * - Underlying smalloc() fails.
 */

void* scalloc(size_t num, size_t size) {
    if (size == 0 || num == 0) {
        return NULL;
    }

    // Overflow protection logic
    if (num > 100000000 / size) {
        return NULL;
    }

    size_t total = size * num;  // Safe now, we've verified no overflow
    void* ptr = smalloc(total);

    if (ptr == NULL) {
        return NULL;
    }

    // Zero the block
    std::memset(ptr, 0, total);

    return ptr;
}


/**
 * @brief Releases the memory block pointed to by 'p'.
 * * If the block was allocated via mmap (large allocation), it is released using munmap().
 * If the block is part of the Buddy System:
 * - Marks the block as free.
 * - Iteratively merges the block with its "buddy" if the buddy is also free,
 * combining them into larger blocks up to MAX_ORDER.
 * * @param p Pointer to the beginning of the allocated block. If NULL, the function does nothing.
 */

void sfree(void* p) {
    if (p == NULL) {
        return;
    }

    auto metadata = static_cast<MallocMetadata*>(p) - 1;
    if (metadata->is_free) {
        return;
    }

    if (metadata->is_mmaped) {
        num_allocated_blocks--;
        num_allocated_bytes -= (metadata->size - sizeof(MallocMetadata));
        munmap(metadata, metadata->size);  // Must not fail
        return;
    }

    metadata->is_free = true;
    size_t max_size = 128 * 1024;
    while (metadata->size < max_size) {
        uintptr_t buddy_addr = reinterpret_cast<uintptr_t>(metadata) ^ metadata->size;
        MallocMetadata* buddy = reinterpret_cast<MallocMetadata*>(buddy_addr);

        if (!buddy->is_free || buddy->size != metadata->size || buddy->is_mmaped) {
            break;
        }

        remove_from_free_list(buddy);
        num_allocated_blocks--;
        num_allocated_bytes += sizeof(MallocMetadata);
        if (reinterpret_cast<uintptr_t>(buddy) < reinterpret_cast<uintptr_t>(metadata)) {
            metadata = buddy;
        }

        metadata->size *= 2;
    }


    add_to_free_list(get_order(metadata->size), metadata);
}

/**
 * @brief Reallocates the given memory block to a new size.
 * * Logic:
 * 1. If 'size' is 0, frees 'oldp' and returns NULL.
 * 2. If 'oldp' is NULL, allocates a new block of 'size'.
 * 3. If 'oldp' is a mmap'ed block:
 * - If new size is same, return oldp.
 * - Otherwise, allocate new mmap block, copy data, and free oldp.
 * 4. If 'oldp' is a Buddy System block:
 * - Tries to reuse the current block if it fits.
 * - Tries to merge with adjacent free buddies to satisfy the request in-place.
 * - If reuse/merge is not possible, allocates a new block, copies data, and frees oldp.
 * * @param oldp Pointer to the previously allocated memory.
 * @param size The new requested size in bytes.
 * @return void* Pointer to the new memory block, or NULL on failure.
 */

void* srealloc(void* oldp, size_t size) {
    if (size == 0 || size > 100000000) {
        return NULL;
    }
    if (oldp == NULL) {
        return smalloc(size);
    }

    auto metadata = static_cast<MallocMetadata*>(oldp) - 1;
    size_t target_size = size + sizeof(MallocMetadata);

    if (metadata->is_mmaped) {
        if (metadata->size == target_size) return oldp;
        void* newp = smalloc(size);
        if (!newp) return NULL;
        size_t copy_size = std::min(size, metadata->size - sizeof(MallocMetadata));
        std::memcpy(newp, oldp, copy_size);
        sfree(oldp);
        return newp;
    }

    if (target_size <= metadata->size) {
        return oldp;
    }

    size_t simulated_size = metadata->size;
    MallocMetadata* simulated_meta = metadata;
    bool can_merge = false;

    while (simulated_size < target_size && simulated_size < 128 * 1024) {
        uintptr_t buddy_addr = reinterpret_cast<uintptr_t>(simulated_meta) ^ simulated_size;
        MallocMetadata* buddy = reinterpret_cast<MallocMetadata*>(buddy_addr);

        if (!buddy->is_free || buddy->size != simulated_size) {
            break;
        }

        if (reinterpret_cast<uintptr_t>(buddy) < reinterpret_cast<uintptr_t>(simulated_meta)) {
            simulated_meta = buddy;
        }
        simulated_size *= 2;

        if (simulated_size >= target_size) {
            can_merge = true;
            break;
        }
    }

    if (can_merge) {
        size_t current_size = metadata->size;
        MallocMetadata* current_meta = metadata;

        while (current_size < simulated_size) {
            uintptr_t buddy_addr = reinterpret_cast<uintptr_t>(current_meta) ^ current_size;
            MallocMetadata* buddy = reinterpret_cast<MallocMetadata*>(buddy_addr);

            remove_from_free_list(buddy);
            num_allocated_blocks--;
            num_allocated_bytes += sizeof(MallocMetadata);

            if (reinterpret_cast<uintptr_t>(buddy) < reinterpret_cast<uintptr_t>(current_meta)) {
                std::memmove(static_cast<void*>(buddy + 1),static_cast<void*>(current_meta + 1),
                    current_meta->size - sizeof(MallocMetadata));
                MallocMetadata old_data = *current_meta;
                current_meta = buddy;
                *current_meta = old_data;
            }
            current_size *= 2;
            current_meta->size = current_size;
        }
        current_meta->is_free = false;
        return (current_meta + 1);
    }

    void* newp = smalloc(size);
    if (!newp) return NULL;

    std::memcpy(newp, oldp, (metadata->size - sizeof(MallocMetadata)));
    sfree(oldp);

    return newp;
}


//============================================= Stats methods ==========================================================

/**
 * @brief Returns the number of allocated blocks in the heap that are currently free.
 * * Iterates through the free_list array (orders 0 to MAX_ORDER) to count free blocks.
 * Note: mmap'ed blocks are never "free" (they are unmapped immediately), so they are not counted here.
 * * @return size_t Count of free blocks.
 */

size_t _num_free_blocks() {
    size_t count = 0;
    for (int i = 0; i <= MAX_ORDER; i++) {
        MallocMetadata* curr = free_list[i];
        while (curr != NULL) {
            count ++;
            curr = curr->next;
        }
    }
    return count;
}


/**
 * @brief Returns the number of bytes in all currently free blocks, excluding meta-data.
 * * Iterates through the free_list and sums up the payload size of each free block.
 * * @return size_t Total number of free bytes available for user.
 */

size_t _num_free_bytes() {
    size_t count = 0;
    for (int i = 0; i <= MAX_ORDER; i++) {
        MallocMetadata* curr = free_list[i];
        while (curr != NULL) {
            count += (curr->size - sizeof(MallocMetadata));
            curr = curr->next;
        }
    }
    return count;
}


/**
 * @brief Returns the overall (free + used) number of allocated blocks in the system.
 * * This count includes:
 * - All blocks currently managed by the Buddy System (free or used).
 * - All currently active mmap'ed blocks.
 * * @return size_t Total number of allocated blocks.
 */

size_t _num_allocated_blocks() {
    return num_allocated_blocks;
}

/**
 * @brief Returns the overall number of allocated bytes (free + used), excluding meta-data.
 * * This sum includes:
 * - Payload bytes of all Buddy System blocks.
 * - Payload bytes of all active mmap'ed blocks.
 * * @return size_t Total allocated bytes.
 */

size_t _num_allocated_bytes() {
    return num_allocated_bytes;
}


/**
 * @brief Returns the overall number of meta-data bytes currently in the system.
 * * calculated as: _num_allocated_blocks() * sizeof(MallocMetadata).
 * * @return size_t Total bytes used by meta-data structures.
 */

size_t _num_meta_data_bytes() {
    return num_allocated_blocks * sizeof(MallocMetadata);
}

/**
 * @brief Returns the size of a single meta-data structure.
 * * @return size_t Size of MallocMetadata in bytes.
 */

size_t _size_meta_data() {
    return sizeof(MallocMetadata);
}




//======================================================================================================================
/**
 * @brief Initializes the Buddy System heap with 32 blocks of 128KB.
 * * Uses sbrk() to allocate 4MB of memory.
 * Performs alignment adjustment to ensure the memory starts at an address
 * suitable for XOR buddy calculation.
 * * @return int 0 on success, -1 on sbrk failure.
 */
int initialize_buddy_system() {
    size_t block_size = 128 * 1024;
    size_t total_size = 32 * block_size;

    // Alignment memory
    uintptr_t current_brk = reinterpret_cast<uintptr_t>(sbrk(0));
    size_t alignment_needed = (total_size - (current_brk % total_size)) % total_size;

    void* ptr = sbrk(total_size + alignment_needed);
    if (ptr == reinterpret_cast<void *>(-1)) {
        return -1;
    }

    // Create 32 blocks, each one of size 128KB including metadata
    MallocMetadata* first_block = reinterpret_cast<MallocMetadata*>(static_cast<char*>(ptr) + alignment_needed);
    free_list[MAX_ORDER] = first_block;

    MallocMetadata* curr = first_block;
    for (int i = 0; i < 32; i++) {
        curr->size = block_size;
        curr->is_free = true;
        curr->is_mmaped = false;

        if (i < 31) {
            MallocMetadata* next_block = reinterpret_cast<MallocMetadata*>(reinterpret_cast<char*>(curr) + block_size);
            curr->next = next_block;
            next_block->prev = curr;
            curr = next_block;
        } else {
            curr->next =  NULL;
        }
    }

    first_block->prev = NULL;
    num_allocated_blocks += 32;
    num_allocated_bytes += 32 * (128 * 1024 - sizeof(MallocMetadata));
    return 0;
}

/**
 * @brief Calculates the Buddy System order required to hold 'size' bytes.
 * * @param size The total size (User Data + Metadata).
 * @return int The smallest order 'i' such that (128 * 2^i) >= size.
 */
int get_order(const size_t size) {
    int i = 0;
    while (i < MAX_ORDER && (static_cast<size_t>(128) << i) < size) {
        i++;
    }

    return i;
}

/**
 * @brief Splits a larger block into smaller buddies until the target order is reached.
 * * @param found_order The order of the available free block.
 * @param target_order The desired order for the allocation.
 * @return void* Pointer to the user data of the resulting block.
 */
void* split_and_get_block(int found_order, int target_order) {
    MallocMetadata* block = free_list[found_order];

    // Remove the block from the list
    free_list[found_order] = block->next;
    if (free_list[found_order]) {
        free_list[found_order]->prev = NULL;
    }

    // Iterative splitting
    while (found_order > target_order) {
        found_order--;
        block->size /= 2;
        MallocMetadata* buddy = reinterpret_cast<MallocMetadata*>(reinterpret_cast<char*>(block) + block->size);
        buddy->is_mmaped = false;
        num_allocated_blocks++;
        num_allocated_bytes -= sizeof(MallocMetadata);
        add_to_free_list(found_order, buddy);
    }

    block->is_free = false;
    block->next = NULL;
    block->prev = NULL;

    return static_cast<void*>(block + 1);
}

/**
 * @brief Adds a block to the free list of a specific order.
 * * Inserts the block into the doubly linked list, maintaining ascending address order.
 * * @param order The order of the block.
 * @param block Pointer to the block's metadata.
 */
void add_to_free_list(int order, MallocMetadata* block) {
    if (!block) {
        return;
    }

    block->is_free = true;
    block->size = (128 << order);
    MallocMetadata* curr = free_list[order];

    // Case 1: add to the head of the list
    if (curr == NULL || block < curr) {
        block->next = curr;
        block->prev = NULL;
        if (curr != NULL) {
            curr->prev = block;
        }
        free_list[order] = block;
        return;
    }

    // Find correct place to insert to maintain ascending sorted list
    while (curr->next != NULL && curr->next < block) {
        curr = curr->next;
    }

    block->next = curr->next;
    block->prev = curr;
    if (curr->next != NULL) {
        curr->next->prev = block;
    }
    curr->next = block;
}

/**
 * @brief Removes a specific block from the free list.
 * * Handles unlinking from the doubly linked list and updating the list head if necessary.
 * * @param block Pointer to the block's metadata to remove.
 */
void remove_from_free_list(MallocMetadata* block) {
    if (!block) {
        return;
    }

    int order = get_order(block->size);
    if (free_list[order] == block) {
        free_list[order] = block->next;
    }

    if (block->prev != NULL) {
        block->prev->next = block->next;
    }

    if (block->next != NULL) {
        block->next->prev = block->prev;
    }

    block->next = NULL;
    block->prev = NULL;
}