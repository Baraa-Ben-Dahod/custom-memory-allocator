#include <unistd.h>
/*
 * Tries to allocate 'size' bytes.
 * Return value:
 * Success – a pointer to the first allocated byte within the allocated block.
 * Failure –
 * If 'size' is 0 returns NULL.
 * If 'size' is more than 100,000,000 (10^8) return NULL.
 * If sbrk fails, return NULL.
 */


void* smalloc(size_t size) {
    // size_t is unsigned number
    if (size == 0 || size > 100000000) {
        return NULL;
    }

    void* ptr = sbrk(size);

    // sbrk() fails
    if (ptr == reinterpret_cast<void *>(-1)) {
        return NULL;
    }

    return ptr;
}