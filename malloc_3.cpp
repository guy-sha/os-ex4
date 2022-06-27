#include <unistd.h>
#include <cstring>
#include <sys/mman.h>
#include <cassert>
#include <exception>

#include <stdio.h>

#define META_TO_DATA_PTR(block_ptr) ((void*)((MallocMetadata*)block_ptr+1))
#define DATA_TO_META_PTR(data_ptr) ((MallocMetadata*)data_ptr-1)
#define SPLIT_THRESHOLD (128)
#define MMAP_THRESHOLD (0x20000)
#define IS_MMAP (true)
#define NOT_MMAP (false)


class OutOfMemory : public std::exception {};


typedef enum { FREE , OCCUPIED} block_status;


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

GlobalMetadata global_ptr = { NULL, NULL, NULL, NULL, NULL, 0,  0, 0, 0};
bool do_setup = true;

int alignInitialProgBreak() {
    unsigned long init_sbrk_ptr = (unsigned long)sbrk(0);
    size_t aligned_size = (init_sbrk_ptr%8 == 0 ? 0 : (8-init_sbrk_ptr%8) );

    void* sbrk_ptr = sbrk(aligned_size);
    if (sbrk_ptr == (void*)(-1)) {
        return -1;
    }

    return 1;
}

/*------------------helper functions--------------*/

void updateMetaData(MallocMetadata* meta, block_status stat, size_t new_size, bool is_mmap=false)
{
    meta->status = stat;
    meta->block_size = new_size;
    meta->is_mmapped = is_mmap;
}

void updateStats(long free_blocks, long free_bytes, long allocated_blocks, long allocated_bytes) {
    global_ptr.free_blocks += free_blocks;
    global_ptr.free_bytes += free_bytes;
    global_ptr.allocated_blocks += allocated_blocks;
    global_ptr.allocated_bytes += allocated_bytes;
}

void removeFromSizeFreeList(MallocMetadata* meta)
{
    /*just take out, no stats needed */
    MallocMetadata* prev = meta->free_by_size_prev, *next = meta->free_by_size_next;
    if (prev != NULL) {
        prev->free_by_size_next = next;
    } else {
        global_ptr.free_by_size_head = next;
    }

    if (next != NULL) {
        next->free_by_size_prev = prev;
    } else {
        global_ptr.free_by_size_tail = prev;
    }
}

MallocMetadata* findBestFit(size_t size)
{
    /*finds smallest large enough block*/
    if (global_ptr.free_by_size_tail == NULL || global_ptr.free_by_size_tail->block_size < size) {
        return NULL;
    }

    MallocMetadata* curr = global_ptr.free_by_size_head;
    while (curr != NULL && size > curr->block_size) {
        curr = curr->free_by_size_next;
    }

    return curr;
}

void appendToMemoryList(MallocMetadata* meta)
{
    /*just insert, no stats needed */
    MallocMetadata* curr_tail = global_ptr.tail;
    if (curr_tail != NULL) {
        curr_tail->next = meta;
        meta->prev = curr_tail;
    } else {
        meta->prev = NULL;
        global_ptr.head = meta;
    }

    meta->next = NULL;
    global_ptr.tail = meta;
}
/* Return true if a < b
 * Both assumed to be not NULL */
bool isLowerInFreeList(MallocMetadata* a, MallocMetadata* b) {
    return ((a->block_size < b->block_size) || ((a->block_size == b->block_size) && (a < b)));
}

void insertToSizeFreeList(MallocMetadata* meta)
{
    /*just insert, no stats needed */
    if (global_ptr.free_by_size_tail == NULL) {
        global_ptr.free_by_size_head = meta;
        global_ptr.free_by_size_tail = meta;
        meta->free_by_size_prev = NULL;
        meta->free_by_size_next = NULL;
        return;
    } else if (isLowerInFreeList(global_ptr.free_by_size_tail, meta)) {
        global_ptr.free_by_size_tail->free_by_size_next = meta;
        meta->free_by_size_prev = global_ptr.free_by_size_tail;
        meta->free_by_size_next = NULL;
        global_ptr.free_by_size_tail = meta;
        return;
    }

    /* If we got here the list should be non-empty
     * And meta will never be inserted at the tail */
    assert(global_ptr.free_by_size_head != NULL);

    /* After break meta should be inserted BEFORE curr */
    MallocMetadata* curr = global_ptr.free_by_size_head;
    while (curr->free_by_size_next != NULL) {
        if (isLowerInFreeList(meta, curr)) {
            break;
        }

        curr = curr->free_by_size_next;
    }


    meta->free_by_size_prev = curr->free_by_size_prev;
    meta->free_by_size_next = curr;

    if (curr->free_by_size_prev == NULL) {
        global_ptr.free_by_size_head = meta;
    } else {
        curr->free_by_size_prev->free_by_size_next = meta;
    }

    curr->free_by_size_prev = meta;
}

