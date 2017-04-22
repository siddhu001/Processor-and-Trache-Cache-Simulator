/*
 * cache.c
 */

#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "cache.h"
#include "main.h"

/* cache configuration parameters */
static int cache_split = 0;
static int cache_usize = DEFAULT_CACHE_SIZE;
static int cache_isize = DEFAULT_CACHE_SIZE; 
static int cache_dsize = DEFAULT_CACHE_SIZE;
static int cache_block_size = DEFAULT_CACHE_BLOCK_SIZE;
static int words_per_block = DEFAULT_CACHE_BLOCK_SIZE / WORD_SIZE;
static int cache_assoc = DEFAULT_CACHE_ASSOC;
static int cache_writeback = DEFAULT_CACHE_WRITEBACK;
static int cache_writealloc = DEFAULT_CACHE_WRITEALLOC;

/* cache model data structures */
static cache cache1;
static cache cache2;
static Pcache cachePtr;
static cache_stat cache1_stat;
static cache_stat cache2_stat;
static Pcache_stat cacheStatPtr;

/************************************************************/
void set_cache_param(param, value)
  int param;
  int value;
{

  switch (param) {
  case CACHE_PARAM_BLOCK_SIZE:
    cache_block_size = value;
    words_per_block = value / WORD_SIZE;
    break;
  case CACHE_PARAM_USIZE:
    cache_split = FALSE;
    cache_usize = value;
    break;
  case CACHE_PARAM_ISIZE:
    cache_split = TRUE;
    cache_isize = value;
    break;
  case CACHE_PARAM_DSIZE:
    cache_split = TRUE;
    cache_dsize = value;
    break;
  case CACHE_PARAM_ASSOC:
    cache_assoc = value;
    break;
  case CACHE_PARAM_WRITEBACK:
    cache_writeback = TRUE;
    break;
  case CACHE_PARAM_WRITETHROUGH:
    cache_writeback = FALSE;
    break;
  case CACHE_PARAM_WRITEALLOC:
    cache_writealloc = TRUE;
    break;
  case CACHE_PARAM_NOWRITEALLOC:
    cache_writealloc = FALSE;
    break;
  default:
    printf("error set_cache_param: bad parameter value\n");
    exit(-1);
  }

}
/************************************************************/

int ceilDiv (int a, int b){
	if (a%b==0) return a/b ;
	else return (1+ a/b);
}

/************************************************************/
void init_cache()
{
	if (cache_split == 0) cache1.size = (cache_usize)/WORD_SIZE; else   cache1.size = (cache_dsize)/WORD_SIZE; 
	cache1.associativity = cache_assoc;
	cache1.n_sets = ceilDiv(ceilDiv(cache1.size*WORD_SIZE, cache_block_size), cache_assoc);
	int numOfIndexBits = LOG2(cache1.n_sets);
	int blockOffsetBits = LOG2(cache_block_size);
	cache1.index_mask_offset = blockOffsetBits ;
	unsigned p=0;
	int i;
	for (i=0; i< numOfIndexBits; i++) p = p*2 + 1;
	p = p << cache1.index_mask_offset;
	cache1.index_mask = p;

	cache1.LRU_head = (Pcache_line *)malloc(sizeof(Pcache_line)*cache1.n_sets);
	cache1.LRU_tail = (Pcache_line *)malloc(sizeof(Pcache_line)*cache1.n_sets);
	cache1.set_contents = (int*) malloc(sizeof(int)*cache1.n_sets);
	for (i=0; i< cache1.n_sets; i++) {
		cache1.LRU_head[i] =  cache1.LRU_tail[i] = NULL;
		cache1.set_contents[i] = 0;
	}
	cache1.contents = 0;

	// printf("Cache Variables\n");
	// printf("ass: %d nsets:%d maskOffset:%d indexBits: %d mask:%x \n",cache1.associativity, cache1.n_sets,cache1.index_mask_offset,numOfIndexBits ,cache1.index_mask );

	if (cache_split==1) {
		cache2.size = (cache_isize)/WORD_SIZE;
		cache2.associativity = cache_assoc;
		cache2.n_sets = ceilDiv(ceilDiv(cache2.size*WORD_SIZE, cache_block_size), cache_assoc);
		
		int blockOffsetBits = LOG2(cache_block_size);
		cache2.index_mask_offset = blockOffsetBits ; // 2 for byte offset in a word.
		
		int numOfIndexBits2 = LOG2(cache2.n_sets);
		
		unsigned p2=0;
		for (i=0; i< numOfIndexBits2; i++) p2 = p2*2 + 1;
		for (i=0; i< cache2.index_mask_offset; i++) p2 = p2 << 1;
		cache2.index_mask = p2;

		cache2.LRU_head = (Pcache_line *)malloc(sizeof(Pcache_line)*cache2.n_sets);
		cache2.LRU_tail = (Pcache_line *)malloc(sizeof(Pcache_line)*cache2.n_sets);
		cache2.set_contents = (int*)malloc(sizeof(int)*cache2.n_sets);
		for (i=0; i< cache2.n_sets; i++) {
			cache2.LRU_head[i] =  cache2.LRU_tail[i] = NULL;
			cache2.set_contents[i] = 0;
		}
		cache2.contents = 0;
	}

	cache1_stat.accesses=0;			/* number of memory references */
	cache1_stat.misses=0;			/* number of cache misses */
	cache1_stat.replacements=0;		/* number of misses that cause replacments */
	cache1_stat.demand_fetches=0;		/* number of fetches */
	cache1_stat.copies_back=0;    /* number of write backs */
	cache2_stat.accesses=0;			/* number of memory references */
	cache2_stat.misses=0;			/* number of cache misses */
	cache2_stat.replacements=0;		/* number of misses that cause replacments */
	cache2_stat.demand_fetches=0;		/* number of fetches */
	cache2_stat.copies_back=0;    /* number of write backs */
}

