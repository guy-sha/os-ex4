#include <unistd.h>
#include <cstring>

#define META_TO_DATA_PTR(block_ptr) ((void*)((MallocMetadata*)block_ptr+1))
#define DATA_TO_META_PTR(data_ptr) ((MallocMetadata*)data_ptr-1)
#define MMAP_THRESHOLD (0x20000)

typedef enum { FREE , OCCUPIED , NEW} block_status;



struct MallocMetadata {
    size_t block_size;  /* 8 bytes */
    block_status status; /* 4 bytes */
    bool is_mmapped; /* 1 byte */
    MallocMetadata* next; /* 8 bytes */
    MallocMetadata* prev; /* 8 bytes */
    MallocMetadata* free_by_size_next; /* 8 bytes */
    MallocMetadata* free_by_size_prev; /* 8 bytes */
    /*TODO: ask is we should enforce our struck to by *8 or can we rely on compilers padding */
};

struct GlobalMetadata {
    MallocMetadata* head; /*head of the memory sorted list*/
    MallocMetadata* tail; /*the last node of memory list - wilderness*/
    MallocMetadata* free_by_size_head; /*head of the size sorted list*/
    MallocMetadata* free_by_size_tail; /*the last node of size sorted list*/
    MallocMetadata* mmap_head; /*head of the mmapped list*/
    size_t free_blocks;
    size_t free_bytes;
    size_t allocated_blocks;
    size_t allocated_bytes;
};

/* TODO: is it okay if we make this static? */
GlobalMetadata* global_ptr = NULL;

void _set_up_global_ptr() {
    void* sbrk_ptr = sbrk(sizeof(*global_ptr));
    if (sbrk_ptr != (void*)(-1)) {
        global_ptr = (GlobalMetadata*)sbrk_ptr;
        global_ptr->head = NULL;
        global_ptr->tail = NULL;
        global_ptr->free_by_size_head = NULL;
        global_ptr->free_by_size_tail = NULL;
        global_ptr->mmap_head = NULL;
        global_ptr->free_blocks = 0;
        global_ptr->free_bytes = 0;
        global_ptr->allocated_blocks = 0;
        global_ptr->allocated_bytes = 0;
    }
}

void* smalloc(size_t size) {

    size_t aligned_size = (size%8 == 0 ? size : size+(8-size%8) ); 

    if (aligned_size == 0 || aligned_size > (size_t)1e8) {
        return NULL;
    }

    if (global_ptr == NULL) {
        _set_up_global_ptr();
        if (global_ptr == NULL) {
            return NULL;
        }
    }

    MallocMetadata* place = findFreeBlockBySize(aligned_size);
    /*------------------no place in the list-------------------------*/
    if (place == NULL){ 
        if(aligned_size + sizeof(MallocMetadata) >= MMAP_THRESHOLD)
        {
            /*will use mmap*/
            return 100000000000000000000000000;
        }

        if(global_ptr->tail != NULL && global_ptr->tail->status == FREE)
        { //wilderness block is free but not big enough, so will enlarge it
            size_t diff = aligned_size - global_ptr->tail->block_size; 
            MallocMetadata* curr = (MallocMetadata*)sbrk((intptr_t)(diff));
            if ((void*)curr == (void*)(-1)) {
                return NULL;
            }
            global_ptr->allocated_bytes += diff;
            changeFreeStatus(global_ptr->tail, OCCUPIED); //will change status to the given one and update free stats
            global_ptr->tail->block_size += diff;
            removeFromSizeFreeList(global_ptr->tail);

            return global_ptr->tail;
        }

        MallocMetadata* new_block = (MallocMetadata*)sbrk((intptr_t)(aligned_size + sizeof(MallocMetadata)));
        if ((void*)new_block == (void*)(-1)) {
            return NULL;
        }

        new_block->block_size = aligned_size;
        changeFreeStatus(global_ptr->tail, NEW);
        new_block->is_mmapped = false;

        insertToMemoryList();
        insertToSizeList();

        global_ptr->allocated_blocks += 1;
        global_ptr->allocated_bytes += aligned_size;

        return new_block;
    }
    /*-------------------------------------------------------------------------------------*/

    /*---------------------found place---------------------------*/


    

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
        global_ptr->free_bytes += metadata_ptr->block_size;
        global_ptr->free_blocks += 1;
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
    return global_ptr->free_blocks;
}

size_t _num_free_bytes() {
    return global_ptr->free_bytes;
}

size_t _num_allocated_blocks() {
    return global_ptr->allocated_blocks;
}

size_t _num_allocated_bytes() {
    return global_ptr->allocated_bytes;
}

size_t _num_meta_data_bytes() {
    return (global_ptr->allocated_blocks * sizeof(MallocMetadata));
}
size_t _size_meta_data() {
    return sizeof(MallocMetadata);
}