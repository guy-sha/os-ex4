#include <unistd.h>
#include <cstring>

#define META_TO_DATA_PTR(block_ptr) ((void*)((MallocMetadata*)block_ptr+1))
#define DATA_TO_META_PTR(data_ptr) ((MallocMetadata*)data_ptr-1)


struct MallocMetadata {
    size_t block_size;
    bool is_free;
    MallocMetadata* next;
    MallocMetadata* prev;
};

struct GlobalMetadata {
    MallocMetadata* head;
    size_t free_blocks;
    size_t free_bytes;
    size_t allocated_blocks;
    size_t allocated_bytes;
};

/* TODO: is it okay if we make this static? */
GlobalMetadata global_ptr = {NULL, 0, 0, 0, 0};
//global_ptr.head = NULL;
//global_ptr.free_blocks = 0;
//global_ptr.free_bytes = 0;
//global_ptr.allocated_blocks = 0;
//global_ptr.allocated_bytes = 0;

/*
void alignInitialProgBreak() {
    void* sbrk_ptr = sbrk(sizeof(*global_ptr));
    if (sbrk_ptr != (void*)(-1)) {
        global_ptr = (GlobalMetadata*)sbrk_ptr;
        global_ptr.head = NULL;
        global_ptr.free_blocks = 0;
        global_ptr.free_bytes = 0;
        global_ptr.allocated_blocks = 0;
        global_ptr.allocated_bytes = 0;
    }
}
*/

void* smalloc(size_t size) {
    if (size == 0 || size > (size_t)1e8) {
        return NULL;
    }

    /*
    if (global_ptr == NULL) {
        alignInitialProgBreak();
        if (global_ptr == NULL) {
            return NULL;
        }
    }
     */

    MallocMetadata* curr = global_ptr.head;
    MallocMetadata* prev = NULL;
    while (curr != NULL) {
        if (curr->is_free == true && curr->block_size >= size) {
            curr->is_free = false;
            global_ptr.free_bytes -= curr->block_size;
            global_ptr.free_blocks -= 1;
            return META_TO_DATA_PTR(curr);
        }

        prev = curr;
        curr = curr->next;
    }

    curr = (MallocMetadata*)sbrk((intptr_t)(sizeof(MallocMetadata) + size));
    if ((void*)curr == (void*)(-1)) {
        return NULL;
    }

    curr->is_free = false;
    curr->block_size = size;
    curr->prev = prev;
    curr->next = NULL;

    if (prev != NULL) {
        prev->next = curr;
    }

    if (global_ptr.head == NULL) {
        global_ptr.head = curr;
    }

    global_ptr.allocated_bytes += size;
    global_ptr.allocated_blocks += 1;

    return META_TO_DATA_PTR(curr);
}

void* scalloc(size_t num, size_t size) {
    void* ret_ptr = smalloc(num*size);
    if (ret_ptr != NULL) {
        memset(ret_ptr, 0, num*size);
    }

    return ret_ptr;
}

void sfree(void* p) {
    if (p == NULL) {
        return;
    }

    MallocMetadata* metadata_ptr = DATA_TO_META_PTR(p);
    if (metadata_ptr->is_free == false) {
        metadata_ptr->is_free = true;
        global_ptr.free_bytes += metadata_ptr->block_size;
        global_ptr.free_blocks += 1;
    }
}

void* srealloc(void* oldp, size_t size) {
    if (size == 0 || size > (size_t)1e8) {
        return NULL;
    }

    if (oldp == NULL) {
        return smalloc(size);
    }

    MallocMetadata* old_meta_ptr = DATA_TO_META_PTR(oldp);
    if (old_meta_ptr->block_size >= size) {
        return oldp;
    } else {
        void* newp = smalloc(size);
        if (newp == NULL) {
            return NULL;
        }

        void* move_ret = memmove(newp, oldp, size);
        if (move_ret != newp) {
            /* TODO: Should we somehow undo the allocation of newp? */
            return NULL;
        }
        sfree(oldp);
        return newp;
    }
}

size_t _num_free_blocks() {
    return global_ptr.free_blocks;
}

size_t _num_free_bytes() {
    return global_ptr.free_bytes;
}

size_t _num_allocated_blocks() {
    return global_ptr.allocated_blocks;
}

size_t _num_allocated_bytes() {
    return global_ptr.allocated_bytes;
}

size_t _num_meta_data_bytes() {
    return (global_ptr.allocated_blocks * sizeof(MallocMetadata));
}
size_t _size_meta_data() {
    return sizeof(MallocMetadata);
}