/************************************************************/

/************************************************************/

void perform_access(addr, access_type)
  unsigned addr, access_type;
{
int wordsInBlock = (cache_block_size/WORD_SIZE);
  /* handle an access to the cache */
	if (access_type == TRACE_DATA_LOAD){ //data read
		cache1_stat.accesses ++;
		int miss = 0; // 0-> not set; 1 -> miss; 2->hit
		unsigned idx= (addr & cache1.index_mask) >> cache1.index_mask_offset;
		unsigned mytag = addr >> cache1.index_mask_offset;
		
		if (idx > cache1.n_sets) {
			printf("$Error\n");
			return;
		}
		
		if (cache1.LRU_head[idx] == NULL) { // empty set
			// printf("Empty\n");
			miss = 1;

			Pcache_line newBlock = (Pcache_line) malloc(sizeof(cache_line));
			newBlock->tag = mytag;
			newBlock->dirty = 0; 
			// newBlock -> data = mem[] ; $
			insert(&cache1.LRU_head[idx],&cache1.LRU_tail[idx],newBlock);
		
			cache1_stat.misses ++;
			cache1_stat.demand_fetches++;

			cache1.contents ++;			
			cache1.set_contents[idx] = 1;
		}
		else {
			Pcache_line ptr = cache1.LRU_head[idx];
			while (ptr !=  NULL){
				if (ptr -> tag == mytag){ // hit
					//reached correct block
					miss = 2;
					// $ return data from cache
					// to maintain cache replacement policy
					// printf("hit\n");
					delete( &cache1.LRU_head[idx], &cache1.LRU_tail[idx],ptr );
					insert( &cache1.LRU_head[idx], &cache1.LRU_tail[idx],ptr );
					break;
				}
				ptr= ptr -> LRU_next;
			}
			
			if (miss != 2) { // miss
				miss = 1;
				Pcache_line newBlock = (Pcache_line) malloc(sizeof(cache_line));
				newBlock->tag = mytag;
				newBlock->dirty = 0; 
				if (cache1.set_contents[idx] == cache1.associativity) { // replace
					// printf("Miss with replace\n");
					// remove the last element in the cache line
					if (cache1.LRU_tail[idx]->dirty==1){
						// update the contents in memory
						cache1_stat.copies_back+= wordsInBlock;
					}
					delete(&cache1.LRU_head[idx],&cache1.LRU_tail[idx],cache1.LRU_tail[idx]);

					// $ add data
					insert(&cache1.LRU_head[idx],&cache1.LRU_tail[idx],newBlock);

					cache1_stat.misses++;
					cache1_stat.replacements++;
					cache1_stat.demand_fetches++;
				}
				else{ // insert 
					// printf("Miss with insert\n");
					// $ add data
					insert(&cache1.LRU_head[idx],&cache1.LRU_tail[idx],newBlock);					
					cache1_stat.misses++;
					cache1_stat.demand_fetches++;
					cache1.set_contents[idx]++;
					cache1.contents++;
				}
			}
		}
	}
	
	else if (access_type == TRACE_DATA_STORE){ // data store
		cache1_stat.accesses ++;
		
		int miss = 1;
		unsigned idx= (addr & cache1.index_mask) >> cache1.index_mask_offset; 
		unsigned mytag = addr >> cache1.index_mask_offset;
		
		Pcache_line ptr = cache1.LRU_head[idx];
		while (ptr!=NULL){
			if (ptr -> tag == mytag){ // hit
				miss = 2;
				break;
			}
			ptr= ptr -> LRU_next;
		}
		if (miss == 1){ // miss
			if (cache_writealloc == 1){
				// $change in memory content
				if (cache_writeback==1){
					Pcache_line newBlock = (Pcache_line) malloc(sizeof(cache_line));
					newBlock->tag = mytag;
					newBlock->dirty = 1;

					if (cache1.set_contents[idx] == cache1.associativity) { // replace
						// printf("miss: writealloc with replace %d %d\n", cache1.associativity,cache1.set_contents[idx] );
						if (cache1.LRU_tail[idx]->dirty == 1){
							// write in the memory to reflect the changes, tail will contain the data						
							cache1_stat.copies_back+=wordsInBlock;

						}

						delete(&cache1.LRU_head[idx],&cache1.LRU_tail[idx],cache1.LRU_tail[idx]);
						insert(&cache1.LRU_head[idx],&cache1.LRU_tail[idx],newBlock);
						
						cache1_stat.misses++;
						cache1_stat.replacements++;
						cache1_stat.demand_fetches++;
					}
					else{ // insert 
						// printf("miss: writealloc with insert\n");
						
						insert(&cache1.LRU_head[idx],&cache1.LRU_tail[idx],newBlock);
						cache1.set_contents[idx]++;
						cache1.contents++;
						
						cache1_stat.misses++;
						cache1_stat.demand_fetches++;
					}		
				}
				else { /*write allocate with no writeback in miss*/
					// printf("write miss wa wthrough\n");
					Pcache_line newBlock = (Pcache_line) malloc(sizeof(cache_line));
					newBlock->tag = mytag;
					newBlock->dirty = 0;

					if (cache1.set_contents[idx] == cache1.associativity) { // replace
						// printf("miss: writealloc with replace %d %d\n", cache1.associativity,cache1.set_contents[idx] );
						delete(&cache1.LRU_head[idx],&cache1.LRU_tail[idx],cache1.LRU_tail[idx]);
						insert(&cache1.LRU_head[idx],&cache1.LRU_tail[idx],newBlock);
						
						cache1_stat.misses++;
						cache1_stat.replacements++;
						cache1_stat.demand_fetches++;
						cache1_stat.copies_back++;
					}
					else{ // insert 
						// printf("miss: writealloc with insert\n");
						
						insert(&cache1.LRU_head[idx],&cache1.LRU_tail[idx],newBlock);
						cache1.set_contents[idx]++;
						cache1.contents++;
						
						cache1_stat.misses++;
						cache1_stat.demand_fetches++;
						cache1_stat.copies_back++;
					}	

				}
			}

			else { // no write allocate
				if (cache_writeback==1){
					cache1_stat.misses++;
					cache1_stat.copies_back++;
				}
				else{ // with no write back
					cache1_stat.misses++;
					cache1_stat.copies_back++;	
				}
			}
		}
		else { // hit
			// printf("hit\n");
			// for block replacement policy
			delete( &cache1.LRU_head[idx], &cache1.LRU_tail[idx],ptr );
			insert( &cache1.LRU_head[idx], &cache1.LRU_tail[idx],ptr );
					
			if (cache_writeback == 1){
				// $change the contents in cache
				ptr -> dirty = 1;
			}
			else { //write through
				 // $change contents in cache and memory
				cache1_stat.copies_back++ ;
			}
		}
	}

	else { //instruction read
		if (cache_split == 0){ cachePtr = (&cache1);} else cachePtr = (&cache2);
		if (cache_split == 0){ cacheStatPtr = (&cache1_stat);} else cacheStatPtr = (&cache2_stat);
		
		(cache2_stat.accesses)++;
		int miss = 0; // 0-> not set; 1 -> miss; 2->hit
		unsigned idx= (addr & cachePtr->index_mask) >> (cachePtr->index_mask_offset);
		// printf("%x %x %x %x\n",addr, cachePtr->index_mask,(addr & cachePtr->index_mask), idx);
		unsigned mytag = addr >> (cachePtr->index_mask_offset);
		// printf("idx:%x mytag:%x\n",idx, mytag );
		if (idx > cachePtr->n_sets) {
			printf("$Error\n");
			return;
		}
		
		if (cachePtr->LRU_head[idx] == NULL) { // empty set
			// printf("Empty\n");
			miss = 1;
			Pcache_line newBlock = (Pcache_line) malloc(sizeof(cache_line));
			newBlock->tag = mytag;
			newBlock->dirty = 0; 
			// newBlock -> data = mem[] ; $
			insert(&(cachePtr->LRU_head[idx]),&(cachePtr->LRU_tail[idx]),newBlock);
			
			(cache2_stat.misses) ++;
			(cacheStatPtr->demand_fetches)++;

			(cachePtr->contents) ++;			
			cachePtr->set_contents[idx] = 1;
		}
		else {
			Pcache_line ptr = cachePtr->LRU_head[idx];
			while (ptr !=  NULL){
				if (ptr -> tag == mytag){ // hit
					//reached correct block
					miss = 2;
					// $ return data from cache
					// to maintain cache replacement policy
					// printf("hit\n");
					delete( &cachePtr->LRU_head[idx], &cachePtr->LRU_tail[idx],ptr );
					insert( &cachePtr->LRU_head[idx], &cachePtr->LRU_tail[idx],ptr );
					break;
				}
				ptr= ptr -> LRU_next;
			}
			
			if (miss != 2) { // miss
				miss = 1;
				if ((cachePtr->set_contents)[idx] == cachePtr->associativity) { // replace
					// printf("Miss with replace\n");
					// remove the last element in the cache line
					if ((cachePtr->LRU_tail)[idx]->dirty==1){
						// update the contents in memory
						(cacheStatPtr->copies_back)+=wordsInBlock;
					}
					delete(&(cachePtr->LRU_head[idx]),&(cachePtr->LRU_tail[idx]),cachePtr->LRU_tail[idx]);

					Pcache_line newBlock = (Pcache_line) malloc(sizeof(cache_line));
					newBlock->tag = mytag;
					newBlock->dirty = 0; 
					// $ add data
					insert(&(cachePtr->LRU_head[idx]),&(cachePtr->LRU_tail[idx]),newBlock);

					
					cache2_stat.misses++;
					cache2_stat.replacements++;
					cacheStatPtr->demand_fetches++;
				}
				else{ // insert 
					// printf("Miss with insert\n");

					Pcache_line newBlock = (Pcache_line) malloc(sizeof(cache_line));
					newBlock->tag = mytag;
					newBlock->dirty = 0; 
					// $ add data
					insert(&(cachePtr->LRU_head[idx]),&(cachePtr->LRU_tail[idx]),newBlock);
					
					cache2_stat.misses++;
					cacheStatPtr->demand_fetches++;
					cachePtr->set_contents[idx]++;
					cachePtr->contents++;
				}
			}
		}
	}
	// printf("..%d %d\n", cache1_stat.copies_back, cache2_stat.copies_back);

}
/************************************************************/