void mergeWithUpper(MallocMetadata* block, block_status status) {
    MallocMetadata* upper = block->next;
    if (upper->status == FREE) {
        removeFromSizeFreeList(upper);
    }

    block->next = upper->next;
    if (upper->next != NULL) {
        upper->next->prev = block;
    } else {
        global_ptr.tail = block;
    }

    updateMetaData(block, status, block->block_size + sizeof(MallocMetadata) + upper->block_size);
}

void mergeWithLower(MallocMetadata* block, block_status status) {
    MallocMetadata* lower = block->prev;
    if (lower->status == FREE) {
        removeFromSizeFreeList(lower);
    }

    lower->next = block->next;
    if (block->next != NULL) {
        block->next->prev = lower;
    } else {
        global_ptr.tail = lower;
    }

    updateMetaData(lower, status, block->block_size + sizeof(MallocMetadata) + lower->block_size);
}

void splitBlock(MallocMetadata* block_to_split, size_t new_size)
{
    /*remember to update all metadata and stats*/
    MallocMetadata* other_part = (MallocMetadata*)((char*)(block_to_split) + sizeof(MallocMetadata) + new_size);
    block_status orig_status = block_to_split->status;
    size_t orig_size = block_to_split->block_size;

    updateMetaData(block_to_split, OCCUPIED, new_size);
    updateMetaData(other_part, FREE, (orig_size - new_size - sizeof(MallocMetadata)));

    other_part->next = block_to_split->next;
    other_part->prev = block_to_split;
    if (block_to_split->next == NULL) {
        global_ptr.tail = other_part;
    } else {
        block_to_split->next->prev = other_part;
    }
    block_to_split->next = other_part;

    if (orig_status == FREE) {
        removeFromSizeFreeList(block_to_split);
        updateStats(0, -(long)(new_size + sizeof(MallocMetadata)), 1, -((long)sizeof(MallocMetadata)));
    } else {
        if (other_part->next != NULL && other_part->next->status == FREE) {
            mergeWithUpper(other_part, FREE);
            updateStats(0, orig_size - new_size, 0, 0);
        } else {
            updateStats(1, other_part->block_size, 1, -((long)sizeof(MallocMetadata)));
        }
    }

    insertToSizeFreeList(other_part);
}

void freeAndMergeAdjacent(MallocMetadata* block)
{
    /*mark as free, try to merge with neighbors and handle stats*/
    updateMetaData(block, FREE, block->block_size);
    updateStats(1, block->block_size, 0, 0);

    MallocMetadata* prev = block->prev, *next = block->next;
    if (next != NULL && next->status == FREE) {
        mergeWithUpper(block, FREE);
        updateStats(-1, sizeof(MallocMetadata), -1, sizeof(MallocMetadata));
    }

    if (prev != NULL && prev->status == FREE) {
        mergeWithLower(block, FREE);
        updateStats(-1, sizeof(MallocMetadata), -1, sizeof(MallocMetadata));
        block = prev;
    }

    insertToSizeFreeList(block);
}

