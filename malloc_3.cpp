#include <unistd.h>
#include <cstring>
#include <sys/mman.h>
#include <cassert>
#include <exception>

#define META_TO_DATA_PTR(block_ptr) ((void*)((MallocMetadata*)block_ptr+1))
#define DATA_TO_META_PTR(data_ptr) ((MallocMetadata*)data_ptr-1)
#define SPLIT_THRESHOLD (128)
#define MMAP_THRESHOLD (0x20000)
#define IS_MMAP (true)
#define NOT_MMAP (false)


class OutOfMemory : public std::exception {};


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
    unsigned long init_sbrk_ptr = (unsigned long)sbrk(0);
    size_t aligned_size = (init_sbrk_ptr%8 == 0 ? 0 : (8-init_sbrk_ptr%8) );

    void* sbrk_ptr = sbrk(sizeof(*global_ptr)+aligned_size);
    if (sbrk_ptr != (void*)(-1)) {
        global_ptr = (GlobalMetadata*)((char*)sbrk_ptr + aligned_size);
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

/*------------------helper functions--------------*/

void updateMetaData(MallocMetadata* meta, block_status stat, size_t diff, bool is_mmap=false)
{

}

void removeFromSizeFreeList(MallocMetadata* meta)
{
    /*just take out, no stats needed */
}

MallocMetadata* findBestFit(size_t size)
{
    /*finds smallest large enough block*/
}

void insertToMemoryList(MallocMetadata* meta)
{
    /*just insert, no stats needed */
}

void insertToSizeFreeList(MallocMetadata* meta)
{
    /*just insert, no stats needed */
}

MallocMetadata* splitBlock(MallocMetadata* block_to_split)
{
    /*remember to update all metadatad and stats*/
}

void freeAndMergeAdjacent(MallocMetadata* block)
{
    /*mark as free, try to merge with neighbors and handle stats*/
}

MallocMetadata* tryToReuseOrMerge(MallocMetadata* block, size_t size)
{/*will handle a-f and do split if necessary and handle stats if needed*/

}

void insertToMmapList(MallocMetadata* block)
{
    /*just insert, no stats needed */
}

void removeFromMmapList(MallocMetadata* block)
{
    /*just take out, no stats needed */
}

/*----------------------------------------------------*/

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
            updateMetaData(new_region, NEW, aligned_size, true);
            insertToMmapList(new_region);

            return META_TO_DATA_PTR(new_region);
        }

        if(global_ptr->tail != NULL && global_ptr->tail->status == FREE)
        { //wilderness block is free but not big enough, so will enlarge it
            size_t diff = aligned_size - global_ptr->tail->block_size; 
            MallocMetadata* curr = (MallocMetadata*)sbrk((intptr_t)(diff));
            if ((void*)curr == (void*)(-1)) {
                return NULL;
            }
            global_ptr->allocated_bytes += diff;
            updateMetaData(global_ptr->tail, OCCUPIED, diff); //will change status to the given one and update free stats
            removeFromSizeFreeList(global_ptr->tail);

            return META_TO_DATA_PTR(global_ptr->tail);
        }

        MallocMetadata* new_block = (MallocMetadata*)sbrk((intptr_t)(aligned_size + sizeof(MallocMetadata)));
        if ((void*)new_block == (void*)(-1)) {
            return NULL;
        }

        updateMetaData(new_block, NEW, aligned_size);
        insertToMemoryList(new_block);
        insertToSizeFreeList(new_block);

        /*global_ptr->allocated_blocks += 1;
        global_ptr->allocated_bytes += aligned_size; - inside updateMetadata*/

        return META_TO_DATA_PTR(new_block);
    }
    /*-------------------------------------------------------------------------------------*/


    /*---------------------found place---------------------------*/
    size_t diff = place->block_size - aligned_size;
    if(diff >= SPLIT_THRESHOLD + sizeof(MallocMetadata))
    {
        MallocMetadata* other_part = splitBlock(place);
        removeFromSizeFreeList(place);
        insertToSizeFreeList(other_part);
        return META_TO_DATA_PTR(place);
    }
    else{
        updateMetaData(place, OCCUPIED, 0);
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
            updateMetaData(metadata_ptr, FREE, metadata_ptr->block_size, true);
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
        updateMetaData(new_region, NEW, aligned_size, true);
        insertToMmapList(new_region);
        
        sfree(oldp);
        return META_TO_DATA_PTR(new_region);
    }
    else {
        MallocMetadata* newp_meta;
        void* address;
        bool free_old = false;
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
            free_old = true;
        }else {
            address = META_TO_DATA_PTR(newp_meta);
        }
        void* move_ret = memmove(address, oldp, DATA_TO_META_PTR(oldp)->block_size);
        if (move_ret != address) {
            /* TODO: Should we somehow undo the allocation of newp? */
            return NULL;
        }
        if(free_old == true)
        {
            sfree(oldp);
        }
        return address;
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