/************************************************************/

void flush()
{
	int i;
	for (i=0; i< cache1.n_sets; i++){
		while (cache1.LRU_head[i] !=NULL){
			if (cache1.LRU_tail[i]->dirty == 1){
				// reflect the changes in memory
				cache1_stat.copies_back+=(cache_block_size/WORD_SIZE);
				cache1.set_contents[i]--;
				cache1.contents--;
			}
			delete(&cache1.LRU_head[i], &cache1.LRU_tail[i],cache1.LRU_tail[i]);
		}
	}

	if (cache_split == 1){
	for (i=0; i< cache2.n_sets; i++){
			while (cache2.LRU_head[i] !=NULL){
				if (cache2.LRU_tail[i]->dirty == 1){
					// reflect the changes in meme
					cache2_stat.copies_back+= (cache_block_size/WORD_SIZE);
					cache2.set_contents[i]--;
					cache2.contents--;
				}
				delete(&cache2.LRU_head[i], &cache2.LRU_tail[i],cache2.LRU_tail[i]);
			}
		}
	}
}

  /* flush the cache */
/************************************************************/

/************************************************************/
void delete(head, tail, item)
  Pcache_line *head, *tail;
  Pcache_line item;
{
  if (item->LRU_prev) {
    item->LRU_prev->LRU_next = item->LRU_next;
  } else {
    /* item at head */
    *head = item->LRU_next;
  }

  if (item->LRU_next) {
    item->LRU_next->LRU_prev = item->LRU_prev;
  } else {
    /* item at tail */
    *tail = item->LRU_prev;
  }
}
/************************************************************/

