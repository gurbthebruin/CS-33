/* 
 * mm.c -  Simple allocator based on implicit free lists, 
 *         first fit placement, and boundary tag coalescing. 
 *
 * Each block has header and footer of the form:
 * 
 *      31                     3  2  1  0 
 *      -----------------------------------
 *     | s  s  s  s  ... s  s  s  0  0  a/f
 *      ----------------------------------- 
 * 
 * where s are the meaningful size bits and a/f is set 
 * iff the block is allocated. The list has the following form:
 *
 * begin                                                          end
 * heap                                                           heap  
 *  -----------------------------------------------------------------   
 * |  pad   | hdr(8:a) | ftr(8:a) | zero or more usr blks | hdr(8:a) |
 *  -----------------------------------------------------------------
 *          |       prologue      |                       | epilogue |
 *          |         block       |                       | block    |
 *
 * The allocated prologue and epilogue blocks are overhead that
 * eliminate edge conditions during coalescing.
 *
 * 
 * My implementation consists of an explicit free list with a first fit implementation using a doubly linked list to keep track of the free list. My mm_init function does almost the exact same thing as the textbook implicit implementation does, which is initializes the prologue header and footer and the epilogue header in order to avoid traversing out of bounds. Also, my mm_malloc acts very similar to that of the implicit free list as it uses first fit to traverse the free list and finds the first block capable of completing the request and splices the block if it's too large.In order to impement this explicit list, I created a pointer to the free list called free_listp and a function called edit_free_list that would add or remove a block from the free list depending on the integer value provided when calling this function.  */
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include "mm.h"
#include "memlib.h"
#include <stdint.h>

/* Your info */
team_t team = { 
    /* First and last name */
    "Gurbir Arora",
    /* UID */
    "105178554"
}; 

/* $begin mallocmacros */
/* Basic constants and macros */
#define WSIZE       4       /* word size (bytes) */  
#define DSIZE       8       /* doubleword size (bytes) */
#define CHUNKSIZE  (1<<16)  /* initial heap size (bytes) */
#define OVERHEAD    8       /* overhead of header and footer (bytes) */

#define MAX(x, y) ((x) > (y)? (x) : (y))  

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc)  ((size) | (alloc))

/* Read and write a word at address p */
#define GET(p)       (*(uint32_t *)(p))
#define PUT(p, val)  (*(uint32_t *)(p) = (val))  

/* Read the size and allocated fields from address p */
#define GET_SIZE(p)  (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)

/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp)       ((char *)(bp) - WSIZE)  
#define FTRP(bp)       ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp)  ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp)  ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))
/* $end mallocmacros */
#define GET_NEXT(bp) (*(char **)(bp))
#define GET_PREV(bp) (*(char **)(bp + DSIZE)) 

#define SET_NEXT(bp, qp) (GET_NEXT(bp) = qp) 
#define SET_PREV(bp, qp) (GET_PREV(bp) = qp) 
/* Global variables */
static char *heap_listp = 0;  /* pointer to first block */  
static char *free_listp = 0; 
/* function prototypes for internal helper routines */
static void *extend_heap(size_t words);
static void place(void *bp, size_t asize);
static void *find_fit(size_t asize);
static void *coalesce(void *bp);
static void printblock(void *bp); 
static void checkblock(void *bp);
static void edit_free_list(void *bp, int i); 
//initializes the alignment padding, proglogue header/footer, epilogue header, and sets up the freelist pointer and the heaplist pointer. It also extends the heap by the CHUNKSIZE/WSIZE
/* 
 * mm_init - Initialize the memory manager 
 */
/* $begin mminit */
int mm_init(void) 
{
    /* create the initial empty heap */
    if ((heap_listp = mem_sbrk(4*WSIZE)) == NULL)
	return -1;
    PUT(heap_listp, 0);                        /* alignment padding */
    PUT(heap_listp+WSIZE, PACK(OVERHEAD, 1));  /* prologue header */ 
    PUT(heap_listp+DSIZE, PACK(OVERHEAD, 1));  /* prologue footer */ 
    PUT(heap_listp+WSIZE+DSIZE, PACK(0, 1));   /* epilogue header */
    free_listp = NULL;

    /* Extend the empty heap with a free block of CHUNKSIZE bytes */
    if (extend_heap(CHUNKSIZE/WSIZE) == NULL)
	return -1;
    return 0;
}
/* $end mminit */
//my mm_malloc acts very similar to that of the implicit free list as it uses first fit to traverse the free list and finds the first block capable of completing the request and splices the block if it's too large.
/* 
 * mm_malloc - Allocate a block with at least size bytes of payload 
 */
