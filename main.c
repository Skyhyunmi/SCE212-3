#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> // for getopt()

#define BYTES_PER_WORD 4
// #define DEBUG

/*
 * Cache structures
 */
int time = 0;

typedef struct {
	int age;
	int valid;
	int modified;
	uint32_t tag;
} cline;

typedef struct {
	cline *lines;
} cset;

typedef struct {
	int s;
	int E;
	int b;
	cset *sets;
} cache;

static int index_bit(int n) {
	int cnt = 0;
	while(n){
		cnt++;
		n = n >> 1;
	}
	return cnt-1;
}

void build_cache(cache *cache,int capacity,int way,int blocksize,int set) {
	cache->E=way;//tag 저장 가능 개수
	cache->b=index_bit(blocksize);//block사이즈 2^n n이 b
	cache->s=index_bit(set);//동일하게 set의 사이즈 2^n n이 s
	cache->sets = malloc(sizeof(cset)*set); //set의 사이즈만큼 malloc
	for(int j=0;j<set;j++){
		cache->sets[j].lines=malloc(sizeof(cline)*way);//way 사이즈 만큼 malloc
		for(int i=0;i<way;i++){
			cache->sets[j].lines[i].valid=0;
			cache->sets[j].lines[i].modified=0;
		}
	}
}

/* 
1024:8:8의 경우
32bit 주소
way = 8way(3bit), size = 8B(3bit) \
set = 10-3-3 =4bit(idx)
 -----------------------------------------------------
I  tag 25bit 그중 way는 3bit  I  set 4bit I  size 3bit  I
 -----------------------------------------------------
	         tag                  idx         block
			                     s bit        b bit 로 저장되어 있음.
*/
//int ab=0,bc=0,cd=0,de=0,ef=0,wa=0,wc=0;
void access_cache(cache *cache, const int op,const uint32_t addr,int *hit,int *miss,int *wb) {
	time++;
	int b = cache->b, s = cache->s;
	int way = cache->E;
	int longest[2]={time,0};
	uint32_t tag = addr >> (s+b);

	uint32_t idx = addr <<(32-s-b);
	idx = idx >> (32-s);

	uint32_t block = addr <<(32-b);
	block = block >>(32-b);

	switch(op){
		case 0://R
		for(int i=0;i<way;i++){//겹치는게 있으면 단지 읽을뿐이니 age는 현재시간으로
			if(cache->sets[idx].lines[i].valid&&cache->sets[idx].lines[i].tag==tag){
				cache->sets[idx].lines[i].age=time; 
				(*hit)+=1;
				return;
			}
		}
		break;

		case 1://W
		for(int i=0;i<way;i++){//겹치는게 있으면 수정했으므로 modi는 1로, 접근했으니 age는 현재시간으로
			if(cache->sets[idx].lines[i].valid&&cache->sets[idx].lines[i].tag==tag){
				cache->sets[idx].lines[i].modified=1;
				cache->sets[idx].lines[i].age=time;
				(*hit)+=1;
				return;
			}	
		}
		break;
	}		
	//여기 까지 온것은 R과 W에서 하나도 겹치는 게 없었다는것
	for(int i=0;i<way;i++){//그렇다면 메모리에서 불러와야 하므로 빈칸을 찾자!  
			if(cache->sets[idx].lines[i].valid==0){
				cache->sets[idx].lines[i].tag=tag;
				cache->sets[idx].lines[i].valid=1;
				cache->sets[idx].lines[i].age=time;
				if(op==0) cache->sets[idx].lines[i].modified=0;
				else cache->sets[idx].lines[i].modified=1;
				(*miss)+=1;
				return;
			}
		}
	//여기 까지 온것은 위에서 빈칸이 하나도 없었을 경우
	for(int i=0;i<way;i++){//제일 오래된 친구를 찾자. age가 가장 작은 친구다!
		if(cache->sets[idx].lines[i].age<longest[0]){
			longest[0]=cache->sets[idx].lines[i].age;
			longest[1]=i;
		}
	}
	//만약 가장 오래된 친구가 예전에 W에서 값이 바뀐 친구였다면 modified가 1일테니 wb의 값을 1더하자.
	if(cache->sets[idx].lines[longest[1]].modified) (*wb)+=1;
	cache->sets[idx].lines[longest[1]].tag=tag;
	cache->sets[idx].lines[longest[1]].age=time;
	if(op==0) cache->sets[idx].lines[longest[1]].modified=0;
	else cache->sets[idx].lines[longest[1]].modified=1;
	(*miss)+=1;
}

/***************************************************************/
/*                                                             */
/* Procedure : cdump                                           */
/*                                                             */
/* Purpose   : Dump cache configuration                        */
/*                                                             */
/***************************************************************/
void cdump(int capacity, int assoc, int blocksize){

	printf("Cache Configuration:\n");
    	printf("-------------------------------------\n");
	printf("Capacity: %dB\n", capacity);
	printf("Associativity: %dway\n", assoc);
	printf("Block Size: %dB\n", blocksize);
	printf("\n");
}