/************************************************************/
/* inserts at the head of the list */
void insert(head, tail, item)
  Pcache_line *head, *tail;
  Pcache_line item;
{
  item->LRU_next = *head;
  item->LRU_prev = (Pcache_line)NULL;

  if (item->LRU_next)
    item->LRU_next->LRU_prev = item;
  else
    *tail = item;

  *head = item;
}
/************************************************************/

/************************************************************/
void dump_settings()
{
  printf("*** CACHE SETTINGS ***\n");
  if (cache_split) {
    printf("  Split I- D-cache\n");
    printf("  I-cache size: \t%d\n", cache_isize);
    printf("  D-cache size: \t%d\n", cache_dsize);
  } else {
    printf("  Unified I- D-cache\n");
    printf("  Size: \t%d\n", cache_usize);
  }
  printf("  Associativity: \t%d\n", cache_assoc);
  printf("  Block size: \t%d\n", cache_block_size);
  printf("  Write policy: \t%s\n", 
	 cache_writeback ? "WRITE BACK" : "WRITE THROUGH");
  printf("  Allocation policy: \t%s\n",
	 cache_writealloc ? "WRITE ALLOCATE" : "WRITE NO ALLOCATE");
}
/************************************************************/

/************************************************************/
void print_stats()
{
  printf("\n*** CACHE STATISTICS ***\n");

  printf(" INSTRUCTIONS\n");
  printf("  accesses:  %d\n", cache2_stat.accesses);
  printf("  misses:    %d\n", cache2_stat.misses);
  if (!cache2_stat.accesses)
    printf("  miss rate: 0 (0)\n"); 
  else
    printf("  miss rate: %2.4f (hit rate %2.4f)\n", 
	 (float)cache2_stat.misses / (float)cache2_stat.accesses,
	 1.0 - (float)cache2_stat.misses / (float)cache2_stat.accesses);
  printf("  replace:   %d\n", cache2_stat.replacements);

  printf(" DATA\n");
  printf("  accesses:  %d\n", cache1_stat.accesses);
  printf("  misses:    %d\n", cache1_stat.misses);
  if (!cache1_stat.accesses)
    printf("  miss rate: 0 (0)\n"); 
  else
    printf("  miss rate: %2.4f (hit rate %2.4f)\n", 
	 (float)cache1_stat.misses / (float)cache1_stat.accesses,
	 1.0 - (float)cache1_stat.misses / (float)cache1_stat.accesses);
  printf("  replace:   %d\n", cache1_stat.replacements);

// static int cache_block_size = DEFAULT_CACHE_BLOCK_SIZE;
// static int words_per_block = DEFAULT_CACHE_BLOCK_SIZE / WORD_SIZE;

  printf(" TRAFFIC (in words)\n");
  printf("  demand fetch:  %d\n", ((cache2_stat.demand_fetches) + 
  	 cache1_stat.demand_fetches)*(cache_block_size/WORD_SIZE));
  printf("  copies back:   %d\n", (cache2_stat.copies_back +
  	 cache1_stat.copies_back));
}
/************************************************************/