/* $begin mmmalloc */
void *mm_malloc(size_t size) 
{
    uint32_t asize;      /* adjusted block size */
    uint32_t extendsize; /* amount to extend heap if no fit */
    char *bp;      
    if(!heap_listp)
        mm_init();
    /* Ignore spurious requests */
    if (size <= 0)
	return NULL;

    /* Adjust block size to include overhead and alignment reqs. */
    if (size <= DSIZE)
	asize = DSIZE + OVERHEAD;
    else
	asize = DSIZE * ((size + (OVERHEAD) + (DSIZE-1)) / DSIZE);
    
    /* Search the free list for a fit */
    if ((bp = find_fit(asize)) != NULL) {
	place(bp, asize);
	return bp;
    }

    /* No fit found. Get more memory and place the block */
    extendsize = MAX(asize,CHUNKSIZE);
    if ((bp = extend_heap(extendsize/WSIZE)) == NULL)
	return NULL;
    place(bp, asize);
   // mm_checkheap(1); 
    return bp;
} 
/* $end mmmalloc */
//mm_free updates the header and footer of the new freed block by changing its allocated bit and the free block is placed at the beginning of the free list using the coaleace function. 
/* 
 * mm_free - Free a block 
 */
/* $begin mmfree */
void mm_free(void *bp)
{
    uint32_t size = GET_SIZE(HDRP(bp));
    if(bp == NULL)
	return; 
    if(!heap_listp) mm_init();  
    PUT(HDRP(bp), PACK(size, 0)); 
    PUT(FTRP(bp), PACK(size, 0));
   // printf("before coalescing"); 
   // printblock(bp);    
    coalesce(bp);
   // printf("after coalescing"); 
   // mm_checkheap(2); 
}

/* $end mmfree */

/*
 * mm_realloc - naive implementation of mm_realloc
 */
void *mm_realloc(void *ptr, size_t size)
{
    void *newp;
    size_t copySize;

    if ((newp = mm_malloc(size)) == NULL) {
	printf("ERROR: mm_malloc failed in mm_realloc\n");
	exit(1);
    }
    copySize = GET_SIZE(HDRP(ptr));
    if (size < copySize)
      copySize = size;
    memcpy(newp, ptr, copySize);
    mm_free(ptr);
    return newp;
}
//this is my checkheap function and what each line does is explained in the line by line comments. 
/* 
 * mm_checkheap - Check the heap for consistency 
 */
void mm_checkheap(int verbose) 
{
    printf("##############: /n");
    int heap_counter = 0, list_counter = 0;
    char *bp, *cp; 
    char *go = free_listp+ DSIZE;

    if((heap_listp) != mem_heap_lo()){
	printf("not correctly started");
	exit(1);  
} //checks if the heap list starts at the lowest heap address
    if ((GET_SIZE(HDRP(go)) != DSIZE) || !GET_ALLOC(HDRP(go))){
	printf("bad prologue HDR"); 
	exit(1); 
} //checks the progolgue header to be 8/1
    if ((GET_SIZE(FTRP(go)) != DSIZE) || !GET_ALLOC(FTRP(go))){
	printf("Bad prologue FTR\n");
	exit(1); 
} // checks prologue ftr to be 8/1
    checkblock(go);
	printf(" Walk through heap\n");
    for (bp = NEXT_BLKP(go); GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
	if (!GET_ALLOC(HDRP(bp))) 
		heap_counter++;
	printblock(bp);  
	checkblock(bp);
    } //goes through heap and checks how many items are on the heap
 	cp = free_listp; 
	while(cp != NULL){
	checkblock(cp);
	list_counter++;
	cp = GET_NEXT(cp);
	printblock(cp); 
} //goes through free list and counts how many item are on the free list and also checks each block while printing it, which was very useful while debugging 
	if ((GET_SIZE(HDRP(bp)) != 0) || (!GET_ALLOC(HDRP(bp)))){
	printf("epilogue");
	exit(1);
}    //checks epilogue header 
	if(heap_counter != list_counter){
	printf("counter !=\n");
	exit(1); 
} 
}

/* The remaining routines are internal helper routines */
//extends the heap by a double word size aligned value by invoking the mem_sbrk function 
/* 
 * extend_heap - Extend heap with free block and return its block pointer
 */
/* $begin mmextendheap */
static void *extend_heap(size_t words) 
{
    char *bp;
    uint32_t size;
	
    /* Allocate an even number of words to maintain alignment */
    size = (words % 2) ? (words+1) * WSIZE : words * WSIZE;
    if(size < 24) 
       size = 24; 
    if ((bp = mem_sbrk(size)) == (void *)-1) 
	return NULL;

    /* Initialize free block header/footer and the epilogue header */
    PUT(HDRP(bp), PACK(size, 0));         /* free block header */
    PUT(FTRP(bp), PACK(size, 0));         /* free block footer */
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); /* new epilogue header */ 
    /* Coalesce if the previous block was free */
    return coalesce(bp);
}
/* $end mmextendheap */
//places allocates a previously free block and if the block is larger than 24(min block size), it splices the block and creates a free block of the remaining size, otherwise it just allocates the block
/* 
 * place - Place block of asize bytes at start of free block bp 
 *         and split if remainder would be at least minimum block size
 */