MallocMetadata* tryToReuseOrMerge(MallocMetadata* block, size_t size)
{/*will handle a-f and do split if necessary and handle stats if needed*/
    size_t next_size = block->next != NULL ? block->next->block_size : 0;
    size_t prev_size = block->prev != NULL ? block->prev->block_size : 0;
    bool prev_free = (block->prev != NULL && block->prev->status == FREE);
    bool next_free = (block->next != NULL && block->next->status == FREE);

    /* a */
    if (block->block_size >= size) {
        return block;
    }

    /* b */
    if (prev_free) {
        size_t merged_size = block->block_size + prev_size + sizeof(MallocMetadata);
        size_t diff = size - merged_size;
        if (merged_size >= size) {
            mergeWithLower(block, OCCUPIED);
            block = block->prev;
            updateStats(-1, -(prev_size), -1, sizeof(MallocMetadata));
            return block;
        } else if (block == global_ptr.tail) {
            /* current block is wilderness */
            void* prev_prog_break = sbrk((intptr_t)(diff));
            if (prev_prog_break == (void*)(-1)) {
                throw OutOfMemory();
            }
            mergeWithLower(block, OCCUPIED);
            block = block->prev;
            updateMetaData(block, OCCUPIED, block->block_size + diff);
            updateStats(-1, -(prev_size), -1, sizeof(MallocMetadata) + diff);
            return block;
        }
    }

    /* c */
    if (block == global_ptr.tail) {
        size_t diff = size - block->block_size;
        void* prev_prog_break = sbrk((intptr_t)(diff));
        if (prev_prog_break == (void*)(-1)) {
            throw OutOfMemory();
        }
        updateMetaData(block, OCCUPIED, block->block_size + diff);
        updateStats(0,0,0,diff);
        return block;
    }

    /* d */
    if (next_free && (size <= (block->block_size + next_size + sizeof(MallocMetadata)))) {
        mergeWithUpper(block, OCCUPIED);
        updateStats(-1, -(next_size), -1, sizeof(MallocMetadata));
        return block;
    }

    /* e */
    if (prev_free && next_free && (prev_size + next_size + block->block_size + 2* sizeof(MallocMetadata) >= size)) {
        mergeWithUpper(block, OCCUPIED);
        mergeWithLower(block, OCCUPIED);
        block = block->prev;
        updateStats(-2, -(prev_size + next_size), -2, 2*sizeof(MallocMetadata));
        return block;
    }

    /* f */
    if (next_free && block->next == global_ptr.tail) {
        if (prev_free) {
            size_t merged_size = prev_size + next_size + block->block_size + 2 * sizeof(MallocMetadata);
            size_t diff = size - merged_size;

            void *prev_prog_break = sbrk((intptr_t) (diff));
            if (prev_prog_break == (void *) (-1)) {
                throw OutOfMemory();
            }

            mergeWithUpper(block, OCCUPIED);
            mergeWithLower(block, OCCUPIED);
            block = block->prev;

            updateMetaData(block, OCCUPIED, block->block_size + diff);
            updateStats(-2, -(prev_size + next_size), -2,
                        2 * sizeof(MallocMetadata) + diff);
            return block;
        } else {
            size_t merged_size = next_size + block->block_size + sizeof(MallocMetadata);
            size_t diff = size - merged_size;

            void *prev_prog_break = sbrk((intptr_t) (diff));
            if (prev_prog_break == (void *) (-1)) {
                throw OutOfMemory();
            }

            mergeWithUpper(block, OCCUPIED);
            updateMetaData(block, OCCUPIED, block->block_size + diff);
            updateStats(-1, -(next_size), -1,sizeof(MallocMetadata) + diff);
            return block;
        }
    }

    return NULL;
}

void prependToMmapList(MallocMetadata* block)
{
    /*just insert, no stats needed */
    block->next = global_ptr.mmap_head;
    block->prev = NULL;
    if (global_ptr.mmap_head != NULL) {
        global_ptr.mmap_head->prev = block;
    }

    global_ptr.mmap_head = block;
}

void removeFromMmapList(MallocMetadata* meta)
{
    /*just take out, no stats needed */
    MallocMetadata* prev = meta->prev, *next = meta->next;
    if (prev != NULL) {
        prev->next = next;
    } else {
        global_ptr.mmap_head = next;
    }

    if (next != NULL) {
        next->prev = prev;
    }
}

/*----------------------------------------------------*/

