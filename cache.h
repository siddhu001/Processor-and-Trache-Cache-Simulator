/*
 * cache.c
 */
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#define WORD_SIZE 4
#define WORD_SIZE_OFFSET 2
#define DEFAULT_CACHE_SIZE (8 * 1024)
#define DEFAULT_CACHE_BLOCK_SIZE 16
#define DEFAULT_CACHE_ASSOC 1
#define DEFAULT_CACHE_WRITEBACK TRUE
#define DEFAULT_CACHE_WRITEALLOC TRUE
#define DEFAULT_FREQ 2
#define DEFAULT_LATENCY 45

#define TRACE_DATA_LOAD 0
#define TRACE_DATA_STORE 1
#define TRACE_INST_LOAD 2

/* structure definitions */
typedef struct cache_line_ {
  unsigned tag;
  int dirty;

  struct cache_line_ *LRU_next;
  struct cache_line_ *LRU_prev;
} cache_line, *Pcache_line;

typedef struct cache_ {
  int perfect;
  int size;     /* cache size in words */
  int associativity;    /* cache associativity */
  int replacementPolicy;    /* cache associativity */
  int writeback;
  int block_size; // in bytes

  int writealloc; // 0 -> no ; 1 -> yes
  int n_sets;     /* number of cache sets */
  unsigned index_mask;    /* mask to find cache index */
  int index_mask_offset;  /* number of zero bits in mask */
  Pcache_line *LRU_head;  /* head of LRU list for each set */
  Pcache_line *LRU_tail;  /* tail of LRU list for each set */
  int *set_contents;    /* number of valid entries in set */
  int contents;     /* number of valid entries in cache */
} cache, *Pcache;

typedef struct cache_stat_ {
  int accesses; /* number of memory references */
  int misses;     /* number of cache misses */
  int replacements;   /* number of misses that cause replacments */
  int demand_fetches;      /* number of fetches*/
  int copies_back;   /* number of write backs */
} cache_stat, *Pcache_stat;


void set_par_cache(int cacheNum, int param, int value);
void set_frequency(double value);
void set_latency(int value);
void configCache();
void perform_access(unsigned, unsigned);
void flush();
void printResultsCache(FILE*,int, int, int, int, int);

/* macros */
#define LOG2(x) ((int) rint((log((double) (x))) / (log(2.0))))

/* cache model data structures */
static cache cache1;
static cache cache2;
static Pcache cachePtr;
static cache_stat cache1_stat;
static cache_stat cache2_stat;
static Pcache_stat cacheStatPtr;
double freq = DEFAULT_FREQ;
int latency = DEFAULT_LATENCY;
static int numOfRamAccess= 0;

#define TRUE 1
#define FALSE 0

/* default cache parameters--can be changed */
// cache1.set_contents[idx]
// cache1.contents

void printResultsCache(FILE* resfp1, int numOfCycles ,int idleCycles,int instAccess, int excessCount, int memAccess){
	//numOfRamAccess = (cache2_stat.demand_fetches)*(cache2.block_size/WORD_SIZE) +  (cache1_stat.demand_fetches)*(cache2.block_size/WORD_SIZE) + (cache2_stat.copies_back + cache1_stat.copies_back);
	numOfRamAccess = cache1_stat.accesses + cache2_stat.accesses;
	double time = (numOfCycles / freq) + ((cache1_stat.misses + cache2_stat.misses)*latency);
	double idleTime= (idleCycles/freq) + ((cache1_stat.misses + cache2_stat.misses)*latency);
	int numOfAccess= instAccess - excessCount + memAccess;
	int miss = cache1_stat.misses + cache2_stat.misses ; 
	
	double avgLatency=  ( (float)(miss*latency) + ((numOfAccess-miss)/freq) )/numOfRamAccess ; 
	
	fprintf(resfp1,"Time (ns),%.4f\n",time);
	fprintf(resfp1,"Idle time (ns),%.4f\n",idleTime);
	fprintf(resfp1,"Idle time (%%),%.4f%%\n", (idleTime*100.0) /time);

	fprintf(resfp1,"Cache Summary\nCache L1-I\n");
	if (cache2.perfect ==1){
		fprintf(resfp1,"num cache accesses,%d\nnum cache misses,0\nmiss rate,0%%\n",instAccess- excessCount);
	}
	else {
		fprintf(resfp1,"num cache accesses,%d\nnum cache misses,%d\nmiss rate,%.4f%%\n",instAccess- excessCount,cache2_stat.misses,(cache2_stat.misses*100.0)/cache2_stat.accesses);
	}
	
	fprintf(resfp1,"Cache Summary\nCache L1-D\n");
	if (cache1.perfect == 1){
		fprintf(resfp1,"num cache accesses,%d\nnum cache misses,0\nmiss rate,0%%\n",memAccess);
	}
	else {
		fprintf(resfp1,"num cache accesses,%d\nnum cache misses,%d\nmiss rate,%.4f%%\n",memAccess, cache1_stat.misses,((cache1_stat.misses*100.0)/cache1_stat.accesses));
	}
 	fprintf(resfp1, "DRAM summary\nnum dram accesses,%d\naverage dram access latency (ns),%.3f",numOfRamAccess, avgLatency); 
}


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
int ceilDiv (int a, int b){
	if (a%b==0) return a/b ;
	else return (1+ a/b);
}

