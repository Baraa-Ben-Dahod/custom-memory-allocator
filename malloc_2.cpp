#include <unistd.h>
#include <cstring>

struct MallocMetadata* block_head = NULL;


struct MallocMetadata {
    size_t size;  // Const value - Must not change after allocation
                  // <size> is the size of the allocated block (excluding meta-data).
    bool is_free; // Indicates if the block is used or no
    MallocMetadata* next;
    MallocMetadata* prev;
};


/*
 * Searches for a free block with at least 'size' bytes or allocates (sbrk()) one if none are found.
 * Return value:
 * Success – a pointer to the first allocated byte in the allocated block (excluding meta-data).
 * Failure –
 * If 'size' is 0 returns NULL.
 * If 'size' is more than 100,000,000 (10^8) return NULL.
 * If sbrk fails, return NULL.
*/


void* smalloc(size_t size) {
    if (size == 0 || size > 100000000) {
        return NULL;
    }

    // Search for a valid block
    MallocMetadata* curr_block = block_head;
    MallocMetadata* block_tail = curr_block;

    while (curr_block != NULL) {
        if (curr_block->is_free && curr_block->size >= size) {
            // Found valid block to allocate
            curr_block->is_free = false;
            return curr_block + 1;
        }

        block_tail = curr_block;
        curr_block = curr_block->next;
    }

    // Not found valid block - Must allocate one
    void* ptr = sbrk(size + sizeof(MallocMetadata));

    // sbrk() fails
    if (ptr == reinterpret_cast<void *>(-1)) {
        return NULL;
    }

    MallocMetadata* new_block = static_cast<MallocMetadata*>(ptr);
    new_block->size = size;
    new_block->is_free = false;
    new_block->next = NULL;
    new_block->prev = block_tail;

    // Link the new block to head or tail
    if (block_tail == NULL) {
        block_head = new_block;
    } else {
        block_tail->next = new_block;
    }

    return static_cast<void*>(new_block + 1);
}

/*
 * Searches for a free block of at least ‘num’ elements, each ‘size’ bytes that are all set to 0,
 * or allocates if none are found.
 * In other words, find/allocate size * num bytes and set all bytes to 0.
 * Return value:
 * Success – a pointer to the first allocated byte in the allocated block (excluding meta-data).
 * Failure –
 * If 'size' or 'num' is 0 returns NULL.
 * If 'size' * 'num' is more than 100,000,000 (10^8) return NULL.
 * If sbrk fails, return NULL.
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


/*
 * Releases the usage of the block that starts with the pointer 'p'.
 * If 'p' is NULL or already released, simply returns.
 * Presume that all pointers ‘p’ truly points to the beginning of an allocated block.
 * It is the user's responsibility to ensure that p points to an allocated block (using smalloc family).
*/

void sfree(void* p) {
    if (p == NULL) {
        return;
    }

    auto metadata = static_cast<MallocMetadata*>(p) - 1;
    if (metadata->is_free) {
        return;
    }
    metadata->is_free = true;
}

/*
 * If 'size' is smaller than or equal to the current block's size, reuses the same block.
 * Otherwise, finds/allocates 'size' bytes for a new space, copies content of oldp into the
 * new allocated space and frees the oldp.
 * Return value:
 * Success –
 * 1) a pointer to the first byte in the (newly) allocated space.
 * 2) If 'oldp' is NULL, allocates space for 'size' bytes and returns a pointer to it.
 * Failure –
 * If 'size' is 0 returns NULL.
 * If 'size' is more than 100,000,000 (10^8) return NULL.
 * If sbrk fails, return NULL.
 * Note: srealloc do not free 'oldp' if it fails.
*/

void* srealloc(void* oldp, size_t size) {
    if (size == 0 || size > 100000000) {
        return NULL;
    }

    if (oldp == NULL) {
        return smalloc(size);
    }

    auto old_metadata = static_cast<MallocMetadata*>(oldp) - 1;
    if (old_metadata->size >= size) {
        return oldp;
    }

    void* ptr = smalloc(size);
    if (ptr == NULL) {
        return NULL;
    }

    std::memmove(ptr, oldp, old_metadata->size);

    // Free oldp
    old_metadata->is_free = true;

    return ptr;
}


//============================================= Stats methods ==========================================================

/*
 * Returns the number of allocated blocks in the heap that are currently free.
*/

size_t _num_free_blocks() {
    auto curr_block = block_head;
    size_t count = 0;
    while (curr_block != NULL) {
        if (curr_block->is_free) {
            count++;
        }
        curr_block = curr_block->next;
    }

    return count;
}


/*
 * Returns the number of bytes in all allocated blocks in the heap that are currently free,
 * excluding the bytes used by the meta-data structs.
*/

size_t _num_free_bytes() {
    auto curr_block = block_head;
    size_t count = 0;
    while (curr_block != NULL) {
        if (curr_block->is_free) {
            count += curr_block->size;
        }
        curr_block = curr_block->next;
    }

    return count;
}


/*
 * Returns the overall (free and used) number of allocated blocks in the heap.
*/

size_t _num_allocated_blocks() {
    auto curr_block = block_head;
    size_t count = 0;
    while (curr_block != NULL) {
        count++;
        curr_block = curr_block->next;
    }

    return count;
}

/*
* Returns the overall number (free and used) of allocated bytes in the heap, excluding
* the bytes used by the meta-data structs.
*/

size_t _num_allocated_bytes() {
    auto curr_block = block_head;
    size_t count = 0;
    while (curr_block != NULL) {
        count += curr_block->size;
        curr_block = curr_block->next;
    }

    return count;
}


/*
 * Returns the overall number of meta-data bytes currently in the heap.
*/

size_t _num_meta_data_bytes() {
    auto curr_block = block_head;
    size_t count = 0;
    while (curr_block != NULL) {
        count++;
        curr_block = curr_block->next;
    }

    return count*sizeof(MallocMetadata);
}

/*
 * Returns the number of bytes of a single meta-data structure in the system.
*/

size_t _size_meta_data() {
    return sizeof(MallocMetadata);
}