void* smalloc(size_t size) {

    size_t aligned_size = (size%8 == 0 ? size : size+(8-size%8) ); 

    if (aligned_size == 0 || aligned_size > (size_t)1e8) {
        return NULL;
    }

    if (do_setup) {
        if (-1 == alignInitialProgBreak()) {
            return NULL;
        }
        do_setup = false;
    }

    MallocMetadata* place = findBestFit(aligned_size);

    /*------------------no place in the list-------------------------*/
    if (place == NULL){ 
        if(aligned_size + sizeof(MallocMetadata) >= MMAP_THRESHOLD)
        {
            MallocMetadata* new_region = (MallocMetadata*)mmap(NULL, aligned_size + sizeof(MallocMetadata), 
                                                                PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
            if((void*)new_region == (void*)(-1))
            {
                return NULL;
            }
            updateMetaData(new_region, OCCUPIED, aligned_size, true);
            updateStats(0,0,1,aligned_size);
            prependToMmapList(new_region);

            return META_TO_DATA_PTR(new_region);
        }

        if(global_ptr.tail != NULL && global_ptr.tail->status == FREE)
        { //wilderness block is free but not big enough, so will enlarge it
            long diff = (long)(aligned_size - global_ptr.tail->block_size);
            MallocMetadata* curr = (MallocMetadata*)sbrk((intptr_t)(diff));
            if ((void*)curr == (void*)(-1)) {
                return NULL;
            }

            updateStats(-1, -(long)(global_ptr.tail->block_size), 0, diff);
            updateMetaData(global_ptr.tail, OCCUPIED, global_ptr.tail->block_size + diff); //will change status to the given one and update free stats
            removeFromSizeFreeList(global_ptr.tail);

            return META_TO_DATA_PTR(global_ptr.tail);
        }

        MallocMetadata* new_block = (MallocMetadata*)sbrk((intptr_t)(aligned_size + sizeof(MallocMetadata)));
        if ((void*)new_block == (void*)(-1)) {
            return NULL;
        }

        updateMetaData(new_block, OCCUPIED, aligned_size);
        updateStats(0,0,1,aligned_size);
        appendToMemoryList(new_block);

        return META_TO_DATA_PTR(new_block);
    }
    /*-------------------------------------------------------------------------------------*/


    /*---------------------found place---------------------------*/
    size_t diff = place->block_size - aligned_size;
    if(diff >= SPLIT_THRESHOLD + sizeof(MallocMetadata))
    {
        splitBlock(place, aligned_size);
        return META_TO_DATA_PTR(place);
    }
    else{
        updateMetaData(place, OCCUPIED, place->block_size);
        updateStats(-1, -(long)(place->block_size), 0, 0);
        removeFromSizeFreeList(place);
        return META_TO_DATA_PTR(place);
    }
}

void* scalloc(size_t num, size_t size) {
    void* ret_ptr = smalloc(num*size);
    if (ret_ptr != NULL) {
        memset(ret_ptr, 0,(DATA_TO_META_PTR(ret_ptr))->block_size);
    }

    return ret_ptr;
}

void sfree(void* p) {
    if (p == NULL) {
        return;
    }
    MallocMetadata* metadata_ptr = DATA_TO_META_PTR(p);

    if (metadata_ptr->status == OCCUPIED) {
        if(metadata_ptr->is_mmapped == true)
        {
//            updateMetaData(metadata_ptr, FREE, metadata_ptr->block_size, true);
            updateStats(0,0,-1,-(long)(metadata_ptr->block_size));
            removeFromMmapList(metadata_ptr);
            int res = munmap(metadata_ptr, metadata_ptr->block_size + sizeof(MallocMetadata));
            /*as long as metadata_ptr was mmapped it should not fail*/
            assert(res != -1);
        }
        else{
            freeAndMergeAdjacent(metadata_ptr); 
        }
    }
}

void* srealloc(void* oldp, size_t size) {

    size_t aligned_size = (size%8 == 0 ? size : size+(8-size%8) );
    if (aligned_size == 0 || aligned_size > (size_t)1e8) {
        return NULL;
    }

    if (oldp == NULL) {
        return smalloc(aligned_size);
    }

    MallocMetadata* old_meta_ptr = DATA_TO_META_PTR(oldp);
    if (old_meta_ptr->block_size == aligned_size) {
        return oldp;
    } 
    if (old_meta_ptr->is_mmapped == IS_MMAP)
    {
        MallocMetadata* new_region = (MallocMetadata*)mmap(NULL, aligned_size + sizeof(MallocMetadata), 
                                                                PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
        if( (void*)new_region == (void*)(-1))
        {
            return NULL;
        }

        void* move_ret = memmove(META_TO_DATA_PTR(new_region), oldp, old_meta_ptr->block_size);
        if( move_ret != META_TO_DATA_PTR(new_region))
        {
            return NULL;
        }
        updateMetaData(new_region, OCCUPIED, aligned_size, true);
        updateStats(0, 0, 1, aligned_size);
        prependToMmapList(new_region);
        
        sfree(oldp);
        return META_TO_DATA_PTR(new_region);
    }
    else {
        MallocMetadata* newp_meta;
        void* address;
        bool used_malloc = false;
        try{
            newp_meta = tryToReuseOrMerge(old_meta_ptr,aligned_size);
        }
        catch(OutOfMemory& err){
            return NULL;
        }
        if ( newp_meta == NULL)
        { /*could not reuse any existing blocks*/
            address = smalloc(aligned_size);
            if(address == NULL)
            {
                return NULL;
            }
            used_malloc = true;
        }else {
            address = META_TO_DATA_PTR(newp_meta);
        }

        size_t min_copy_size = old_meta_ptr->block_size <= aligned_size ? old_meta_ptr->block_size : aligned_size;
        void* move_ret = memmove(address, oldp, min_copy_size);
        if (move_ret != address) {
            /* TODO: Should we somehow undo the allocation of newp? */
            return NULL;
        }

        if(used_malloc == true)
        {
            sfree(oldp);
        } else {
            if (newp_meta->block_size >= aligned_size + SPLIT_THRESHOLD + sizeof(MallocMetadata)) {
                splitBlock(newp_meta, aligned_size);
            }
        }
        return address;
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