void initialiseCache(){
	cache1.perfect = 0;
	cache1.size = DEFAULT_CACHE_SIZE/WORD_SIZE;			/* cache size in words */
	cache1.associativity= DEFAULT_CACHE_ASSOC;		/* cache associativity */
	cache1.replacementPolicy= 0;		/* cache associativity */
	cache1.writeback = DEFAULT_CACHE_WRITEBACK;
	cache1.block_size = DEFAULT_CACHE_BLOCK_SIZE;		// in bytes
	cache1.writealloc = DEFAULT_CACHE_WRITEALLOC; // 0 -> no ; 1 -> yes

	cache2.perfect = 0;
	cache2.size = DEFAULT_CACHE_SIZE/WORD_SIZE;			/* cache size in words */
	cache2.associativity= DEFAULT_CACHE_ASSOC;		/* cache associativity */
	cache2.replacementPolicy= 0;		/* cache associativity */
	cache2.writeback = DEFAULT_CACHE_WRITEBACK;
	cache2.block_size = DEFAULT_CACHE_BLOCK_SIZE;		// in bytes
	cache2.writealloc = DEFAULT_CACHE_WRITEALLOC; // 0 -> no ; 1 -> yes


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

void set_par_cache(int cacheNum, int param, int value)
{
	switch (param) {
	  case 1:
	    if (cacheNum==1){
	    	cache1.perfect = value;	
	    }
	    else {
	    	cache2.perfect = value;
	    }
	    break;
	  case 2:
	   	if (cacheNum==1){
	    	cache1.size = (value*1024)/4;	// in words
	    }
	    else {
	    	cache2.size = (value*1024)/4; // in words
	    }
	    break;
	  case 3:
	   if (cacheNum==1){
	    	cache1.associativity = value;	
	    }
	    else {
	    	cache2.associativity = value;
	    }
	    break;
	  case 4:
	   	if (cacheNum==1){
	    	cache1.replacementPolicy = value;	
	    }
	    else {
	    	cache2.replacementPolicy = value;
	    }
	    break;
	  case 5:
	 	if (cacheNum==1){
	    	cache1.writeback = 1-value;	
	    }
	    else {
	    	cache2.writeback = 1- value;
	    }
	    break;
	  case 6:
	 	if (cacheNum==1){
	    	cache1.block_size = value;	
	    }
	    else {
	    	cache2.block_size = value;
	    }
	    break;
	   default:
	    printf("error set_cache_param: bad parameter value %d %d %d\n", cacheNum, param, value);
	    exit(-1);
  }
}

void set_frequency(double value){
	freq = value;
}

void set_latency(int value){
	latency = value;
}

int isPowerOfTwo(int n){
	while(1){
		if (n == 1||n==0) return 1;
		if (n%2) return 0;
		n/=2;
	}
}

void validate(){
	if ((cache1.size)%WORD_SIZE) {
		printf("Cache size should be divisible by word size.\n");
		exit(-1);
	}
	if (cache1.size*WORD_SIZE %(cache1.block_size*cache1.associativity)) {
		printf("Complete sets should be formed in Data Cache.\n");
		exit(-1);
	}
	if (isPowerOfTwo(cache1.n_sets)== 0 ){
		printf("The Number of Sets formed should be powers of two.\n");
		exit(-1);	
	}
	
	if (isPowerOfTwo(cache1.block_size)== 0 ){
		printf("%d\n",cache1.block_size);
		printf("The Block Size formed should be powers of two.\n");
		exit(-1);	
	}


	if ((cache2.size)%WORD_SIZE) {
		printf("Cache size should be divisible by word size.\n");
		exit(-1);
	}
	if (cache2.size*WORD_SIZE %(cache2.block_size*cache2.associativity)) {
		printf("Complete sets should be formed in Data Cache.\n");
		exit(-1);
	}
	if (isPowerOfTwo(cache2.n_sets)== 0 ){
		printf("The Number of Sets formed should be powers of two.\n");
		exit(-1);	
	}
	
	if (isPowerOfTwo(cache2.block_size)== 0 ){
		printf("%d\n",cache1.block_size);
		printf("The Block Size formed should be powers of two.\n");
		exit(-1);	
	}
	
}

void configCache()
{
	validate();
	cache1.n_sets = ceilDiv(ceilDiv((cache1.size)*WORD_SIZE, cache1.block_size),cache1.associativity);
	int numOfIndexBits = LOG2(cache1.n_sets);
	int blockOffsetBits = LOG2(cache1.block_size);
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

	cache2.n_sets = ceilDiv(ceilDiv((cache2.size)*WORD_SIZE, cache2.block_size),cache2.associativity);
	numOfIndexBits = LOG2(cache2.n_sets);
	blockOffsetBits = LOG2(cache2.block_size);
	cache2.index_mask_offset = blockOffsetBits ;
	for (i=0; i< numOfIndexBits; i++) p = p*2 + 1;
	p = p << cache2.index_mask_offset;
	cache2.index_mask = p;

	cache2.LRU_head = (Pcache_line *)malloc(sizeof(Pcache_line)*cache2.n_sets);
	cache2.LRU_tail = (Pcache_line *)malloc(sizeof(Pcache_line)*cache2.n_sets);
	cache2.set_contents = (int*) malloc(sizeof(int)*cache2.n_sets);
	for (i=0; i< cache2.n_sets; i++) {
		cache2.LRU_head[i] =  cache2.LRU_tail[i] = NULL;
		cache2.set_contents[i] = 0;
	}
	cache2.contents = 0;

	printf("Cache Configured:\n");
	printf("Cache1:\n");
	printf("perfect %d\n",cache1.perfect);
	printf("size %d\n",cache1.size);     /* cache size in words */
	printf("associativity %d\n",cache1.associativity);    /* cache associativity */
	printf("replacementPolicy %d\n",cache1.replacementPolicy);    /* cache associativity */
	printf("writeback %d\n",cache1.writeback);
	printf("block_size %d\n",cache1.block_size); // in bytes

	printf("writealloc %d\n",cache1.writealloc); // 0 -> no ; 1 -> yes
	printf("n_sets %d\n",cache1.n_sets);     /* number of cache sets */
	printf("index_mask_offset %d\n",cache1.index_mask_offset);    /*  number of cache sets*/ 	

	printf("Cache2:\n");
	printf("perfect %d\n",cache2.perfect);
	printf("size %d\n",cache2.size);     /* cache size in words */
	printf("associativity %d\n",cache2.associativity);    /* cache associativity */
	printf("replacementPolicy %d\n",cache2.replacementPolicy);    /* cache associativity */
	printf("writeback %d\n",cache2.writeback);
	printf("block_size %d\n",cache2.block_size); // in bytes

	printf("writealloc %d\n",cache2.writealloc); // 0 -> no ; 1 -> yes
	printf("n_sets %d\n",cache2.n_sets);     /* number of cache sets */
	printf("index_mask_offset %d\n",cache2.index_mask_offset);     /* number of cache sets */
	printf("freq:%f latency %d\n",freq, latency );

}

/************************************************************/

void perform_access(addr, access_type)
  unsigned addr, access_type;
{
  	/* handle an access to the cache */
  	printf("^^%d\n",access_type);
	if (access_type == TRACE_DATA_LOAD){ //data read
		int wordsInBlock = ((cache1.block_size)/WORD_SIZE);
		cache1_stat.accesses ++;
		printf("..%d\n",cache1_stat.accesses);
		int miss = 0; // 0-> not set; 1 -> miss; 2->hit
		unsigned idx= (addr & cache1.index_mask) >> cache1.index_mask_offset;
		unsigned mytag = addr >> cache1.index_mask_offset;
		
		if (idx > cache1.n_sets) {
			printf("Error\n");
			exit(0);
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
		printf("YEs\n");
		int wordsInBlock = ((cache1.block_size)/WORD_SIZE);
		cache1_stat.accesses ++;
		printf("=%d\n",cache1_stat.accesses );
		
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
			if (cache1.writealloc == 1){
				// $change in memory content
				if (cache1.writeback==1){
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
				if (cache1.writeback==1){
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
					
			if (cache1.writeback == 1){
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
		int wordsInBlock = ((cache1.block_size)/WORD_SIZE);

		cachePtr = (&cache2);
		cacheStatPtr = (&cache2_stat);
		
		(cache2_stat.accesses)++;
		printf("==> %d\n", cache2_stat.accesses);
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
				cache1_stat.copies_back+=(cache1.block_size/WORD_SIZE);
				cache1.set_contents[i]--;
				cache1.contents--;
			}
			delete(&cache1.LRU_head[i], &cache1.LRU_tail[i],cache1.LRU_tail[i]);
		}
	}

	for (i=0; i< cache2.n_sets; i++){
		while (cache2.LRU_head[i] !=NULL){
			if (cache2.LRU_tail[i]->dirty == 1){
				// reflect the changes in meme
				cache2_stat.copies_back+= (cache2.block_size/WORD_SIZE);
				cache2.set_contents[i]--;
				cache2.contents--;
			}
			delete(&cache2.LRU_head[i], &cache2.LRU_tail[i],cache2.LRU_tail[i]);
		}
	}
}

/************************************************************/

/************************************************************/

/************************************************************/

/************************************************************/
// void dump_settings()
// {
//   printf("*** CACHE SETTINGS ***\n");
//   if (cache_split) {
//     printf("  Split I- D-cache\n");
//     printf("  I-cache size: \t%d\n", cache_isize);
//     printf("  D-cache size: \t%d\n", cache_dsize);
//   } else {
//     printf("  Unified I- D-cache\n");
//     printf("  Size: \t%d\n", cache_usize);
//   }
//   printf("  Associativity: \t%d\n", cache_assoc);
//   printf("  Block size: \t%d\n", cache_block_size);
//   printf("  Write policy: \t%s\n", 
// 	 cache_writeback ? "WRITE BACK" : "WRITE THROUGH");
//   printf("  Allocation policy: \t%s\n",
// 	 cache_writealloc ? "WRITE ALLOCATE" : "WRITE NO ALLOCATE");
// }
/************************************************************/

/************************************************************/
// void print_stats()
// {
//   printf("\n*** CACHE STATISTICS ***\n");

//   printf(" INSTRUCTIONS\n");
//   printf("  accesses:  %d\n", cache2_stat.accesses);
//   printf("  misses:    %d\n", cache2_stat.misses);
//   if (!cache2_stat.accesses)
//     printf("  miss rate: 0 (0)\n"); 
//   else
//     printf("  miss rate: %2.4f (hit rate %2.4f)\n", 
// 	 (float)cache2_stat.misses / (float)cache2_stat.accesses,
// 	 1.0 - (float)cache2_stat.misses / (float)cache2_stat.accesses);
//   printf("  replace:   %d\n", cache2_stat.replacements);

//   printf(" DATA\n");
//   printf("  accesses:  %d\n", cache1_stat.accesses);
//   printf("  misses:    %d\n", cache1_stat.misses);
//   if (!cache1_stat.accesses)
//     printf("  miss rate: 0 (0)\n"); 
//   else
//     printf("  miss rate: %2.4f (hit rate %2.4f)\n", 
// 	 (float)cache1_stat.misses / (float)cache1_stat.accesses,
// 	 1.0 - (float)cache1_stat.misses / (float)cache1_stat.accesses);
//   printf("  replace:   %d\n", cache1_stat.replacements);

// 	// static int cache_block_size = DEFAULT_CACHE_BLOCK_SIZE;
// 	// static int words_per_block = DEFAULT_CACHE_BLOCK_SIZE / WORD_SIZE;

//   printf(" TRAFFIC (in words)\n");
//   printf("  demand fetch:  %d\n", ((cache2_stat.demand_fetches) + 
//   	 cache1_stat.demand_fetches)*(cache_block_size/WORD_SIZE));
//   printf("  copies back:   %d\n", (cache2_stat.copies_back +
//   	 cache1_stat.copies_back));
// }
/************************************************************/
