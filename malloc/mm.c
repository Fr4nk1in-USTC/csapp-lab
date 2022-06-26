/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 *
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include "mm.h"

#include "memlib.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "Fu Shen",
    /* First member's full name */
    "Fu Shen",
    /* First member's email address */
    "fushen@mail.ustc.edu.cn",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""};

/* Type definitions */
typedef unsigned char byte;
typedef unsigned long dword;

/* Sizes in bytes */
#define DWORD_SIZE   8
#define CHUNK_SIZE   (1 << 12)  // 4KB per chunk
#define MIN_BLK_SIZE 32         // at least 4 dword a block

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)

#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

/* Pointers */
static void *first_free;
static void *heap_start;

/* Helper inline functions */
/*
 * Pack size, alloc-bit and prev-alloc-bit into a dword.
 */
static inline dword pack(size_t size, int prev_alloc, int alloc)
{
    return (size & ~0x7) | ((prev_alloc << 1) & 0x2) | (alloc & 0x1);
}

/*
 * Modify the alloc-bit/prev-alloc-bit of a header/footer dword.
 * ptr is the header/footer dword. (footer only for empty blocks)
 */
static inline void modify_alloc(void *ptr, int alloc)
{
    *(dword *)ptr = (*(dword *)ptr & ~0x1) | (alloc & 0x1);
}

static inline void modify_prev_alloc(void *ptr, int prev_alloc)
{
    *(dword *)ptr = (*(dword *)ptr & ~0x2) | ((prev_alloc << 1) & 0x2);
}

/*
 * get/set dword from a pointer
 */
static inline dword get_dword(void *ptr)
{
    return *(dword *)ptr;
}

static inline void set_dword(void *ptr, dword val)
{
    *(dword *)ptr = val;
}

/*
 * Get info from the header/footer dword pointer.
 * (footer only for empty blocks)
 */
static inline size_t get_size(void *ptr)
{
    return get_dword(ptr) & ~0x7;
}

static inline int get_alloc(void *ptr)
{
    return get_dword(ptr) & 0x1;
}

static inline int get_prev_alloc(void *ptr)
{
    return (get_dword(ptr) & 0x2) >> 1;
}

/*
 * Get the address of the header/footer dword.
 * ptr is the address of the payload.
 * (footer only for free blocks)
 */
static inline void *get_header(void *ptr)
{
    return (void *)((byte *)ptr - DWORD_SIZE);
}

static inline void *get_footer(void *ptr)
{  // only for free block
    return (void *)((byte *)ptr + get_size(get_header(ptr)) - 2 * DWORD_SIZE);
}

/*
 * Get the address of the next/prev block payload.
 * ptr is the address of the payload.
 * (prev block only when prev block is free)
 */
static inline void *next_blk(void *ptr)
{
    return (void *)((byte *)ptr + get_size((byte *)ptr - DWORD_SIZE));
}

static inline void *prev_blk(void *ptr)
{  // only for free block
    return (void *)((byte *)ptr - get_size((byte *)ptr - 2 * DWORD_SIZE));
}

/*
 * Get/Set the address of the prev/next free block in the free list
 * from a FREE block payload address.
 */
static inline void *get_prev_free(void *ptr)
{
    return (void *)(*(dword *)ptr);
}

static inline void *get_next_free(void *ptr)
{
    return (void *)(*((dword *)ptr + 1));
}

static inline void set_prev_free(void *ptr, void *prev)
{
    *(dword *)ptr = (dword)prev;
}

static inline void set_next_free(void *ptr, void *next)
{
    *((dword *)ptr + 1) = (dword)next;
}

/* Min/Max macros */
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

/* Helper Functions */
static void *extend_heap(size_t size);
static void *coalesce_free(void *payload);
static void *find_fit(size_t size);
static void  place(void *free_payload, size_t size);
static void  insert_free(void *free_payload);
static void  remove_free(void *free_payload);

/* Checker Function */
void mm_check(const char *);

/*
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    first_free = NULL;
    /* Allocate a dword before the first block */
    if ((heap_start = mem_sbrk(DWORD_SIZE)) == NULL)
        return -1;
    /* Initial dword will be the new begin dword when extend_heap() */
    set_dword(heap_start, pack(0, 1, 1));

    if (extend_heap(CHUNK_SIZE) == NULL)
        return -1;

    return 0;
}

/*
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
    if (size == 0)
        return NULL;

    size_t new_size = ALIGN(size + DWORD_SIZE);  // Add the size of the header dword
    void  *payload;                              // The return payload address

    new_size = MAX(new_size, MIN_BLK_SIZE);  // new_size must >= MIN_BLK_SIZE

    /* Try to find a fit free block */
    if ((payload = find_fit(new_size)) != NULL) {
        place(payload, new_size);
        return payload;
    }
    /* No fit found, extend the heap */
    size_t extend_size = MAX(new_size, CHUNK_SIZE);
    if ((payload = extend_heap(extend_size)) == NULL)
        return NULL;
    place(payload, new_size);

    return payload;
}

/*
 * mm_free - Freeing a block from its payload address.
 */
void mm_free(void *ptr)
{
    size_t size        = get_size(get_header(ptr));
    int    prev_alloc  = get_prev_alloc(get_header(ptr));
    void  *next_header = get_header(next_blk(ptr));

    /* Set the header/footer of the block */
    modify_alloc(get_header(ptr), 0);
    set_dword(get_footer(ptr), pack(size, prev_alloc, 0));
    modify_prev_alloc(next_header, 0);

    /* Coalesce the block */
    coalesce_free(ptr);
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
    void  *oldptr = ptr;
    void  *newptr;
    size_t copySize;

    newptr = mm_malloc(size);
    if (newptr == NULL)
        return NULL;
    copySize = get_size(get_header(oldptr));
    if (size < copySize)
        copySize = size;
    memcpy(newptr, oldptr, copySize);
    mm_free(oldptr);
    return newptr;
}