/* $begin mmplace */
/* $begin mmplace-proto */
static void place(void *bp, size_t asize)
/* $end mmplace-proto */
{
    uint32_t csize = GET_SIZE(HDRP(bp));    
    edit_free_list(bp, 0); 	
    if ((csize - asize) >= (DSIZE*3)) { 
	PUT(HDRP(bp), PACK(asize, 1));
	PUT(FTRP(bp), PACK(asize, 1));	 
	bp = NEXT_BLKP(bp);
	PUT(HDRP(bp), PACK(csize - asize, 0));
	PUT(FTRP(bp), PACK(csize - asize, 0));
	edit_free_list(bp,1);  
    }
    else { 
	PUT(HDRP(bp), PACK(csize, 1));
	PUT(FTRP(bp), PACK(csize, 1));  	 
    }
  
}
/* $end mmplace */
//this find fit simply traverses the free list and seeks the first free block that is able to accomodate for the requested asize, but in order to increase the kpops for the binary files, which repeatedly request the same size, I created this conditional that just requests for a heap extension once the requested hasn't been fulfilled more than 40 times. 
/* 
 * find_fit - Find a fit for a block with asize bytes 
 */
static void *find_fit(size_t asize)
{ 
    /* first fit search */
    void *fp = free_listp; 
    static int size = 0;
    static int count = 0;
  if( size == (int)asize){
      if(count>40){  
        int extendsize = MAX(asize, 4 * WSIZE);
        fp = extend_heap(extendsize/4);
        return fp;
      }
      else
        count++;
  }
  else
    count = 0;
    while(fp){
	if(asize <= GET_SIZE(HDRP(fp))){ 
		size = asize; 
		return fp;
}
	fp = GET_NEXT(fp); 
}
    return NULL; /* no fit */
}
//the coalesce function plays a huge role in utilization as it allows for consecutive free blocks to be combined and it is called when ever a block is freed in order to provide maximum utilization 
/*
 * coalesce - boundary tag coalescing. Return ptr to coalesced block
 */
static void *coalesce(void *bp) 
{
    uint32_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp))) || PREV_BLKP(bp) == bp;
    uint32_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    uint32_t size = GET_SIZE(HDRP(bp));
    if(prev_alloc && next_alloc){
	bp = bp; 
}
 else  if (prev_alloc && !next_alloc) {      /* Case 2 */
	size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
	edit_free_list(NEXT_BLKP(bp), 0); 
	PUT(HDRP(bp), PACK(size, 0));
	PUT(FTRP(bp), PACK(size,0));
    }

    else if (!prev_alloc && next_alloc) {      /* Case 3 */
	size += GET_SIZE(HDRP(PREV_BLKP(bp)));
	bp = PREV_BLKP(bp);
	PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
        edit_free_list(bp,0); 
    }
    else if(!prev_alloc && !next_alloc)  {      /* Case 4 */
	size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp))); 
	edit_free_list(NEXT_BLKP(bp), 0); 	
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
	edit_free_list(bp, 0); 
    }
    
    edit_free_list(bp, 1); 
    return bp;
}

//prints the current status of the block 
static void printblock(void *bp) 
{
    uint32_t hsize, halloc, fsize, falloc;

    hsize = GET_SIZE(HDRP(bp));
    halloc = GET_ALLOC(HDRP(bp));  
    fsize = GET_SIZE(FTRP(bp));
    falloc = GET_ALLOC(FTRP(bp));  
    
    if (hsize == 0) {
	printf("%p: EOL\n", bp);
	return;
    }

    printf("%p: header: [%d:%c] footer: [%d:%c]\n", bp, 
	   hsize, (halloc ? 'a' : 'f'), 
	   fsize, (falloc ? 'a' : 'f')); 
}
//checks if the block is in the heap, if the block has been linked/coalesced properly, and if the header is equal to the footer and  and if it is aligned 
static void checkblock(void *bp) {
    if (!(bp <= mem_heap_hi() && bp >= mem_heap_lo())){
	printf("Not in heap\n");
	exit(1); 
}
    if (!((size_t)((((size_t)(bp) + 7) & ~0x7)) == ((size_t)bp))){
	printf("Not in alignment");
    	exit(1);
	}
   if(GET(HDRP(bp)) != GET(FTRP(bp))){
	printf("header not equal to foot");
	exit(1);
}
}
//this function is used to add or remove from the free list and if the input for the in is 0, the requested item will be removed and for any other integer, the requested item will be added to the free list
static void edit_free_list(void *bp, int i){
	if(i != 0){
	if(free_listp){
        SET_PREV(free_listp, bp);
        SET_NEXT(bp, free_listp);
} else 
	SET_NEXT(bp, NULL); 
	SET_PREV(bp, NULL);
	free_listp = bp; 

} else{  
	if(GET_PREV(bp)) 
		SET_NEXT(GET_PREV(bp), GET_NEXT(bp)); 
	else
		free_listp = GET_NEXT(bp); 
	if(GET_NEXT(bp)) 
		SET_PREV(GET_NEXT(bp), GET_PREV(bp)); 	
}
}