/***************************************************************/
/*                                                             */
/* Procedure : sdump                                           */
/*                                                             */
/* Purpose   : Dump cache stat		                           */
/*                                                             */
/***************************************************************/
void sdump(int total_reads, int total_writes, int write_backs,
	int reads_hits, int write_hits, int reads_misses, int write_misses) {
	printf("Cache Stat:\n");
    	printf("-------------------------------------\n");
	printf("Total reads: %d\n", total_reads);
	printf("Total writes: %d\n", total_writes);
	printf("Write-backs: %d\n", write_backs);
	printf("Read hits: %d\n", reads_hits);
	printf("Write hits: %d\n", write_hits);
	printf("Read misses: %d\n", reads_misses);
	printf("Write misses: %d\n", write_misses);
	printf("\n");
}


/***************************************************************/
/*                                                             */
/* Procedure : xdump                                           */
/*                                                             */
/* Purpose   : Dump current cache state                        */
/* 					                            		       */
/* Cache Design						                           */
/*  							                               */
/* 	    cache[set][way][word per block]		                   */
/*                                						       */
/*      				                        		       */
/*       ----------------------------------------	           */
/*       I        I  way0  I  way1  I  way2  I                 */
/*       ----------------------------------------              */
/*       I        I  word0 I  word0 I  word0 I                 */
/*       I  set0  I  word1 I  word1 I  work1 I                 */
/*       I        I  word2 I  word2 I  word2 I                 */
/*       I        I  word3 I  word3 I  word3 I                 */
/*       ----------------------------------------              */
/*       I        I  word0 I  word0 I  word0 I                 */
/*       I  set1  I  word1 I  word1 I  work1 I                 */
/*       I        I  word2 I  word2 I  word2 I                 */
/*       I        I  word3 I  word3 I  word3 I                 */
/*       ----------------------------------------              */
/*                              						       */
/*                                                             */
/***************************************************************/
void xdump(cache* L)
{
	int i,j,k = 0;
	int b = L->b, s = L->s;
	int way = L->E, set = 1 << s;
	int E = index_bit(way);

	uint32_t line;

	printf("Cache Content:\n");
    	printf("-------------------------------------\n");
	for(i = 0; i < way;i++)
	{
		if(i == 0)
		{
			printf("    ");
		}
		printf("      WAY[%d]",i);
	}
	printf("\n");

	for(i = 0 ; i < set;i++)
	{
		printf("SET[%d]:   ",i);
		for(j = 0; j < way;j++)
		{
			if(k != 0 && j == 0)
			{
				printf("          ");
			}
			if(L->sets[i].lines[j].valid){
				line = L->sets[i].lines[j].tag << (s+b);
				line = line|(i << b);
			}
			else{
				line = 0;
			}
			printf("0x%08x  ", line);
		}
		printf("\n");
	}
	printf("\n");
}




int main(int argc, char *argv[]) {
	int i, j, k;
	int capacity=1024;
	int way=8;
	int blocksize=8;
	int set;

	//cache
	cache simCache;

	// counts
	int read=0, write=0, writeback=0;
	int readhit=0, writehit=0;
	int readmiss=0, writemiss = 0;

	// Input option
	int opt = 0;
	char* token;
	int xflag = 0;

	// parse file
	char *trace_name = (char*)malloc(32);
	FILE *fp;
    char line[16];
    char *op;
    uint32_t addr;

    /* You can define any variables that you want */

	trace_name = argv[argc-1];
	if (argc < 3) {
		printf("Usage: %s -c cap:assoc:block_size [-x] input_trace \n",argv[0]);
		exit(1);
	}
	while((opt = getopt(argc, argv, "c:x")) != -1){
		switch(opt){
			case 'c':
                // extern char *optarg;
				token = strtok(optarg, ":");
				capacity = atoi(token);
				token = strtok(NULL, ":");
				way = atoi(token);
				token = strtok(NULL, ":");
				blocksize  = atoi(token);
				break;
			case 'x':
				xflag = 1;
				break;
			default:
			printf("Usage: %s -c cap:assoc:block_size [-x] input_trace \n",argv[0]);
			exit(1);

		}
	}

	// allocate
	set = capacity/way/blocksize;

    /* TODO: Define a cache based on the struct declaration */
    // simCache = build_cache();
	build_cache(&simCache,capacity,way,blocksize,set);
	// simulate
	fp = fopen(trace_name, "r"); // read trace file
	if(fp == NULL){
		printf("\nInvalid trace file: %s\n", trace_name);
		return 1;
	}
	cdump(capacity, way, blocksize);

    /* TODO: Build an access function to load and store data from the file */
    while (fgets(line, sizeof(line), fp) != NULL) {
        op = strtok(line, " ");
        addr = strtoull(strtok(NULL, ","), NULL, 16);
//#define DEBUG
#ifdef DEBUG
        // You can use #define DEBUG above for seeing traces of the file.
        fprintf(stderr, "op: %s\n", op);
        fprintf(stderr, "addr: %x\n", addr);
#endif
    	if(strcmp(op,"R")==0){
			read++;
			access_cache(&simCache, 0,addr,&readhit,&readmiss,&writeback);
		}  
		else{//w
			write++;
			access_cache(&simCache, 1,addr,&writehit,&writemiss,&writeback);
		}
    }

    // test example
	sdump(read, write, writeback, readhit, writehit, readmiss, writemiss);
	if (xflag){
	    	xdump(&simCache);
	}
	//printf("\n\nRead : %d Write : %d Empty : %d Full : %d WB : %d wb : %d, wc : %d\n",ab,bc,cd,de,ef,wa,wc);
    	return 0;
}
