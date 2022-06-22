#include <unistd.h>

void* smalloc(size_t size) {
    if (size == 0 || size > 1e8) {
        return NULL;
    }

    void* ret_ptr = sbrk((intptr_t)size);
    if (ret_ptr == (void*)(-1)) {
        return NULL;
    }

    return ret_ptr;
}