/*
 * Extend the heap is the same as getting a new free block
 * while the header is the old ending dword. And the new ending
 * dword is the "next block" header.
 */
static void *extend_heap(size_t size)
{
    void *old_ending_dword = get_header(mem_sbrk(0));
    int   prev_alloc       = get_prev_alloc(old_ending_dword);
    void *payload;
    size = ALIGN(size);

    if ((payload = mem_sbrk(size)) == (void *)-1)
        return NULL;

    /* Set the header/footer of the block */
    set_dword(get_header(payload), pack(size, prev_alloc, 0));
    set_dword(get_footer(payload), pack(size, prev_alloc, 0));

    /* Set the new ending dword */
    set_dword(get_header(next_blk(payload)), pack(0, 0, 1));
    /* Return thecoalesced the block */
    return coalesce_free(payload);
}

/*
 * Coalesce the free block with the next/prev block if it's free.
 */
static void *coalesce_free(void *payload)
{
    int    prev_alloc = get_prev_alloc(get_header(payload));
    int    next_alloc = get_alloc(get_header(next_blk(payload)));
    size_t size       = get_size(get_header(payload));
    size_t prev_size;
    size_t next_size;

    if (prev_alloc && next_alloc) {  // prev & next both allocated
        insert_free(payload);
    } else if (prev_alloc && !next_alloc) {  // only prev allocated
        remove_free(next_blk(payload));

        next_size = get_size(get_header(next_blk(payload)));
        size      += next_size;

        set_dword(get_header(payload), pack(size, 1, 0));
        set_dword(get_footer(payload), pack(size, 1, 0));

        insert_free(payload);
    } else if (!prev_alloc && next_alloc) {  // only next allocated
        prev_size = get_size(get_header(prev_blk(payload)));
        size      += prev_size;

        payload = prev_blk(payload);
        set_dword(get_header(payload), pack(size, 1, 0));
        set_dword(get_footer(payload), pack(size, 1, 0));
    } else {  // prev & next both free
        remove_free(next_blk(payload));

        prev_size = get_size(get_header(prev_blk(payload)));
        next_size = get_size(get_header(next_blk(payload)));
        size      += prev_size + next_size;

        payload = prev_blk(payload);
        set_dword(get_header(payload), pack(size, 1, 0));
        set_dword(get_footer(payload), pack(size, 1, 0));
    }
    return payload;
}

/*
 * Find first fit free block. Return NULL if no fit free block found.
 */
static void *find_fit(size_t size)
{
    for (void *p = first_free; p != NULL; p = get_next_free(p)) {
        if (get_size(get_header(p)) >= size)
            return p;
    }
    return NULL;
}

/*
 * Place a new block in a free block.
 */
static void place(void *payload, size_t size)
{
    remove_free(payload);

    size_t free_size   = get_size(get_header(payload));
    size_t remain_size = free_size - size;

    if (remain_size >= MIN_BLK_SIZE) {
        set_dword(get_header(payload), pack(size, 1, 1));

        void *new_free = next_blk(payload);
        set_dword(get_header(new_free), pack(remain_size, 1, 0));
        set_dword(get_footer(new_free), pack(remain_size, 1, 0));
        insert_free(new_free);
    } else {
        set_dword(get_header(payload), pack(free_size, 1, 1));
        modify_prev_alloc(get_header(next_blk(payload)), 1);
    }
}

/*
 * Insert/Remove a free block into/from the free list.
 */
static void insert_free(void *payload)
{
    if (first_free == NULL) {  // free list is empty
        set_prev_free(payload, NULL);
        set_next_free(payload, NULL);
        first_free = payload;
    } else {
        set_prev_free(payload, NULL);
        set_next_free(payload, first_free);
        set_prev_free(first_free, payload);
        first_free = payload;
    }
}

static void remove_free(void *payload)
{
    if (first_free == NULL)
        return;

    void *prev = get_prev_free(payload);
    void *next = get_next_free(payload);

    if (prev != NULL && next != NULL) {
        set_next_free(prev, next);
        set_prev_free(next, prev);
    } else if (prev != NULL) {
        set_next_free(prev, NULL);
    } else if (next != NULL) {
        set_prev_free(next, NULL);
        first_free = next;
    } else {
        first_free = NULL;
    }
}

void mm_check(const char *function)
{
    printf("\n---cur func: %s :\n", function);
    void *bp                = first_free;
    int   count_empty_block = 0;
    printf(" ---free list:\n");
    while (bp != NULL)  // not end block;
    {
        count_empty_block++;
        if (get_alloc(get_header(bp)) == 1) {
            printf("WRONG ALLOC BIT\n");
        }
        printf("  addr_start: %p, addr_end: %p, size_head: %u, size_foot: %u, "
               "PRED= %p, SUCC= %p \n",
               get_header(bp), get_footer(bp), get_size(get_header(bp)),
               get_size(get_footer(bp)), get_prev_free(bp), get_next_free(bp));

        bp = get_next_free(bp);
    }
    printf("empty_block num: %d\n", count_empty_block);
    printf("ending = %lx\n\n", get_dword(get_header(mem_sbrk(0))));
}